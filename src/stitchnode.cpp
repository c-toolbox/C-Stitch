/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stitchnode.h"

#include "stitchwuffs.h"

#include <sgct/commandline.h>
#include <sgct/config.h>
#include <sgct/engine.h>
#include <sgct/image.h>
#include <sgct/opengl.h>
#include <sgct/projection/fisheye.h>
#include <sgct/projection/nonlinearprojection.h>
#include <sgct/shadermanager.h>
#include <sgct/texturemanager.h>
#include <sgct/user.h>
#include <sgct/viewport.h>
#include <sgct/window.h>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <format>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace {

struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

enum class EyeMode {
    Mono,
    Left,
    Right
};

struct StitchJob {
    std::vector<std::filesystem::path> textures;
    std::filesystem::path leftOutput;
    std::filesystem::path rightOutput;
    int start = 0;
    int stop = 0;
    int outputStart = 0;
    int steps = 1;
    std::vector<int> frameIndices;
    int loadAttemptsMax = 5;
    int loadAttemptsSleep = 1;
    int rotations[4] = { 0, 0, 0, 0 };
    bool alpha = false;
    bool stereo = false;
    EyeMode eyeMode = EyeMode::Mono;
    bool fxaa = false;
    bool cubic = true;
    double fisheyeFov = 180.0;
    double eyeSeparation = 0.065;
    double domeDiameter = 14.8;
    int msaa = 4;
    int resolution = 2048;
    int cubemapResolution = 2048;
    std::string format = "png";
    int compression = 6;
    int quality = 95;
    int loadingThreads = 8;
    int encodingThreads = 8;
    bool verifyOnly = false;
};

struct SequencePattern {
    std::filesystem::path directory;
    std::string prefix;
    std::string suffix;
    int digitCount = 0;
    bool numbered = false;
};

struct FileSizeObservation {
    std::uintmax_t bytes = 0;
    bool known = false;
};

bool hasValue(const std::vector<std::string> &arguments, std::size_t index)
{
    return index + 1 < arguments.size();
}

int toInt(const std::string &value, int fallback)
{
    try {
        return std::stoi(value);
    }
    catch (...) {
        return fallback;
    }
}

double toDouble(const std::string &value, double fallback)
{
    try {
        return std::stod(value);
    }
    catch (...) {
        return fallback;
    }
}

int normalizeCubemapThreadCount(int value, int fallback)
{
    if (value <= 0) {
        value = fallback;
    }
    value = std::clamp(value, 4, 128);
    return std::max(4, (value / 4) * 4);
}

std::string normalizedFormat(std::string format)
{
    std::ranges::transform(format, format.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (format == "jpg") {
        return "jpeg";
    }
    if (format != "png" && format != "jpeg" && format != "tga") {
        return "png";
    }
    return format;
}

std::string extensionForFormat(const std::string &format)
{
    if (format == "jpeg") {
        return ".jpg";
    }
    if (format == "tga") {
        return ".tga";
    }
    return ".png";
}

std::string lowerExtension(std::filesystem::path path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

std::filesystem::path ensureOutputExtension(std::filesystem::path path, const std::string &format)
{
    const std::string wantedExtension = extensionForFormat(format);
    if (lowerExtension(path) != wantedExtension) {
        path += wantedExtension;
    }
    return path;
}

std::filesystem::path outputFramePath(const std::filesystem::path &basePath, const StitchJob &job, int outputFrame, int totalFrameCount)
{
    std::filesystem::path path = basePath;
    if (totalFrameCount > 1) {
        std::ostringstream suffix;
        suffix << '_' << std::setw(6) << std::setfill('0') << outputFrame;
        path = basePath.parent_path() / (basePath.filename().string() + suffix.str());
    }
    return ensureOutputExtension(path, job.format);
}

std::filesystem::path outputPathForWindow(const std::filesystem::path &path, int windowId, std::size_t windowCount)
{
    if (windowCount <= 1) {
        return path;
    }

    return path.parent_path() / std::format("{}_win{}{}", path.stem().string(), windowId, path.extension().string());
}

std::string_view eyeModeName(EyeMode eyeMode)
{
    switch (eyeMode) {
        case EyeMode::Left: return "left";
        case EyeMode::Right: return "right";
        case EyeMode::Mono: return "mono";
    }
    return "mono";
}

double activeEyeSeparation(const StitchJob &job)
{
    return job.eyeMode == EyeMode::Mono ? 0.0 : job.eyeSeparation;
}

double offAxisFisheyeOffset(const StitchJob &job)
{
    const double stereoScale = activeEyeSeparation(job) / job.domeDiameter;
    switch (job.eyeMode) {
        case EyeMode::Left: return -stereoScale;
        case EyeMode::Right: return stereoScale;
        case EyeMode::Mono: return 0.0;
    }
    return 0.0;
}

int expectedFrameCount(const StitchJob &job)
{
    if (!job.frameIndices.empty()) {
        return static_cast<int>(job.frameIndices.size());
    }
    const int first = std::min(job.start, job.stop);
    const int last = std::max(job.start, job.stop);
    return ((last - first) / std::max(1, job.steps)) + 1;
}

std::vector<int> buildFrameIndices(const StitchJob &job)
{
    if (!job.frameIndices.empty()) {
        return job.frameIndices;
    }

    std::vector<int> frames;
    const int first = std::min(job.start, job.stop);
    const int last = std::max(job.start, job.stop);
    const int step = std::max(1, job.steps);
    frames.reserve(static_cast<std::size_t>(((last - first) / step) + 1));
    for (int frame = first; frame <= last; frame += step) {
        frames.push_back(frame);
    }
    return frames;
}

int outputOffsetForFrame(const StitchJob &job, int frameIndex, int frameOffset)
{
    const int first = std::min(job.start, job.stop);
    const int last = std::max(job.start, job.stop);
    const int step = std::max(1, job.steps);
    if (frameIndex >= first && frameIndex <= last && (frameIndex - first) % step == 0) {
        return (frameIndex - first) / step;
    }
    return frameOffset;
}

std::vector<int> parseFrameList(std::string text)
{
    std::ranges::replace(text, ';', ',');
    std::ranges::replace(text, '\n', ',');
    std::ranges::replace(text, '\t', ',');
    std::ranges::replace(text, ' ', ',');

    std::vector<int> frames;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (token.empty()) {
            continue;
        }

        const std::size_t rangeSeparator = token.find("..");
        if (rangeSeparator != std::string::npos) {
            const int first = toInt(token.substr(0, rangeSeparator), std::numeric_limits<int>::min());
            const int last = toInt(token.substr(rangeSeparator + 2), std::numeric_limits<int>::min());
            if (first == std::numeric_limits<int>::min() || last == std::numeric_limits<int>::min()) {
                continue;
            }
            const int direction = last >= first ? 1 : -1;
            for (int frame = first; frame != last + direction; frame += direction) {
                if (std::ranges::find(frames, frame) == frames.end()) {
                    frames.push_back(frame);
                }
            }
            continue;
        }

        const int frame = toInt(token, std::numeric_limits<int>::min());
        if (frame != std::numeric_limits<int>::min() && std::ranges::find(frames, frame) == frames.end()) {
            frames.push_back(frame);
        }
    }
    return frames;
}

std::string joinFrameIndices(const std::vector<int> &frames)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < frames.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }
        stream << frames[index];
    }
    return stream.str();
}

std::string formatFileSize(std::uintmax_t bytes)
{
    constexpr std::uintmax_t KiB = 1024;
    constexpr std::uintmax_t MiB = KiB * 1024;
    if (bytes >= MiB) {
        return std::format("{:.1f} MiB", static_cast<double>(bytes) / static_cast<double>(MiB));
    }
    if (bytes >= KiB) {
        return std::format("{:.1f} KiB", static_cast<double>(bytes) / static_cast<double>(KiB));
    }
    return std::format("{} bytes", bytes);
}

bool isSmallerByPercent(std::uintmax_t bytes, std::uintmax_t referenceBytes, int percent)
{
    if (percent <= 0 || referenceBytes == 0 || bytes >= referenceBytes) {
        return false;
    }
    const double ratio = static_cast<double>(bytes) / static_cast<double>(referenceBytes);
    return ratio <= (1.0 - static_cast<double>(percent) / 100.0);
}

SequencePattern parseSequencePattern(const std::filesystem::path &path)
{
    SequencePattern pattern;
    pattern.directory = path.parent_path();
    pattern.suffix = path.extension().string();

    const std::string stem = path.stem().string();
    int digitCount = 0;
    for (auto it = stem.rbegin(); it != stem.rend(); ++it) {
        if (!std::isdigit(static_cast<unsigned char>(*it))) {
            break;
        }
        ++digitCount;
    }

    pattern.numbered = digitCount > 0;
    pattern.digitCount = digitCount;
    pattern.prefix = pattern.numbered ? stem.substr(0, stem.size() - static_cast<std::size_t>(digitCount)) : stem;
    return pattern;
}

std::filesystem::path framePathFor(const std::filesystem::path &path, int frameIndex)
{
    const SequencePattern pattern = parseSequencePattern(path);
    if (!pattern.numbered) {
        return path;
    }

    std::ostringstream filename;
    filename << pattern.prefix << std::setw(pattern.digitCount) << std::setfill('0') << frameIndex << pattern.suffix;
    return pattern.directory / filename.str();
}

RgbaImage convertToRgba(const CStitch::Wuffs::Image &image)
{
    RgbaImage output;
    output.width = image.width;
    output.height = image.height;
    output.pixels.resize(static_cast<std::size_t>(output.width) * static_cast<std::size_t>(output.height) * 4u);

    const std::size_t bytesPerPixel = image.bytesPerPixel();
    for (int y = 0; y < output.height; ++y) {
        for (int x = 0; x < output.width; ++x) {
            const std::size_t src = (static_cast<std::size_t>(y) * static_cast<std::size_t>(output.width) + static_cast<std::size_t>(x)) * bytesPerPixel;
            const std::size_t dst = (static_cast<std::size_t>(y) * static_cast<std::size_t>(output.width) + static_cast<std::size_t>(x)) * 4u;
            switch (image.format) {
                case CStitch::Wuffs::Image::PixelDataFormat::Gray8:
                    output.pixels[dst + 0] = image.pixels[src];
                    output.pixels[dst + 1] = image.pixels[src];
                    output.pixels[dst + 2] = image.pixels[src];
                    output.pixels[dst + 3] = 255;
                    break;
                case CStitch::Wuffs::Image::PixelDataFormat::Rgb8:
                    output.pixels[dst + 0] = image.pixels[src + 0];
                    output.pixels[dst + 1] = image.pixels[src + 1];
                    output.pixels[dst + 2] = image.pixels[src + 2];
                    output.pixels[dst + 3] = 255;
                    break;
                case CStitch::Wuffs::Image::PixelDataFormat::Bgr8:
                    output.pixels[dst + 0] = image.pixels[src + 2];
                    output.pixels[dst + 1] = image.pixels[src + 1];
                    output.pixels[dst + 2] = image.pixels[src + 0];
                    output.pixels[dst + 3] = 255;
                    break;
                case CStitch::Wuffs::Image::PixelDataFormat::Rgba8:
                    output.pixels[dst + 0] = image.pixels[src + 0];
                    output.pixels[dst + 1] = image.pixels[src + 1];
                    output.pixels[dst + 2] = image.pixels[src + 2];
                    output.pixels[dst + 3] = image.pixels[src + 3];
                    break;
                case CStitch::Wuffs::Image::PixelDataFormat::Bgra8:
                    output.pixels[dst + 0] = image.pixels[src + 2];
                    output.pixels[dst + 1] = image.pixels[src + 1];
                    output.pixels[dst + 2] = image.pixels[src + 0];
                    output.pixels[dst + 3] = image.pixels[src + 3];
                    break;
            }
        }
    }
    return output;
}

bool loadImage(const std::filesystem::path &path, RgbaImage &image, CStitch::Wuffs::DecodeContext &context, std::string *error)
{
    CStitch::Wuffs::Image decoded;
    if (!CStitch::Wuffs::decodeFile(path, decoded, context, error)) {
        return false;
    }
    image = convertToRgba(decoded);
    return true;
}

bool loadImageWithAttempts(const std::filesystem::path &path, RgbaImage &image, CStitch::Wuffs::DecodeContext &context, const StitchJob &job, std::string *error)
{
    std::string lastError;
    const int attempts = std::max(1, job.loadAttemptsMax);
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        std::error_code beforeError;
        const std::uintmax_t beforeSize = std::filesystem::file_size(path, beforeError);

        std::string decodeError;
        const bool decoded = loadImage(path, image, context, &decodeError);

        std::error_code afterError;
        const std::uintmax_t afterSize = std::filesystem::file_size(path, afterError);
        const bool changedDuringRead = !beforeError && !afterError && beforeSize != afterSize;

        if (decoded && !changedDuringRead) {
            return true;
        }

        lastError = changedDuringRead
            ? std::format("Image file size changed while reading '{}': {} -> {}", path.string(), formatFileSize(beforeSize), formatFileSize(afterSize))
            : (decodeError.empty() ? std::format("Could not load {}", path.string()) : std::move(decodeError));

        if (attempt < attempts && job.loadAttemptsSleep > 0) {
            std::this_thread::sleep_for(std::chrono::seconds(job.loadAttemptsSleep));
        }
    }

    if (error) {
        *error = std::move(lastError);
    }
    return false;
}

sgct::Image toSgctImage(const RgbaImage &image)
{
    sgct::Image output;
    output.setSize(sgct::ivec2{ image.width, image.height });
    output.setChannels(4);
    output.setBytesPerChannel(1);
    output.allocateOrResizeData();

    unsigned char *target = output.data();
    for (int y = 0; y < image.height; ++y) {
        const int targetY = image.height - 1 - y;
        for (int x = 0; x < image.width; ++x) {
            const std::size_t sourceOffset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4u;
            const std::size_t targetOffset = (static_cast<std::size_t>(targetY) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4u;
            target[targetOffset + 0] = image.pixels[sourceOffset + 2];
            target[targetOffset + 1] = image.pixels[sourceOffset + 1];
            target[targetOffset + 2] = image.pixels[sourceOffset + 0];
            target[targetOffset + 3] = image.pixels[sourceOffset + 3];
        }
    }

    return output;
}

struct LoadedFrame {
    int frameIndex = 0;
    int frameOffset = 0;
    std::array<RgbaImage, 4> images;
    int remainingFaces = 4;
    bool failed = false;
    std::string error;
};

struct LoadTask {
    std::shared_ptr<LoadedFrame> frame;
    std::size_t faceIndex = 0;
    std::filesystem::path path;
};

class CubemapFrameLoader {
public:
    explicit CubemapFrameLoader(const StitchJob &job)
        : m_job(job)
        , m_frameIndices(buildFrameIndices(job))
        , m_totalFrames(static_cast<int>(m_frameIndices.size()))
        , m_threadCount(normalizeCubemapThreadCount(job.loadingThreads, 8))
        , m_maxBufferedFrames(std::max(1, m_threadCount / 4))
    {
        for (std::size_t index = 0; index < m_inputPaths.size(); ++index) {
            m_inputPaths[index] = job.textures[index];
        }
    }

    ~CubemapFrameLoader()
    {
        stop();
    }

    void start()
    {
        {
            std::lock_guard lock(m_mutex);
            scheduleMoreLocked();
        }
        for (int index = 0; index < m_threadCount; ++index) {
            m_workers.emplace_back(&CubemapFrameLoader::workerLoop, this);
        }
    }

    bool next(std::array<RgbaImage, 4> &images, int &frameIndex, int &frameOffset, bool &skipped, std::string &error)
    {
        std::unique_lock lock(m_mutex);
        m_ready.wait(lock, [this]() {
            return m_stopping || (!m_frames.empty() && m_frames.front()->remainingFaces == 0);
        });

        if (m_stopping || m_frames.empty()) {
            error = "Image loading stopped";
            return false;
        }

        std::shared_ptr<LoadedFrame> frame = m_frames.front();
        m_frames.pop_front();
        scheduleMoreLocked();
        lock.unlock();
        m_tasksAvailable.notify_all();

        frameIndex = frame->frameIndex;
        frameOffset = frame->frameOffset;
        if (frame->failed) {
            skipped = true;
            error = frame->error.empty() ? "Could not load cubemap frame" : frame->error;
            return true;
        }

        skipped = false;
        images = std::move(frame->images);
        return true;
    }

    void stop()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stopping = true;
        }
        m_tasksAvailable.notify_all();
        m_ready.notify_all();
        for (std::thread &worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_workers.clear();
    }

    int threadCount() const { return m_threadCount; }
    int maxBufferedFrames() const { return m_maxBufferedFrames; }

private:
    void scheduleMoreLocked()
    {
        while (!m_stopping && m_scheduledFrames < m_totalFrames && static_cast<int>(m_frames.size()) < m_maxBufferedFrames) {
            auto frame = std::make_shared<LoadedFrame>();
            frame->frameOffset = m_scheduledFrames;
            frame->frameIndex = m_frameIndices[static_cast<std::size_t>(m_scheduledFrames)];
            m_frames.push_back(frame);

            for (std::size_t faceIndex = 0; faceIndex < m_inputPaths.size(); ++faceIndex) {
                m_tasks.push_back({ frame, faceIndex, framePathFor(m_inputPaths[faceIndex], frame->frameIndex) });
            }
            ++m_scheduledFrames;
        }
    }

    void workerLoop()
    {
        CStitch::Wuffs::DecodeContext context;
        while (true) {
            LoadTask task;
            {
                std::unique_lock lock(m_mutex);
                m_tasksAvailable.wait(lock, [this]() {
                    return m_stopping || !m_tasks.empty();
                });
                if (m_stopping && m_tasks.empty()) {
                    return;
                }
                task = std::move(m_tasks.front());
                m_tasks.pop_front();
            }

            RgbaImage image;
            std::string error;
            const bool ok = loadImageWithAttempts(task.path, image, context, m_job, &error);

            {
                std::lock_guard lock(m_mutex);
                if (ok) {
                    task.frame->images[task.faceIndex] = std::move(image);
                }
                else {
                    task.frame->failed = true;
                    if (task.frame->error.empty()) {
                        task.frame->error = error.empty() ? std::format("Could not load {}", task.path.string()) : error;
                    }
                }
                --task.frame->remainingFaces;
            }
            m_ready.notify_all();
        }
    }

    const StitchJob &m_job;
    std::array<std::filesystem::path, 4> m_inputPaths;
    std::vector<int> m_frameIndices;
    const int m_totalFrames = 0;
    const int m_threadCount = 4;
    const int m_maxBufferedFrames = 1;
    int m_scheduledFrames = 0;
    bool m_stopping = false;
    std::mutex m_mutex;
    std::condition_variable m_tasksAvailable;
    std::condition_variable m_ready;
    std::deque<LoadTask> m_tasks;
    std::deque<std::shared_ptr<LoadedFrame>> m_frames;
    std::vector<std::thread> m_workers;
};

struct SaveTask {
    std::filesystem::path path;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgbaBottomUp;
};

bool writeStbImage(const SaveTask &task, const StitchJob &job, std::string &error)
{
    const std::filesystem::path target = std::filesystem::absolute(task.path);
    if (!target.parent_path().empty()) {
        std::filesystem::create_directories(target.parent_path());
    }
    std::error_code removeError;
    std::filesystem::remove(target, removeError);

    std::vector<std::uint8_t> topDownRgba(task.rgbaBottomUp.size());
    const std::size_t rowSize = static_cast<std::size_t>(task.width) * 4u;
    for (int y = 0; y < task.height; ++y) {
        const std::size_t source = static_cast<std::size_t>(task.height - 1 - y) * rowSize;
        const std::size_t destination = static_cast<std::size_t>(y) * rowSize;
        std::copy_n(task.rgbaBottomUp.data() + source, rowSize, topDownRgba.data() + destination);
    }

    const std::string path = target.string();
    int result = 0;
    if (job.format == "jpeg") {
        std::vector<std::uint8_t> topDownRgb(static_cast<std::size_t>(task.width) * static_cast<std::size_t>(task.height) * 3u);
        for (int pixel = 0; pixel < task.width * task.height; ++pixel) {
            topDownRgb[static_cast<std::size_t>(pixel) * 3u + 0u] = topDownRgba[static_cast<std::size_t>(pixel) * 4u + 0u];
            topDownRgb[static_cast<std::size_t>(pixel) * 3u + 1u] = topDownRgba[static_cast<std::size_t>(pixel) * 4u + 1u];
            topDownRgb[static_cast<std::size_t>(pixel) * 3u + 2u] = topDownRgba[static_cast<std::size_t>(pixel) * 4u + 2u];
        }
        result = stbi_write_jpg(path.c_str(), task.width, task.height, 3, topDownRgb.data(), job.quality);
    }
    else if (job.format == "tga") {
        result = stbi_write_tga(path.c_str(), task.width, task.height, 4, topDownRgba.data());
    }
    else {
        result = stbi_write_png(path.c_str(), task.width, task.height, 4, topDownRgba.data(), static_cast<int>(rowSize));
    }

    if (result == 0) {
        error = std::format("Could not write {}", path);
        return false;
    }
    return true;
}

class StbSaveQueue {
public:
    explicit StbSaveQueue(const StitchJob &job)
        : m_job(job)
        , m_threadCount(std::clamp(job.encodingThreads, 1, 128))
    {}

    ~StbSaveQueue()
    {
        stop();
    }

    void start()
    {
        stbi_write_png_compression_level = std::clamp(m_job.compression, 0, 9);
        for (int index = 0; index < m_threadCount; ++index) {
            m_workers.emplace_back(&StbSaveQueue::workerLoop, this);
        }
    }

    void enqueue(SaveTask task)
    {
        {
            std::lock_guard lock(m_mutex);
            ++m_pendingTasks;
            m_tasks.push_back(std::move(task));
        }
        m_available.notify_one();
    }

    bool finish(std::string &error)
    {
        {
            std::unique_lock lock(m_mutex);
            m_done.wait(lock, [this]() {
                return m_pendingTasks == 0;
            });
            error = m_error;
            m_stopping = true;
        }
        m_available.notify_all();
        for (std::thread &worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_workers.clear();
        return !m_failed;
    }

    void stop()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stopping = true;
        }
        m_available.notify_all();
        for (std::thread &worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_workers.clear();
    }

    int threadCount() const { return m_threadCount; }

private:
    void workerLoop()
    {
        while (true) {
            SaveTask task;
            {
                std::unique_lock lock(m_mutex);
                m_available.wait(lock, [this]() {
                    return m_stopping || !m_tasks.empty();
                });
                if (m_stopping && m_tasks.empty()) {
                    return;
                }
                task = std::move(m_tasks.front());
                m_tasks.pop_front();
            }

            std::string error;
            const bool ok = writeStbImage(task, m_job, error);
            {
                std::lock_guard lock(m_mutex);
                if (!ok) {
                    m_failed = true;
                    if (m_error.empty()) {
                        m_error = error;
                    }
                }
                --m_pendingTasks;
            }
            m_done.notify_all();
        }
    }

    const StitchJob &m_job;
    const int m_threadCount = 1;
    std::mutex m_mutex;
    std::condition_variable m_available;
    std::condition_variable m_done;
    std::deque<SaveTask> m_tasks;
    std::vector<std::thread> m_workers;
    int m_pendingTasks = 0;
    bool m_stopping = false;
    bool m_failed = false;
    std::string m_error;
};

enum class GpuSide {
    Right = 0,
    Bottom,
    Top,
    Left
};

enum class GpuRotation {
    Deg0,
    Deg90,
    Deg180,
    Deg270
};

struct GpuVertex {
    float x = 0.0f;
    float y = 0.0f;
    float s = 0.0f;
    float t = 0.0f;
};

struct GpuGeometry {
    GLuint vao = 0;
    GLuint vbo = 0;
    std::array<GLuint, 4> ibos = { 0, 0, 0, 0 };
};

struct GpuStitchState {
    const StitchJob *job = nullptr;
    std::array<std::filesystem::path, 4> inputPaths;
    std::array<GLuint, 4> textureIds = { 0, 0, 0, 0 };
    std::array<GpuRotation, 4> rotations = {
        GpuRotation::Deg0,
        GpuRotation::Deg0,
        GpuRotation::Deg0,
        GpuRotation::Deg0
    };
    GpuGeometry geometry;
    GLint textureUniform = -1;
    std::vector<std::filesystem::path> finalOutputs;
    std::unique_ptr<CubemapFrameLoader> loader;
    std::unique_ptr<StbSaveQueue> saveQueue;
    std::vector<int> frameIndices;
    std::vector<int> failedIndices;
    int completedFrames = 0;
    int submittedFrames = 0;
    int totalFrames = 0;
    int drawCounter = 0;
    int readbackFrameOffset = -1;
    bool shouldReadback = false;
    bool readbackFailed = false;
    std::string readbackError;
    bool terminateAfterDraw = false;
};

GpuStitchState *g_gpuState = nullptr;

constexpr std::string_view GpuVertexShader = R"(
#version 330 core

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_texCoords;

out vec2 texCoords;

void main() {
    gl_Position = vec4(in_position, 0.0, 1.0);
    texCoords = in_texCoords;
}
)";

constexpr std::string_view GpuFragmentShader = R"(
#version 330 core

in vec2 texCoords;

out vec4 color;

uniform sampler2D tex;

void main() {
    color = texture(tex, texCoords);
}
)";

GpuRotation gpuRotationFromDegrees(int value)
{
    const int normalized = ((value % 360) + 360) % 360;
    switch (normalized) {
        case 90: return GpuRotation::Deg90;
        case 180: return GpuRotation::Deg180;
        case 270: return GpuRotation::Deg270;
        default: return GpuRotation::Deg0;
    }
}

std::size_t gpuSideIndexForInput(std::size_t inputIndex)
{
    switch (inputIndex) {
        case 0: return static_cast<std::size_t>(GpuSide::Left);
        case 1: return static_cast<std::size_t>(GpuSide::Right);
        case 2: return static_cast<std::size_t>(GpuSide::Top);
        case 3: return static_cast<std::size_t>(GpuSide::Bottom);
        default: return 0;
    }
}

GpuRotation gpuRotationForSide(const GpuStitchState &state, GpuSide side)
{
    switch (side) {
        case GpuSide::Left: return state.rotations[0];
        case GpuSide::Right: return state.rotations[1];
        case GpuSide::Top: return state.rotations[2];
        case GpuSide::Bottom: return state.rotations[3];
    }
    return GpuRotation::Deg0;
}

bool uploadGpuFrame(GpuStitchState &state, const std::array<RgbaImage, 4> &images)
{
    for (GLuint &textureId : state.textureIds) {
        if (textureId != 0) {
            sgct::TextureManager::instance().removeTexture(textureId);
            textureId = 0;
        }
    }

    for (std::size_t inputIndex = 0; inputIndex < images.size(); ++inputIndex) {
        sgct::Image sgctImage = toSgctImage(images[inputIndex]);
        state.textureIds[gpuSideIndexForInput(inputIndex)] = sgct::TextureManager::instance().loadTexture(
            sgctImage,
            true,
            1.0f,
            1
        );
    }

    return true;
}

void gpuDrawFace(const GpuStitchState &state, GpuRotation rotation)
{
    glBindVertexArray(state.geometry.vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.geometry.ibos[static_cast<std::size_t>(rotation)]);
    sgct::ShaderManager::instance().shaderProgram("cstitch_face").bind();
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, nullptr);
    sgct::ShaderProgram::unbind();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void gpuPreWindow()
{
    if (!g_gpuState || !g_gpuState->job) {
        return;
    }

    const StitchJob &job = *g_gpuState->job;
    sgct::Engine::defaultUser().setEyeSeparation(static_cast<float>(activeEyeSeparation(job)));

    const float fisheyeOffset = static_cast<float>(offAxisFisheyeOffset(job));

    for (const std::unique_ptr<sgct::Window> &window : sgct::Engine::instance().windows()) {
        for (const std::unique_ptr<sgct::Viewport> &viewport : window->viewports()) {
            sgct::NonLinearProjection *projection = viewport->nonLinearProjection();
            if (!projection) {
                continue;
            }
            projection->setCubemapResolution(job.cubemapResolution);
            projection->setInterpolationMode(
                job.cubic ?
                    sgct::NonLinearProjection::InterpolationMode::Cubic :
                    sgct::NonLinearProjection::InterpolationMode::Linear
            );

            if (sgct::FisheyeProjection *fisheye = dynamic_cast<sgct::FisheyeProjection *>(projection)) {
                fisheye->setDomeDiameter(static_cast<float>(job.domeDiameter));
                fisheye->setFOV(static_cast<float>(job.fisheyeFov));
                fisheye->setOffset(sgct::vec3{ fisheyeOffset, 0.0f, 0.0f });
            }
        }
    }
}

void gpuInitOpenGL(GLFWwindow *)
{
    if (!g_gpuState) {
        return;
    }

    GpuGeometry &geometry = g_gpuState->geometry;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glGenVertexArrays(1, &geometry.vao);
    glGenBuffers(1, &geometry.vbo);
    glGenBuffers(static_cast<GLsizei>(geometry.ibos.size()), geometry.ibos.data());

    const std::vector<GpuVertex> vertices = {
        { -1.0f, -1.0f, 0.0f, 0.0f }, { -1.0f,  1.0f, 0.0f, 1.0f }, {  1.0f,  1.0f, 1.0f, 1.0f }, {  1.0f, -1.0f, 1.0f, 0.0f },
        { -1.0f, -1.0f, 1.0f, 0.0f }, { -1.0f,  1.0f, 0.0f, 0.0f }, {  1.0f,  1.0f, 0.0f, 1.0f }, {  1.0f, -1.0f, 1.0f, 1.0f },
        { -1.0f, -1.0f, 1.0f, 1.0f }, { -1.0f,  1.0f, 1.0f, 0.0f }, {  1.0f,  1.0f, 0.0f, 0.0f }, {  1.0f, -1.0f, 0.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 1.0f }, { -1.0f,  1.0f, 1.0f, 1.0f }, {  1.0f,  1.0f, 1.0f, 0.0f }, {  1.0f, -1.0f, 0.0f, 0.0f }
    };

    glBindVertexArray(geometry.vao);
    glBindBuffer(GL_ARRAY_BUFFER, geometry.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(GpuVertex)), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GpuVertex), reinterpret_cast<const void *>(2 * sizeof(float)));

    const std::array<std::array<std::uint8_t, 6>, 4> indices = {{
        {{ 0, 2, 1, 0, 3, 2 }},
        {{ 4, 6, 5, 4, 7, 6 }},
        {{ 8, 10, 9, 8, 11, 10 }},
        {{ 12, 14, 13, 12, 15, 14 }}
    }};
    for (std::size_t index = 0; index < indices.size(); ++index) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry.ibos[index]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices[index].size() * sizeof(std::uint8_t)), indices[index].data(), GL_STATIC_DRAW);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    sgct::ShaderManager::instance().addShaderProgram("cstitch_face", GpuVertexShader, GpuFragmentShader);
    g_gpuState->textureUniform = glGetUniformLocation(
        sgct::ShaderManager::instance().shaderProgram("cstitch_face").id(),
        "tex"
    );
}

void gpuPreSync()
{
    if (!g_gpuState) {
        return;
    }

    GpuStitchState &state = *g_gpuState;
    if (state.completedFrames < state.totalFrames) {
        std::array<RgbaImage, 4> images;
        int frameIndex = 0;
        int frameOffset = 0;
        bool skipped = false;
        std::string error;
        if (!state.loader || !state.loader->next(images, frameIndex, frameOffset, skipped, error)) {
            state.readbackFailed = true;
            state.readbackError = error.empty() ? "Could not load cubemap frame" : error;
            sgct::Engine::instance().terminate();
            return;
        }
        ++state.completedFrames;
        if (skipped) {
            state.failedIndices.push_back(frameIndex);
            const std::string message = error.empty() ? "Could not load cubemap frame" : error;
            std::cout << "FailedFrame " << frameIndex << " " << message << "\n" << std::flush;
            std::cerr << "Skipping input index " << frameIndex << ": " << message << "\n";
            std::cout << "Progress " << state.completedFrames << " " << state.totalFrames << " " << frameIndex << "\n" << std::flush;
            if (state.completedFrames >= state.totalFrames) {
                state.terminateAfterDraw = true;
            }
            return;
        }
        if (!uploadGpuFrame(state, images)) {
            state.readbackFailed = true;
            state.readbackError = "Could not upload cubemap textures";
            sgct::Engine::instance().terminate();
            return;
        }

        state.shouldReadback = true;
        state.readbackFrameOffset = frameOffset;
        state.drawCounter = 0;
        ++state.submittedFrames;
        std::cout << "Progress " << state.completedFrames << " " << state.totalFrames << " " << frameIndex << "\n" << std::flush;
    }
    else {
        state.terminateAfterDraw = true;
    }
}

void gpuPostSyncPreDraw()
{
}

void gpuDraw(const sgct::RenderData &)
{
    if (!g_gpuState) {
        return;
    }

    GpuStitchState &state = *g_gpuState;
    const std::size_t index = static_cast<std::size_t>(state.drawCounter++ % 4);
    const GLuint textureId = state.textureIds[index];
    if (textureId == 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glBindTextureUnit(0, textureId);
    const sgct::ShaderProgram &shader = sgct::ShaderManager::instance().shaderProgram("cstitch_face");
    if (state.textureUniform >= 0) {
        glProgramUniform1i(shader.id(), state.textureUniform, 0);
    }

    const GpuSide side = static_cast<GpuSide>(index);
    gpuDrawFace(state, gpuRotationForSide(state, side));
}

void gpuPostDraw()
{
    if (!g_gpuState) {
        return;
    }

    GpuStitchState &state = *g_gpuState;
    if (state.shouldReadback && state.readbackFrameOffset >= 0) {
        state.shouldReadback = false;
        try {
            const auto &windows = sgct::Engine::instance().windows();
            if (windows.empty()) {
                throw std::runtime_error("No SGCT window available for readback");
            }

            const std::filesystem::path framePath = state.finalOutputs.at(static_cast<std::size_t>(state.readbackFrameOffset));
            for (const std::unique_ptr<sgct::Window> &windowPointer : windows) {
                const sgct::Window &window = *windowPointer;
                const sgct::ivec2 resolution = window.framebufferResolution();
                SaveTask task;
                task.path = outputPathForWindow(framePath, window.id(), windows.size());
                task.width = resolution.x;
                task.height = resolution.y;
                task.rgbaBottomUp.resize(static_cast<std::size_t>(resolution.x) * static_cast<std::size_t>(resolution.y) * 4u);

                const GLuint textureId = window.frameBufferTextureEye(sgct::Eye::MonoOrLeft);
                glBindTexture(GL_TEXTURE_2D, textureId);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, task.rgbaBottomUp.data());
                glBindTexture(GL_TEXTURE_2D, 0);

                state.saveQueue->enqueue(std::move(task));
            }
        }
        catch (const std::exception &exception) {
            state.readbackFailed = true;
            state.readbackError = exception.what();
            sgct::Engine::instance().terminate();
            return;
        }
    }

    if (state.terminateAfterDraw) {
        sgct::Engine::instance().terminate();
    }
}

void gpuCleanup()
{
    if (!g_gpuState) {
        return;
    }

    for (GLuint textureId : g_gpuState->textureIds) {
        if (textureId != 0) {
            sgct::TextureManager::instance().removeTexture(textureId);
        }
    }

    GpuGeometry &geometry = g_gpuState->geometry;
    if (geometry.vao != 0) {
        glDeleteVertexArrays(1, &geometry.vao);
    }
    if (geometry.vbo != 0) {
        glDeleteBuffers(1, &geometry.vbo);
    }
    glDeleteBuffers(static_cast<GLsizei>(geometry.ibos.size()), geometry.ibos.data());
}

sgct::config::Cluster createGpuCluster(const StitchJob &job)
{
    sgct::config::FisheyeProjection fisheye;
    fisheye.fov = static_cast<float>(job.fisheyeFov);
    fisheye.quality = job.cubemapResolution;
    fisheye.interpolation = job.cubic ? sgct::config::FisheyeProjection::Interpolation::Cubic : sgct::config::FisheyeProjection::Interpolation::Linear;
    fisheye.diameter = static_cast<float>(job.domeDiameter);
    fisheye.background = sgct::vec4{ 0.0f, 0.0f, 0.0f, job.alpha ? 0.0f : 1.0f };

    sgct::config::Viewport viewport;
    viewport.position = sgct::vec2{ 0.0f, 0.0f };
    viewport.size = sgct::vec2{ 1.0f, 1.0f };
    viewport.projection = fisheye;

    sgct::config::Window window;
    window.id = 0;
    window.name = "stitch";
    window.size = sgct::ivec2{ 512, 512 };
    window.resolution = sgct::ivec2{ job.resolution, job.resolution };
    window.isFullScreen = false;
    window.alpha = job.alpha;
    window.msaa = 1;
    window.useFxaa = job.fxaa;
    window.alwaysRender = true;
    window.takeScreenshot = true;
    window.stereo = sgct::config::Window::StereoMode::NoStereo;
    window.viewports.push_back(std::move(viewport));

    sgct::config::Node node;
    node.address = "localhost";
    node.port = 20401;
    node.windows.push_back(std::move(window));

    sgct::config::User user;
    user.eyeSeparation = static_cast<float>(activeEyeSeparation(job));
    user.position = sgct::vec3{ 0.0f, 0.0f, 0.0f };

    sgct::config::Cluster cluster;
    cluster.success = true;
    cluster.masterAddress = "localhost";
    cluster.nodes.push_back(std::move(node));
    cluster.users.push_back(std::move(user));
    return cluster;
}

bool runSgctOpenGlStitchJob(const StitchJob &job)
{
    if (job.textures.size() < 4) {
        return false;
    }

    GpuStitchState state;
    state.job = &job;
    state.frameIndices = buildFrameIndices(job);
    state.totalFrames = static_cast<int>(state.frameIndices.size());
    state.loader = std::make_unique<CubemapFrameLoader>(job);
    state.saveQueue = std::make_unique<StbSaveQueue>(job);

    for (std::size_t index = 0; index < state.inputPaths.size(); ++index) {
        state.inputPaths[index] = job.textures[index];
    }
    for (std::size_t index = 0; index < state.rotations.size(); ++index) {
        state.rotations[index] = gpuRotationFromDegrees(job.rotations[index]);
    }
    for (int frameOffset = 0; frameOffset < state.totalFrames; ++frameOffset) {
        const int inputFrame = state.frameIndices[static_cast<std::size_t>(frameOffset)];
        const int outputFrame = job.outputStart + outputOffsetForFrame(job, inputFrame, frameOffset);
        state.finalOutputs.push_back(outputFramePath(job.leftOutput, job, outputFrame, state.totalFrames));
    }

    sgct::Configuration configuration;
    configuration.isServer = true;
    configuration.nodeId = 0;
    configuration.ignoreSync = true;
    configuration.firmSync = false;
    configuration.nCaptureThreads = 1;
    configuration.addNodeNameInScreenshot = false;
    configuration.omitWindowNameInScreenshot = true;

    sgct::Engine::Callbacks callbacks;
    callbacks.preWindow = gpuPreWindow;
    callbacks.initOpenGL = gpuInitOpenGL;
    callbacks.preSync = gpuPreSync;
    callbacks.postSyncPreDraw = gpuPostSyncPreDraw;
    callbacks.draw = gpuDraw;
    callbacks.postDraw = gpuPostDraw;
    callbacks.cleanup = gpuCleanup;

    g_gpuState = &state;
    bool rendered = false;
    try {
        std::cout << std::format("Image loader using {} face worker thread(s) per eye, buffering {} cubemap frame(s)\n",
            state.loader->threadCount(), state.loader->maxBufferedFrames()) << std::flush;
        std::cout << std::format("Output saver using {} worker thread(s) per eye\n", state.saveQueue->threadCount()) << std::flush;
        state.loader->start();
        state.saveQueue->start();
        sgct::Engine::create(createGpuCluster(job), callbacks, configuration);
        sgct::Engine::instance().exec();
        rendered = state.completedFrames == state.totalFrames;
        sgct::Engine::destroy();
    }
    catch (const std::exception &exception) {
        std::cerr << "SGCT OpenGL stitch failed: " << exception.what() << "\n";
        sgct::Engine::destroy();
        g_gpuState = nullptr;
        return false;
    }
    g_gpuState = nullptr;

    state.loader->stop();
    std::string saveError;
    const bool saved = state.saveQueue->finish(saveError);
    if (!saveError.empty()) {
        std::cerr << saveError << "\n";
    }
    if (state.readbackFailed) {
        std::cerr << (state.readbackError.empty() ? std::string("Readback failed") : state.readbackError) << "\n";
    }
    if (!state.failedIndices.empty()) {
        std::cout << "Failed indices: " << joinFrameIndices(state.failedIndices) << "\n" << std::flush;
    }
    return rendered && saved && !state.readbackFailed;
}

StitchJob parseArguments(const std::vector<std::string> &arguments)
{
    StitchJob job;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string &argument = arguments[index];
        if ((argument == "-tex" || argument == "--texture") && hasValue(arguments, index)) {
            job.textures.emplace_back(arguments[++index]);
        }
        else if (argument == "-leftPath" && hasValue(arguments, index)) {
            job.leftOutput = arguments[++index];
        }
        else if (argument == "-rightPath" && hasValue(arguments, index)) {
            job.rightOutput = arguments[++index];
        }
        else if (argument == "-start" && hasValue(arguments, index)) {
            job.outputStart = toInt(arguments[++index], job.outputStart);
        }
        else if (argument == "-steps" && hasValue(arguments, index)) {
            job.steps = std::max(1, toInt(arguments[++index], job.steps));
        }
        else if (argument == "-seq" && index + 2 < arguments.size()) {
            job.start = toInt(arguments[++index], job.start);
            job.stop = toInt(arguments[++index], job.stop);
        }
        else if ((argument == "-indices" || argument == "-frameList" || argument == "--indices") && hasValue(arguments, index)) {
            job.frameIndices = parseFrameList(arguments[++index]);
        }
        else if (argument == "-loadTextureAttemptsMax" && hasValue(arguments, index)) {
            job.loadAttemptsMax = std::max(1, toInt(arguments[++index], job.loadAttemptsMax));
        }
        else if (argument == "-loadTextureAttemptsSleep" && hasValue(arguments, index)) {
            job.loadAttemptsSleep = std::max(0, toInt(arguments[++index], job.loadAttemptsSleep));
        }
        else if (argument == "-rot" && index + 4 < arguments.size()) {
            job.rotations[0] = toInt(arguments[++index], 0);
            job.rotations[1] = toInt(arguments[++index], 0);
            job.rotations[2] = toInt(arguments[++index], 0);
            job.rotations[3] = toInt(arguments[++index], 0);
        }
        else if (argument == "-alpha" && hasValue(arguments, index)) {
            job.alpha = toInt(arguments[++index], 0) != 0;
        }
        else if (argument == "-stereo" && hasValue(arguments, index)) {
            job.stereo = toInt(arguments[++index], 0) != 0;
        }
        else if (argument == "-eye" && hasValue(arguments, index)) {
            const std::string value = arguments[++index];
            if (value == "left") {
                job.eyeMode = EyeMode::Left;
            }
            else if (value == "right") {
                job.eyeMode = EyeMode::Right;
            }
            else {
                job.eyeMode = EyeMode::Mono;
            }
        }
        else if (argument == "-fxaa" && hasValue(arguments, index)) {
            job.fxaa = toInt(arguments[++index], 0) != 0;
        }
        else if (argument == "-cubic" && hasValue(arguments, index)) {
            job.cubic = toInt(arguments[++index], 1) != 0;
        }
        else if ((argument == "-fisheyeFov" || argument == "-fov") && hasValue(arguments, index)) {
            job.fisheyeFov = std::clamp(toDouble(arguments[++index], job.fisheyeFov), 1.0, 360.0);
        }
        else if (argument == "-eyeSep" && hasValue(arguments, index)) {
            job.eyeSeparation = std::max(0.0, toDouble(arguments[++index], job.eyeSeparation));
        }
        else if (argument == "-diameter" && hasValue(arguments, index)) {
            job.domeDiameter = std::max(0.001, toDouble(arguments[++index], job.domeDiameter));
        }
        else if (argument == "-msaa" && hasValue(arguments, index)) {
            job.msaa = std::clamp(toInt(arguments[++index], job.msaa), 1, 64);
        }
        else if (argument == "-res" && hasValue(arguments, index)) {
            job.resolution = std::clamp(toInt(arguments[++index], job.resolution), 64, 32768);
        }
        else if (argument == "-cubemap" && hasValue(arguments, index)) {
            job.cubemapResolution = std::clamp(toInt(arguments[++index], job.cubemapResolution), 64, 32768);
        }
        else if (argument == "-format" && hasValue(arguments, index)) {
            job.format = normalizedFormat(arguments[++index]);
        }
        else if (argument == "-compression" && hasValue(arguments, index)) {
            job.compression = std::clamp(toInt(arguments[++index], job.compression), 0, 9);
        }
        else if (argument == "-quality" && hasValue(arguments, index)) {
            job.quality = std::clamp(toInt(arguments[++index], job.quality), 1, 100);
        }
        else if ((argument == "-loadingThreads" || argument == "-image-buffering-threads") && hasValue(arguments, index)) {
            job.loadingThreads = normalizeCubemapThreadCount(toInt(arguments[++index], job.loadingThreads), job.loadingThreads);
        }
        else if ((argument == "-encodingThreads" || argument == "-max-encoder-load-threads" || argument == "-captureThreads") && hasValue(arguments, index)) {
            job.encodingThreads = std::clamp(toInt(arguments[++index], job.encodingThreads), 1, 128);
        }
        else if (argument == "-verifyOnly" || argument == "--verify-only") {
            job.verifyOnly = true;
        }
    }
    return job;
}

std::string faceName(std::size_t index)
{
    switch (index) {
        case 0: return "left";
        case 1: return "right";
        case 2: return "top";
        case 3: return "bottom";
        default: return "input";
    }
}

void reportCriticalError(const std::string &message, const std::filesystem::path &path = {})
{
    std::string fullMessage = message;
    if (!path.empty()) {
        fullMessage += std::format(" ({})", path.string());
    }
    std::cout << "CriticalError " << fullMessage << "\n" << std::flush;
    std::cerr << fullMessage << "\n";
}

bool validateTextures(const StitchJob &job, int frameIndex, std::array<FileSizeObservation, 4> &fileSizes)
{
    const std::size_t expectedTextureCount = job.stereo ? 8u : 4u;
    if (job.textures.size() < expectedTextureCount) {
        std::cerr << "Expected " << expectedTextureCount << " cubemap face texture(s), got " << job.textures.size() << ".\n";
        return false;
    }

    bool ok = true;
    CStitch::Wuffs::DecodeContext context;
    for (std::size_t index = 0; index < expectedTextureCount; ++index) {
        const std::filesystem::path texture = framePathFor(job.textures[index], frameIndex);
        std::error_code fileSizeError;
        const std::uintmax_t fileSize = std::filesystem::file_size(texture, fileSizeError);
        if (!fileSizeError && index < fileSizes.size()) {
            fileSizes[index] = { fileSize, true };
        }

        if (!std::filesystem::exists(texture)) {
            reportCriticalError(std::format("Failed to verify {} input: Missing texture", faceName(index)), texture);
            ok = false;
            continue;
        }

        CStitch::Wuffs::ImageInfo info;
        std::string error;
        if (!CStitch::Wuffs::readImageInfo(texture, info, context, &error)) {
            reportCriticalError(std::format("Failed to verify {} input: {}", faceName(index), error.empty() ? std::string("Could not read image information") : error), texture);
            ok = false;
            continue;
        }
        std::cout << "Verified " << texture.string() << " " << info.width << "x" << info.height << "\n";
    }
    return ok;
}

bool validateTextureSequences(const StitchJob &job)
{
    bool ok = true;
    const std::vector<int> frames = buildFrameIndices(job);
    std::vector<std::array<FileSizeObservation, 4>> fileSizes(frames.size());
    for (std::size_t index = 0; index < frames.size(); ++index) {
        ok = validateTextures(job, frames[index], fileSizes[index]) && ok;
    }

    constexpr int ImageSizeWarningPercent = 25;
    for (std::size_t frameOffset = 0; frameOffset < frames.size(); ++frameOffset) {
        for (std::size_t faceIndex = 0; faceIndex < fileSizes[frameOffset].size(); ++faceIndex) {
            const FileSizeObservation &current = fileSizes[frameOffset][faceIndex];
            if (!current.known) {
                continue;
            }

            std::uintmax_t referenceBytes = 0;
            if (frameOffset > 0 && fileSizes[frameOffset - 1][faceIndex].known && isSmallerByPercent(current.bytes, fileSizes[frameOffset - 1][faceIndex].bytes, ImageSizeWarningPercent)) {
                referenceBytes = std::max(referenceBytes, fileSizes[frameOffset - 1][faceIndex].bytes);
            }
            if (frameOffset + 1 < frames.size() && fileSizes[frameOffset + 1][faceIndex].known && isSmallerByPercent(current.bytes, fileSizes[frameOffset + 1][faceIndex].bytes, ImageSizeWarningPercent)) {
                referenceBytes = std::max(referenceBytes, fileSizes[frameOffset + 1][faceIndex].bytes);
            }

            if (referenceBytes > 0) {
                const std::filesystem::path texture = framePathFor(job.textures[faceIndex], frames[frameOffset]);
                std::cout << std::format("VerificationWarning {} input: Image file is at least {}% smaller than a neighboring file ({} vs {}) ({})\n",
                    faceName(faceIndex),
                    ImageSizeWarningPercent,
                    formatFileSize(current.bytes),
                    formatFileSize(referenceBytes),
                    texture.string()) << std::flush;
            }
        }
    }
    return ok;
}

int runStitchJob(const StitchJob &job)
{
    std::cout << "C-Stitch node mode\n";
    std::cout << "Fisheye FOV: " << job.fisheyeFov << " deg, dome diameter: " << job.domeDiameter << " m, eye separation: " << job.eyeSeparation << " m\n";
    std::cout << "Eye mode: " << eyeModeName(job.eyeMode)
              << ", active eye separation: " << activeEyeSeparation(job) << " m"
              << ", off-axis fisheye offset: " << offAxisFisheyeOffset(job) << "\n";
    if (job.frameIndices.empty()) {
        std::cout << "Frames: " << job.start << " -> " << job.stop << " step " << job.steps << ", output start " << job.outputStart << "\n";
    }
    else {
        std::cout << "Frames: " << joinFrameIndices(job.frameIndices) << ", output start " << job.outputStart << "\n";
    }
    std::cout << "Output: " << job.leftOutput.string();
    if (job.stereo) {
        std::cout << " / " << job.rightOutput.string();
    }
    std::cout << ", format " << job.format << ", resolution " << job.resolution
              << ", loading threads/eye " << job.loadingThreads
              << ", encoding threads/eye " << job.encodingThreads << "\n";

    if (job.leftOutput.empty()) {
        std::cerr << "Left output path is required.\n";
        return EXIT_FAILURE;
    }
    if (job.stereo) {
        std::cerr << "Combined stereo node jobs are not supported. Launch separate --node jobs with -eye left and -eye right.\n";
        return EXIT_FAILURE;
    }
    if (job.fisheyeFov > 180.0) {
        std::cerr << "Warning: four-face SGCT fisheye input only fully covers up to 180 degrees; missing regions above 180 degrees render transparent/black.\n";
    }

    if (job.verifyOnly) {
        return validateTextureSequences(job) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    std::cout << "Using SGCT OpenGL stitch pipeline\n" << std::flush;
    if (!runSgctOpenGlStitchJob(job)) {
        std::cerr << "SGCT OpenGL stitch pipeline failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

} // namespace

bool CStitch::Node::isNodeMode(int argc, char **argv)
{
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--node" || argument == "-tex" || argument == "-leftPath" || argument == "-rightPath") {
            return true;
        }
    }
    return false;
}

int CStitch::Node::run(int argc, char **argv)
{
    std::vector<std::string> arguments(argv + 1, argv + argc);
    arguments.erase(std::remove(arguments.begin(), arguments.end(), "--node"), arguments.end());

    const StitchJob job = parseArguments(arguments);
    return runStitchJob(job);
}
