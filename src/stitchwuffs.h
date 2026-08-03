/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSTITCH_STITCHWUFFS_H
#define CSTITCH_STITCHWUFFS_H

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace CStitch::Wuffs {

struct ImageInfo {
    int width = 0;
    int height = 0;
};

struct Image {
    enum class PixelDataFormat {
        Gray8,
        Rgb8,
        Bgr8,
        Rgba8,
        Bgra8
    };

    int width = 0;
    int height = 0;
    PixelDataFormat format = PixelDataFormat::Rgb8;
    std::vector<unsigned char> pixels;

    std::size_t bytesPerPixel() const;
    std::size_t byteSize() const;
};

class DecodeContext {
public:
    DecodeContext();
    DecodeContext(const DecodeContext&) = delete;
    DecodeContext& operator=(const DecodeContext&) = delete;
    ~DecodeContext();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    friend bool decodeFile(const std::filesystem::path& path, Image& image, DecodeContext& context, std::string* errorMessage);
    friend bool readImageInfo(const std::filesystem::path& path, ImageInfo& info, DecodeContext& context, std::string* errorMessage);
};

bool decodeFile(const std::filesystem::path& path, Image& image, std::string* errorMessage = nullptr);
bool decodeFile(const std::filesystem::path& path, Image& image, DecodeContext& context, std::string* errorMessage = nullptr);
bool readImageInfo(const std::filesystem::path& path, ImageInfo& info, std::string* errorMessage = nullptr);
bool readImageInfo(const std::filesystem::path& path, ImageInfo& info, DecodeContext& context, std::string* errorMessage = nullptr);

} // namespace CStitch::Wuffs

#endif // CSTITCH_STITCHWUFFS_H
