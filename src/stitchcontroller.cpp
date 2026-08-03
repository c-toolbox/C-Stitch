/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stitchcontroller.h"

#include "stitchsettings.h"
#include "utils/imagesequenceutils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

Q_LOGGING_CATEGORY(cjobClient, "cjob.client")

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {

QString defaultOutputDirectory()
{
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QDir::toNativeSeparators(QDir(pictures.isEmpty() ? QDir::currentPath() : pictures).filePath(QStringLiteral("C-Stitch")));
}

QString defaultLeftOutputDirectory()
{
    return QDir::toNativeSeparators(QDir(defaultOutputDirectory()).filePath(QStringLiteral("stitched_left")));
}

QString defaultRightOutputDirectory()
{
    return QDir::toNativeSeparators(QDir(defaultOutputDirectory()).filePath(QStringLiteral("stitched_right")));
}

QString formatDuration(qint64 milliseconds)
{
    const qint64 totalSeconds = std::max<qint64>(0, milliseconds / 1000);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

int clampedRotation(int rotation)
{
    const int normalized = ((rotation % 360) + 360) % 360;
    if (normalized == 90 || normalized == 180 || normalized == 270) {
        return normalized;
    }
    return 0;
}

QString normalizedFormat(QString format)
{
    format = format.trimmed().toLower();
    if (format == QStringLiteral("jpg")) {
        return QStringLiteral("jpeg");
    }
    if (format != QStringLiteral("png") && format != QStringLiteral("jpeg") && format != QStringLiteral("tga")) {
        return QStringLiteral("png");
    }
    return format;
}

QString normalizedStereoEye(QString eye)
{
    eye = eye.trimmed().toLower();
    if (eye == QStringLiteral("left") || eye == QStringLiteral("right")) {
        return eye;
    }
    return QStringLiteral("both");
}

int normalizedCubemapThreadCount(int threadCount)
{
    const int clamped = std::clamp(threadCount, 4, 128);
    return std::max(4, (clamped / 4) * 4);
}

QVector<int> parseCustomFrameList(const QString &text)
{
    QVector<int> frames;
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
    for (const QString &rawToken : tokens) {
        const QString token = rawToken.trimmed();
        const qsizetype rangeIndex = token.indexOf(QStringLiteral(".."));
        if (rangeIndex > 0) {
            bool firstOk = false;
            bool lastOk = false;
            const int first = token.left(rangeIndex).toInt(&firstOk);
            const int last = token.mid(rangeIndex + 2).toInt(&lastOk);
            if (!firstOk || !lastOk) {
                continue;
            }
            const int direction = last >= first ? 1 : -1;
            for (int frame = first; frame != last + direction; frame += direction) {
                if (!frames.contains(frame)) {
                    frames.push_back(frame);
                }
            }
            continue;
        }

        bool ok = false;
        const int frame = token.toInt(&ok);
        if (ok && !frames.contains(frame)) {
            frames.push_back(frame);
        }
    }
    return frames;
}

} // namespace

StitchController::StitchController(QObject *parent)
    : QObject(parent)
    , m_outputDirectory(defaultOutputDirectory())
    , m_rightOutputDirectory(defaultRightOutputDirectory())
{
    StitchSettings::self()->load();
    applyApplicationSettings();

    m_progressTimer.setInterval(1000);
    connect(&m_progressTimer, &QTimer::timeout, this, [this]() {
        updateProgress();
    });
}

QString StitchController::normalizedPath(QString path)
{
    path = path.trimmed();
    if (path.startsWith(QStringLiteral("file:///"))) {
        path = QUrl(path).toLocalFile();
    }
    return QDir::toNativeSeparators(path);
}

QString StitchController::cleanNativePath(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return QString();
    }
    return QDir::toNativeSeparators(QDir::cleanPath(path));
}

QString StitchController::absolutePathFromWorkingDirectory(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return QString();
    }
    const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (QDir::isAbsolutePath(cleanPath)) {
        return cleanPath;
    }
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(cleanPath));
}

bool StitchController::setFace(QString &target, const QString &path)
{
    const QString value = normalizedPath(path);
    const bool changed = setFaceValue(target, value);
    bool completedFaces = false;
    if (m_autoDetectCubemapFaces && !m_applyingApplicationSettings && !value.isEmpty()) {
        completedFaces = completeCubemapFacesFromPath(value);
    }

    // Auto-extract filename stem from the loaded face path and set as outputName.
    // This extracts the meaningful name portion by removing separators followed by
    // eye/face designations, index numbers, etc. Updates every time a new file is
    // loaded (not when applying saved settings).
    if (!m_applyingApplicationSettings && !value.isEmpty()) {
        QString stem = extractFilenameStem(value);
        if (!stem.isEmpty() && m_outputName != stem) {
            m_outputName = stem;
            Q_EMIT outputChanged();
        }
    }

    if (changed || completedFaces) {
        Q_EMIT faceInputsChanged();
        emitCommandSignals();
    }
    if (!m_applyingApplicationSettings && !value.isEmpty() && (changed || completedFaces)) {
        indexSequence(true);
    }
    return changed || completedFaces;
}

bool StitchController::setFaceValue(QString &target, const QString &path)
{
    const QString value = normalizedPath(path);
    if (target == value) {
        return false;
    }
    target = value;
    return true;
}

struct CubemapPattern {
    // Pattern name for debugging
    const char* name;
    
    // Array of face directories in this pattern
    struct FaceDef {
        QString directory;
        QString *target;
        bool rightEye;
    };
    std::array<FaceDef, 8> faces;
};

QString StitchController::extractFilenameStem(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return QString();
    }

    // Get just the filename without directory path
    QFileInfo fi(filePath);
    QString fileName = fi.completeBaseName();  // Filename without extension

    // Remove trailing index numbers: everything after the last sequence of 
    // letters/digits followed by only digits at the end
    // e.g., "myimage_0001" -> "myimage", "photo.2024.07.15_0042" -> "photo.2024.07.15"
    
    // First, try to strip trailing numbers that look like frame indices
    // Pattern: optional separator (_ . -) followed by digits at the end
    QRegularExpression indexRe(QStringLiteral("([_.\\-])\\d+$"));
    QRegularExpressionMatch match = indexRe.match(fileName);
    if (match.hasMatch()) {
        fileName = fileName.left(match.capturedStart());
    }

    // Now strip everything after the first special character, space, or digit sequence
    // that separates meaningful name from metadata
    // We want to keep only: letters, digits (at start), and common name chars like / . _ -
    // Stop at: spaces, or sequences like "_001", ".left.", etc.
    
    // Find the first occurrence of a separator followed by something that looks 
    // like eye designation, face name, or index marker
    QRegularExpression stemRe(QStringLiteral("([_.\\-])([a-zA-Z]+|[0-9])"));
    match = stemRe.match(fileName);
    if (match.hasMatch()) {
        fileName = fileName.left(match.capturedStart());
    }

    // Clean up: remove trailing special characters
    while (!fileName.isEmpty() && (fileName.back().isSpace() || fileName.back() == QLatin1Char('_') || 
           fileName.back() == QLatin1Char('.') || fileName.back() == QLatin1Char('-'))) {
        fileName.chop(1);
    }

    return fileName;
}

bool StitchController::completeCubemapFacesFromPath(const QString &path)
{
    const QFileInfo selected(path);
    if (!selected.exists() || !selected.isFile()) {
        return false;
    }

    // Get the directory name containing the face image
    const QDir faceDirectory(selected.absolutePath());
    const QString faceDirectoryName = faceDirectory.dirName();
    
    // First, scan the parent directory to get all subdirectory names
    QDir cubemapDirectory(faceDirectory);
    if (!cubemapDirectory.cdUp()) {
        return false;
    }
    
    const QStringList subdirectories = cubemapDirectory.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    // Pattern definitions - ordered by priority (first match wins!)
    // To add a new pattern:
    //   1. Add a new entry to this array below the existing patterns
    //   2. Give it a descriptive name and list all 8 face directories in order:
    //      left, right, top, bottom (for left eye), then left, right, top, bottom (for right eye)
    //   3. The pattern will be automatically detected when a matching directory is found
    const CubemapPattern patterns[] = {
        // Priority 1: Legacy pattern (LL, LR, LT, LB, RL, RR, RT, RB)
        {
            "Nr1",
            {{
                { QStringLiteral("LL"), &m_leftFace, false },
                { QStringLiteral("LR"), &m_rightFace, false },
                { QStringLiteral("LT"), &m_topFace, false },
                { QStringLiteral("LB"), &m_bottomFace, false },
                { QStringLiteral("RL"), &m_rightLeftFace, true },
                { QStringLiteral("RR"), &m_rightRightFace, true },
                { QStringLiteral("RT"), &m_rightTopFace, true },
                { QStringLiteral("RB"), &m_rightBottomFace, true }
            }}
        },
        // Priority 2: F:\Cubemaps\Logo pattern
        {
            "Nr2",
            {{
                { QStringLiteral("LEFT_Left"), &m_leftFace, false },
                { QStringLiteral("LEFT_Right"), &m_rightFace, false },
                { QStringLiteral("LEFT_Top"), &m_topFace, false },
                { QStringLiteral("LEFT_Bottom"), &m_bottomFace, false },
                { QStringLiteral("RIGHT_Left"), &m_rightLeftFace, true },
                { QStringLiteral("RIGHT_Right"), &m_rightRightFace, true },
                { QStringLiteral("RIGHT_Top"), &m_rightTopFace, true },
                { QStringLiteral("RIGHT_Bottom"), &m_rightBottomFace, true }
            }}
        }
    };
    
    // Try each pattern in order of priority - first match wins!
    for (const auto& pattern : patterns) {
        // For partial matching: check if any subdirectory contains the pattern string
        bool patternMatch = false;
        
        for (size_t i = 0; i < pattern.faces.size(); ++i) {
            const QString& patternDir = pattern.faces[i].directory;
            // Check if any subdirectory name contains this pattern string
            for (const QString& subdir : subdirectories) {
                if (subdir.contains(patternDir, Qt::CaseInsensitive)) {
                    patternMatch = true;
                    break;
                }
            }
            if (patternMatch) {
                break;
            }
        }
        
        // If this pattern matches, use it to populate all faces
        if (patternMatch) {
            bool changed = false;
            bool hasRightEyeFaces = true;
            const QString fileName = selected.fileName();
            
            // Populate all face directories based on the matched pattern
            for (const auto& face : pattern.faces) {
                const QString& patternDir = face.directory;
                // Find a subdirectory that contains this pattern string
                QString foundSubdir;
                for (const QString& subdir : subdirectories) {
                    if (subdir.contains(patternDir, Qt::CaseInsensitive)) {
                        foundSubdir = subdir;
                        break;
                    }
                }
                
                if (foundSubdir.isEmpty()) {
                    // This face directory doesn't exist in the pattern
                    if (face.rightEye) {
                        hasRightEyeFaces = false;
                    }
                    continue;
                }
                
                const QString candidatePath = cubemapDirectory.filePath(foundSubdir + QLatin1Char('/') + fileName);
                const QFileInfo candidate(candidatePath);
                if (!candidate.exists() || !candidate.isFile()) {
                    if (face.rightEye) {
                        hasRightEyeFaces = false;
                    }
                    continue;
                }
                changed = setFaceValue(*face.target, QDir::toNativeSeparators(candidate.absoluteFilePath())) || changed;
            }

            // Enable stereo mode if all right-eye faces are present
            if (hasRightEyeFaces && !m_stereo) {
                m_stereo = true;
                changed = true;
                Q_EMIT stitchSettingsChanged();
            }

            return changed;
        }
    }
    
    // No pattern matched
    return false;
}

QString StitchController::leftFace() const { return m_leftFace; }
void StitchController::setLeftFace(const QString &path) { setFace(m_leftFace, path); }
QString StitchController::rightFace() const { return m_rightFace; }
void StitchController::setRightFace(const QString &path) { setFace(m_rightFace, path); }
QString StitchController::topFace() const { return m_topFace; }
void StitchController::setTopFace(const QString &path) { setFace(m_topFace, path); }
QString StitchController::bottomFace() const { return m_bottomFace; }
void StitchController::setBottomFace(const QString &path) { setFace(m_bottomFace, path); }
QString StitchController::rightLeftFace() const { return m_rightLeftFace; }
void StitchController::setRightLeftFace(const QString &path) { setFace(m_rightLeftFace, path); }
QString StitchController::rightRightFace() const { return m_rightRightFace; }
void StitchController::setRightRightFace(const QString &path) { setFace(m_rightRightFace, path); }
QString StitchController::rightTopFace() const { return m_rightTopFace; }
void StitchController::setRightTopFace(const QString &path) { setFace(m_rightTopFace, path); }
QString StitchController::rightBottomFace() const { return m_rightBottomFace; }
void StitchController::setRightBottomFace(const QString &path) { setFace(m_rightBottomFace, path); }

QString StitchController::outputDirectory() const { return m_outputDirectory; }
void StitchController::setOutputDirectory(const QString &path)
{
    const QString value = normalizedPath(path);
    if (m_outputDirectory == value) return;
    m_outputDirectory = value;
    Q_EMIT outputChanged();
    emitCommandSignals();
}

QString StitchController::rightOutputDirectory() const { return m_rightOutputDirectory; }
void StitchController::setRightOutputDirectory(const QString &path)
{
    const QString value = normalizedPath(path);
    if (m_rightOutputDirectory == value) return;
    m_rightOutputDirectory = value;
    Q_EMIT outputChanged();
    emitCommandSignals();
}

QString StitchController::outputName() const { return m_outputName; }
void StitchController::setOutputName(const QString &name)
{
    const QString value = name.trimmed();
    if (m_outputName == value) return;
    m_outputName = value;
    Q_EMIT outputChanged();
    emitCommandSignals();
}

bool StitchController::stereo() const { return m_stereo; }
void StitchController::setStereo(bool stereo)
{
    if (m_stereo == stereo) return;
    m_stereo = stereo;
    Q_EMIT stitchSettingsChanged();
    emitCommandSignals();
}

QString StitchController::stereoEye() const { return m_stereoEye; }
void StitchController::setStereoEye(const QString &eye)
{
    const QString value = normalizedStereoEye(eye);
    if (m_stereoEye == value) return;
    m_stereoEye = value;
    Q_EMIT stitchSettingsChanged();
    emitCommandSignals();
}

bool StitchController::alpha() const { return m_alpha; }
void StitchController::setAlpha(bool alpha)
{
    if (m_alpha == alpha) return;
    m_alpha = alpha;
    Q_EMIT stitchSettingsChanged();
    emitCommandSignals();
}

bool StitchController::fxaa() const { return m_fxaa; }
void StitchController::setFxaa(bool fxaa)
{
    if (m_fxaa == fxaa) return;
    m_fxaa = fxaa;
    Q_EMIT stitchSettingsChanged();
    emitCommandSignals();
}

bool StitchController::cubic() const { return m_cubic; }
void StitchController::setCubic(bool cubic)
{
    if (m_cubic == cubic) return;
    m_cubic = cubic;
    Q_EMIT stitchSettingsChanged();
    emitCommandSignals();
}

bool StitchController::autoDetectCubemapFaces() const { return m_autoDetectCubemapFaces; }
void StitchController::setAutoDetectCubemapFaces(bool autoDetect)
{
    if (m_autoDetectCubemapFaces == autoDetect) return;
    m_autoDetectCubemapFaces = autoDetect;
    Q_EMIT stitchSettingsChanged();
}

double StitchController::fisheyeFov() const { return m_fisheyeFov; }
void StitchController::setFisheyeFov(double fov)
{
    const double value = std::clamp(fov, 1.0, 360.0);
    if (qFuzzyCompare(m_fisheyeFov, value)) return;
    m_fisheyeFov = value;
    Q_EMIT stitchSettingsChanged();
    emitCommandSignals();
}

double StitchController::eyeSeparation() const { return m_eyeSeparation; }
void StitchController::setEyeSeparation(double value)
{
    value = std::clamp(value, 0.0, 2.0);
    if (qFuzzyCompare(m_eyeSeparation, value)) return;
    m_eyeSeparation = value;
    Q_EMIT stitchSettingsChanged();
    emitCommandSignals();
}

double StitchController::domeDiameter() const { return m_domeDiameter; }
void StitchController::setDomeDiameter(double value)
{
    value = std::max(0.001, value);
    if (qFuzzyCompare(m_domeDiameter, value)) return;
    m_domeDiameter = value;
    Q_EMIT stitchSettingsChanged();
    emitCommandSignals();
}

int StitchController::resolution() const { return m_resolution; }
void StitchController::setResolution(int resolution)
{
    const int value = std::clamp(resolution, 64, 32768);
    if (m_resolution == value) return;
    m_resolution = value;
    Q_EMIT stitchSettingsChanged();
    emitCommandSignals();
}

int StitchController::cubemapResolution() const { return m_cubemapResolution; }
void StitchController::setCubemapResolution(int resolution)
{
    const int value = std::clamp(resolution, 64, 32768);
    if (m_cubemapResolution == value) return;
    m_cubemapResolution = value;
    Q_EMIT stitchSettingsChanged();
    emitCommandSignals();
}

int StitchController::msaa() const { return m_msaa; }
void StitchController::setMsaa(int msaa)
{
    const int value = std::clamp(msaa, 1, 64);
    if (m_msaa == value) return;
    m_msaa = value;
    Q_EMIT stitchSettingsChanged();
    emitCommandSignals();
}

QString StitchController::format() const { return m_format; }
void StitchController::setFormat(const QString &format)
{
    const QString value = normalizedFormat(format);
    if (m_format == value) return;
    m_format = value;
    Q_EMIT encodingChanged();
    emitCommandSignals();
}

int StitchController::compression() const { return m_compression; }
void StitchController::setCompression(int compression)
{
    const int value = std::clamp(compression, 0, 9);
    if (m_compression == value) return;
    m_compression = value;
    Q_EMIT encodingChanged();
    emitCommandSignals();
}

int StitchController::quality() const { return m_quality; }
void StitchController::setQuality(int quality)
{
    const int value = std::clamp(quality, 1, 100);
    if (m_quality == value) return;
    m_quality = value;
    Q_EMIT encodingChanged();
    emitCommandSignals();
}

int StitchController::loadingThreads() const { return m_loadingThreads; }
void StitchController::setLoadingThreads(int loadingThreads)
{
    const int value = normalizedCubemapThreadCount(loadingThreads);
    if (m_loadingThreads == value) return;
    m_loadingThreads = value;
    Q_EMIT encodingChanged();
    emitCommandSignals();
}

int StitchController::encodingThreads() const { return m_encodingThreads; }
void StitchController::setEncodingThreads(int encodingThreads)
{
    const int value = std::clamp(encodingThreads, 1, 128);
    if (m_encodingThreads == value) return;
    m_encodingThreads = value;
    Q_EMIT encodingChanged();
    emitCommandSignals();
}

int StitchController::startIndex() const { return m_startIndex; }
void StitchController::setStartIndex(int index)
{
    if (m_startIndex == index) return;
    m_startIndex = index;
    Q_EMIT sequenceChanged();
    emitCommandSignals();
}

int StitchController::stopIndex() const { return m_stopIndex; }
void StitchController::setStopIndex(int index)
{
    if (m_stopIndex == index) return;
    m_stopIndex = index;
    Q_EMIT sequenceChanged();
    emitCommandSignals();
}

int StitchController::outputStartIndex() const { return m_outputStartIndex; }
void StitchController::setOutputStartIndex(int index)
{
    if (m_outputStartIndex == index) return;
    m_outputStartIndex = index;
    Q_EMIT sequenceChanged();
    emitCommandSignals();
}

int StitchController::steps() const { return m_steps; }
void StitchController::setSteps(int steps)
{
    const int value = std::max(1, steps);
    if (m_steps == value) return;
    m_steps = value;
    Q_EMIT sequenceChanged();
    emitCommandSignals();
}

QString StitchController::customFrameList() const { return m_customFrameList; }
void StitchController::setCustomFrameList(const QString &list)
{
    const QString value = list.trimmed();
    if (m_customFrameList == value) return;
    m_customFrameList = value;
    Q_EMIT sequenceChanged();
    emitCommandSignals();
}

int StitchController::loadTextureAttemptsMax() const { return m_loadTextureAttemptsMax; }
void StitchController::setLoadTextureAttemptsMax(int attempts)
{
    const int value = std::clamp(attempts, 1, 1000);
    if (m_loadTextureAttemptsMax == value) return;
    m_loadTextureAttemptsMax = value;
    Q_EMIT sequenceChanged();
    emitCommandSignals();
}

int StitchController::loadTextureAttemptsSleep() const { return m_loadTextureAttemptsSleep; }
void StitchController::setLoadTextureAttemptsSleep(int seconds)
{
    const int value = std::clamp(seconds, 0, 3600);
    if (m_loadTextureAttemptsSleep == value) return;
    m_loadTextureAttemptsSleep = value;
    Q_EMIT sequenceChanged();
    emitCommandSignals();
}

int StitchController::leftRotation() const { return m_leftRotation; }
void StitchController::setLeftRotation(int rotation)
{
    const int value = clampedRotation(rotation);
    if (m_leftRotation == value) return;
    m_leftRotation = value;
    Q_EMIT rotationsChanged();
    emitCommandSignals();
}

int StitchController::rightRotation() const { return m_rightRotation; }
void StitchController::setRightRotation(int rotation)
{
    const int value = clampedRotation(rotation);
    if (m_rightRotation == value) return;
    m_rightRotation = value;
    Q_EMIT rotationsChanged();
    emitCommandSignals();
}

int StitchController::topRotation() const { return m_topRotation; }
void StitchController::setTopRotation(int rotation)
{
    const int value = clampedRotation(rotation);
    if (m_topRotation == value) return;
    m_topRotation = value;
    Q_EMIT rotationsChanged();
    emitCommandSignals();
}

int StitchController::bottomRotation() const { return m_bottomRotation; }
void StitchController::setBottomRotation(int rotation)
{
    const int value = clampedRotation(rotation);
    if (m_bottomRotation == value) return;
    m_bottomRotation = value;
    Q_EMIT rotationsChanged();
    emitCommandSignals();
}

bool StitchController::running() const { return !m_activeProcesses.isEmpty(); }
bool StitchController::sequenceIndexing() const { return m_sequenceIndexing; }
QString StitchController::sequenceStatus() const { return m_sequenceStatus; }
int StitchController::progressCompleted() const { return m_progressCompleted; }
int StitchController::progressTotal() const { return m_progressTotal; }
double StitchController::progress() const
{
    if (m_progressTotal <= 0) {
        return 0.0;
    }
    return std::clamp(static_cast<double>(m_progressCompleted) / static_cast<double>(m_progressTotal), 0.0, 1.0);
}
QString StitchController::progressStatus() const
{
    if (m_progressTotal <= 0) {
        return running() ? QStringLiteral("Preparing stitch jobs...") : QStringLiteral("No stitch running.");
    }
    if (running()) {
        return QStringLiteral("%1 of %2 frame(s)").arg(m_progressCompleted).arg(m_progressTotal);
    }
    return QStringLiteral("%1 of %2 frame(s) complete").arg(m_progressCompleted).arg(m_progressTotal);
}
QString StitchController::elapsedTime() const { return formattedElapsedTime(); }
QString StitchController::remainingTime() const { return formattedRemainingTime(); }
QString StitchController::logText() const { return m_logText; }

QStringList StitchController::buildArguments(StitchEye eye, bool verifyOnly) const
{
    QStringList arguments;
    
    // New path structure: OutputDirectory/OutputName/SubDir/
    // The subdirectory is the lowest folder, containing files named OutputName[_L|_R]
    // Example paths:
    // Mono:  C:/output/myproject/mono/      -> contains stitched.png
    // Left:  C:/output/myproject/left/      -> contains stitched_L.png
    // Right: C:/output/myproject/right/     -> contains stitched_R.png
    
    const QString outputName = m_outputName.trimmed().isEmpty() ? QStringLiteral("stitched") : m_outputName;
    const QDir outputRoot(absolutePathFromWorkingDirectory(m_outputDirectory));
    
    // Get the appropriate subdirectory for this eye type
    const QString subDir = [eye]() -> QString {
        switch (eye) {
            case StitchEye::Mono: return StitchSettings::self()->monoSubDir();
            case StitchEye::Left: return StitchSettings::self()->leftSubDir();
            case StitchEye::Right: return StitchSettings::self()->rightSubDir();
        }
        return StitchSettings::self()->monoSubDir();
    }();
    
    // Construct path: OutputDirectory/OutputName/SubDir/Filename
    // The stitcher needs the full path including filename to increment it
    QString outputFolder;
    switch (eye) {
        case StitchEye::Mono:
            outputFolder = outputRoot.filePath(outputName + QLatin1Char('/') + subDir + QLatin1Char('/') + outputName);
            break;
        case StitchEye::Left:
            outputFolder = outputRoot.filePath(outputName + QLatin1Char('/') + subDir + QLatin1Char('/') + outputName + QStringLiteral("_L"));
            break;
        case StitchEye::Right:
            outputFolder = outputRoot.filePath(outputName + QLatin1Char('/') + subDir + QLatin1Char('/') + outputName + QStringLiteral("_R"));
            break;
    }
    
    const bool leftEye = eye == StitchEye::Mono || eye == StitchEye::Left;
    
    arguments << QStringLiteral("--node")
              << QStringLiteral("-leftPath") << cleanNativePath(outputFolder)
              << QStringLiteral("-eye") << (eye == StitchEye::Left ? QStringLiteral("left") : (eye == StitchEye::Right ? QStringLiteral("right") : QStringLiteral("mono")))
              << QStringLiteral("-tex") << cleanNativePath(absolutePathFromWorkingDirectory(leftEye ? m_leftFace : m_rightLeftFace))
              << QStringLiteral("-tex") << cleanNativePath(absolutePathFromWorkingDirectory(leftEye ? m_rightFace : m_rightRightFace))
              << QStringLiteral("-tex") << cleanNativePath(absolutePathFromWorkingDirectory(leftEye ? m_topFace : m_rightTopFace))
              << QStringLiteral("-tex") << cleanNativePath(absolutePathFromWorkingDirectory(leftEye ? m_bottomFace : m_rightBottomFace));

    arguments << QStringLiteral("-start") << QString::number(m_outputStartIndex)
              << QStringLiteral("-steps") << QString::number(m_steps)
              << QStringLiteral("-seq") << QString::number(m_startIndex) << QString::number(m_stopIndex)
              << QStringLiteral("-loadTextureAttemptsMax") << QString::number(m_loadTextureAttemptsMax)
              << QStringLiteral("-loadTextureAttemptsSleep") << QString::number(m_loadTextureAttemptsSleep)
              << QStringLiteral("-rot") << QString::number(m_leftRotation) << QString::number(m_rightRotation) << QString::number(m_topRotation) << QString::number(m_bottomRotation)
              << QStringLiteral("-alpha") << QString::number(m_alpha ? 1 : 0)
              << QStringLiteral("-stereo") << QStringLiteral("0")
              << QStringLiteral("-fxaa") << QString::number(m_fxaa ? 1 : 0)
              << QStringLiteral("-cubic") << QString::number(m_cubic ? 1 : 0)
              << QStringLiteral("-fisheyeFov") << QString::number(m_fisheyeFov, 'f', 4)
              << QStringLiteral("-eyeSep") << QString::number(m_eyeSeparation, 'f', 6)
              << QStringLiteral("-diameter") << QString::number(m_domeDiameter, 'f', 6)
              << QStringLiteral("-msaa") << QString::number(m_msaa)
              << QStringLiteral("-res") << QString::number(m_resolution)
              << QStringLiteral("-cubemap") << QString::number(m_cubemapResolution)
              << QStringLiteral("-format") << m_format
              << QStringLiteral("-compression") << QString::number(m_compression)
              << QStringLiteral("-quality") << QString::number(m_quality)
              << QStringLiteral("-loadingThreads") << QString::number(m_loadingThreads)
              << QStringLiteral("-encodingThreads") << QString::number(m_encodingThreads);

    if (!m_customFrameList.trimmed().isEmpty()) {
        arguments << QStringLiteral("-indices") << m_customFrameList.trimmed();
    }

    if (verifyOnly) {
        arguments << QStringLiteral("-verifyOnly");
    }
    return arguments;
}

int StitchController::expectedFrameCount() const
{
    const QVector<int> customFrames = parseCustomFrameList(m_customFrameList);
    if (!customFrames.isEmpty()) {
        return customFrames.size();
    }
    const int first = std::min(m_startIndex, m_stopIndex);
    const int last = std::max(m_startIndex, m_stopIndex);
    return ((last - first) / std::max(1, m_steps)) + 1;
}

QVector<StitchController::ProcessLaunch> StitchController::buildLaunches(bool verifyOnly) const
{
    QVector<ProcessLaunch> launches;
    const int totalFrames = expectedFrameCount();
    if (!m_stereo) {
        launches.push_back({ buildArguments(StitchEye::Mono, verifyOnly), verifyOnly ? QStringLiteral("Verifying mono") : QStringLiteral("Launching mono"), totalFrames });
        return launches;
    }

    if (m_stereoEye != QStringLiteral("right")) {
        launches.push_back({ buildArguments(StitchEye::Left, verifyOnly), verifyOnly ? QStringLiteral("Verifying left eye") : QStringLiteral("Launching left eye"), totalFrames });
    }
    if (m_stereoEye != QStringLiteral("left")) {
        launches.push_back({ buildArguments(StitchEye::Right, verifyOnly), verifyOnly ? QStringLiteral("Verifying right eye") : QStringLiteral("Launching right eye"), totalFrames });
    }
    return launches;
}

QString StitchController::quoteArgument(const QString &argument)
{
    QString quoted = argument;
    quoted.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    if (quoted.contains(QLatin1Char(' ')) || quoted.contains(QLatin1Char('\t')) || quoted.contains(QLatin1Char('\\'))) {
        return QStringLiteral("\"") + quoted + QStringLiteral("\"");
    }
    return quoted;
}

QString StitchController::commandLinePreview() const
{
    // Show the command line arguments for each stitch job that will be launched
    // The paths shown are passed to --node, which creates them when it starts
    
    QStringList output;
    const QVector<ProcessLaunch> launches = buildLaunches(false);
    
    for (const ProcessLaunch &launch : launches) {
        output << launch.arguments.join(QLatin1Char(' '));
    }
    
    return output.join(QLatin1Char('\n'));
}

QString StitchController::firstSequencePath() const
{
    const QStringList paths = { m_leftFace, m_rightFace, m_topFace, m_bottomFace, m_rightLeftFace, m_rightRightFace, m_rightTopFace, m_rightBottomFace };
    for (const QString &path : paths) {
        if (!path.trimmed().isEmpty()) {
            return path;
        }
    }
    return QString();
}

void StitchController::indexSequence(bool adoptDetectedRange)
{
    const QString path = firstSequencePath();
    const int requestId = ++m_sequenceIndexRequestId;
    if (path.isEmpty()) {
        m_sequenceIndexing = false;
        m_sequenceStatus = QStringLiteral("Choose at least one cubemap face before indexing.");
        Q_EMIT sequenceStatusChanged();
        return;
    }

    m_sequenceIndexing = true;
    m_sequenceStatus = QStringLiteral("Indexing image sequence...");
    Q_EMIT sequenceStatusChanged();

    QThread *thread = QThread::create([this, path, adoptDetectedRange, requestId]() {
        const ImageSequenceScanResult result = ImageSequenceUtils::scanImageSequence(path);
        QMetaObject::invokeMethod(this, [this, result, adoptDetectedRange, requestId]() {
            if (requestId != m_sequenceIndexRequestId) {
                return;
            }
            m_sequenceIndexing = false;
            if (result.ok) {
                if (adoptDetectedRange) {
                    m_startIndex = result.firstIndex;
                    m_stopIndex = result.lastIndex;
                    Q_EMIT sequenceChanged();
                }

                // Log dimension check results
                QString dimensionLog;
                if (result.dimensions.width > 0 && result.dimensions.height > 0) {
                    if (result.allMatch) {
                        dimensionLog = QStringLiteral("All %1 frames have matching dimensions: %2x%3")
                            .arg(result.count)
                            .arg(result.dimensions.width)
                            .arg(result.dimensions.height);
                    } else {
                        dimensionLog = QStringLiteral("Frame dimensions do not match. First frame: %1x%2")
                            .arg(result.dimensions.width)
                            .arg(result.dimensions.height);
                    }

                    // Automatically set cubemap resolution from first frame's larger dimension
                    const int detectedResolution = std::max(result.dimensions.width, result.dimensions.height);
                    if (m_cubemapResolution != detectedResolution) {
                        m_cubemapResolution = detectedResolution;
                        Q_EMIT stitchSettingsChanged();
                        dimensionLog += QStringLiteral(" (cubemap resolution set to %1)").arg(detectedResolution);
                    }

                    // Automatically set output resolution to 4x cubemap resolution
                    const int detectedOutputResolution = std::clamp(detectedResolution * 2, 256, 16384);
                    if (m_resolution != detectedOutputResolution) {
                        m_resolution = detectedOutputResolution;
                        Q_EMIT stitchSettingsChanged();
                        dimensionLog += QStringLiteral(", output resolution set to %1x%2").arg(detectedOutputResolution).arg(detectedOutputResolution);
                    }
                } else {
                    dimensionLog = QStringLiteral("Could not determine image dimensions.");
                }
                appendLog(dimensionLog);

                m_sequenceStatus = QStringLiteral("Indexed %1 frame(s), %2 -> %3%4.")
                    .arg(result.count)
                    .arg(result.firstIndex)
                    .arg(result.lastIndex)
                    .arg(result.missingFrames ? QStringLiteral(" with gaps") : QString());
            }
            else {
                m_sequenceStatus = result.message.isEmpty() ? QStringLiteral("No numbered sequence detected.") : result.message;
            }
            Q_EMIT sequenceStatusChanged();
            emitCommandSignals();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QStringList StitchController::outputPaths() const
{
    QStringList paths;
    
    // Helper to get subdirectory for eye type
    auto getSubDir = [](StitchEye eye) -> QString {
        switch (eye) {
            case StitchEye::Mono: return StitchSettings::self()->monoSubDir();
            case StitchEye::Left: return StitchSettings::self()->leftSubDir();
            case StitchEye::Right: return StitchSettings::self()->rightSubDir();
        }
        return StitchSettings::self()->monoSubDir();
    };
    
    const QString outputName = m_outputName.trimmed().isEmpty() ? QStringLiteral("stitched") : m_outputName;
    const QDir outputRoot(absolutePathFromWorkingDirectory(m_outputDirectory));
    
    // Output paths for each eye type:
    // C:/output/myproject/mono/stitched
    // C:/output/myproject/left/stitched_L
    // C:/output/myproject/right/stitched_R
    if (!m_stereo || m_stereoEye != QStringLiteral("right")) {
        QString path = outputRoot.filePath(outputName + QLatin1Char('/') + getSubDir(StitchEye::Mono) + QLatin1Char('/') + outputName);
        paths << path;
    }
    if (m_stereo) {
        if (m_stereoEye != QStringLiteral("right")) {
            QString path = outputRoot.filePath(outputName + QLatin1Char('/') + getSubDir(StitchEye::Left) + QLatin1Char('/') + outputName + QStringLiteral("_L"));
            paths << path;
        }
        if (m_stereoEye != QStringLiteral("left")) {
            QString path = outputRoot.filePath(outputName + QLatin1Char('/') + getSubDir(StitchEye::Right) + QLatin1Char('/') + outputName + QStringLiteral("_R"));
            paths << path;
        }
    }
    
    return paths;
}

bool StitchController::validateLaunchInputs()
{
    const bool needsLeftEye = !m_stereo || m_stereoEye != QStringLiteral("right");
    const bool needsRightEye = m_stereo && m_stereoEye != QStringLiteral("left");

    if (needsLeftEye) {
        const QStringList requiredLeft = { m_leftFace, m_rightFace, m_topFace, m_bottomFace };
        if (std::ranges::any_of(requiredLeft, [](const QString &path) { return path.trimmed().isEmpty(); })) {
            appendLog(QStringLiteral("Left/mono cubemap requires left, right, top, and bottom faces."));
            return false;
        }
    }
    if (needsRightEye) {
        const QStringList requiredRight = { m_rightLeftFace, m_rightRightFace, m_rightTopFace, m_rightBottomFace };
        if (std::ranges::any_of(requiredRight, [](const QString &path) { return path.trimmed().isEmpty(); })) {
            appendLog(QStringLiteral("Right-eye cubemap requires left, right, top, and bottom faces."));
            return false;
        }
    }
    if (m_outputName.trimmed().isEmpty()) {
        appendLog(QStringLiteral("Output filename is required."));
        return false;
    }
    if (m_outputDirectory.trimmed().isEmpty()) {
        appendLog(QStringLiteral("Output directory is required."));
        return false;
    }
    return true;
}

void StitchController::startNodeProcesses(QVector<ProcessLaunch> launches)
{
    if (running()) {
        appendLog(QStringLiteral("C-Stitch node is already running."));
        return;
    }
    if (!validateLaunchInputs()) {
        return;
    }

    // Output paths for each eye type:
    // Mono:  OutputDirectory/OutputName/MonoSubDir/
    // Left:  OutputDirectory/OutputName/LeftSubDir/  
    // Right: OutputDirectory/OutputName/RightSubDir/
    // Note: C-Stitch node creates these directories when it starts

    int totalFrames = 0;
    for (const ProcessLaunch &launch : launches) {
        totalFrames += launch.totalFrames;
    }
    resetProgress(totalFrames);
    appendLog(QStringLiteral("Existing output files are overwritten."));

    const QVector<ProcessLaunch> launchCopies = std::move(launches);
    for (const ProcessLaunch &launch : launchCopies) {
        startNodeProcess(launch);
    }
    if (m_activeProcesses.isEmpty()) {
        m_progressTimer.stop();
        return;
    }

    Q_EMIT runningChanged();
    updateProgress();
}

void StitchController::startNodeProcess(const ProcessLaunch &launch)
{
    const QStringList &arguments = launch.arguments;

    QStringList parts;
    parts << quoteArgument(QCoreApplication::applicationFilePath());
    for (const QString &argument : arguments) {
        parts << quoteArgument(argument);
    }
    appendLog(QStringLiteral("%1: %2").arg(launch.activity, parts.join(QLatin1Char(' '))));

    auto *state = new ProcessState;
    state->launch = launch;
    state->totalFrames = launch.totalFrames;
    state->process = new QProcess(this);
    m_activeProcesses.push_back(state);

    QProcess *process = state->process;
    process->setProgram(QCoreApplication::applicationFilePath());
    process->setArguments(arguments);
    process->setWorkingDirectory(QCoreApplication::applicationDirPath());
    process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(process, &QProcess::readyReadStandardOutput, this, [this, state]() {
        handleProcessOutput(state, QProcess::StandardOutput);
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, state]() {
        handleProcessOutput(state, QProcess::StandardError);
    });
    connect(process, &QProcess::started, this, [this, launch]() {
        appendLog(QStringLiteral("%1 started.").arg(launch.activity));
    });
    connect(process, &QProcess::errorOccurred, this, [this, state](QProcess::ProcessError error) {
        state->failed = true;
        appendLog(QStringLiteral("%1 process error: %2").arg(state->launch.activity, state->process->errorString()));
        if (error == QProcess::FailedToStart) {
            QMetaObject::invokeMethod(this, [this, state]() {
                if (m_activeProcesses.contains(state)) {
                    finishNodeProcess(state, -1, QProcess::CrashExit);
                }
            }, Qt::QueuedConnection);
            return;
        }
        updateProgress();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, state](int exitCode, QProcess::ExitStatus exitStatus) {
            finishNodeProcess(state, exitCode, exitStatus);
        });

    process->start();
}

void StitchController::finishNodeProcess(ProcessState *state, int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!state || !state->process) {
        return;
    }
    if (state->finishing) {
        return;
    }
    state->finishing = true;

    handleProcessOutput(state, QProcess::StandardOutput);
    handleProcessOutput(state, QProcess::StandardError);
    if (!state->standardOutputBuffer.isEmpty()) {
        processOutputLine(state, QString::fromLocal8Bit(state->standardOutputBuffer), QProcess::StandardOutput);
        state->standardOutputBuffer.clear();
    }
    if (!state->standardErrorBuffer.isEmpty()) {
        processOutputLine(state, QString::fromLocal8Bit(state->standardErrorBuffer), QProcess::StandardError);
        state->standardErrorBuffer.clear();
    }

    const bool ok = !state->failed && exitStatus == QProcess::NormalExit && exitCode == 0;
    m_processFailure = m_processFailure || !ok;
    if (ok) {
        m_finishedFrames += state->totalFrames;
    }
    appendLog(QStringLiteral("%1 finished with exit code %2 (%3).").arg(state->launch.activity).arg(exitCode).arg(exitStatus == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crashed")));

    QProcess *process = state->process;
    m_activeProcesses.removeOne(state);
    process->deleteLater();
    delete state;

    if (m_activeProcesses.isEmpty()) {
        m_progressTimer.stop();
        m_progressCompleted = m_processFailure ? std::min(m_progressCompleted, m_progressTotal) : m_progressTotal;
        Q_EMIT runningChanged();
        
        // If we were processing a job from C-Job, notify the server that it's complete
        if (!m_currentCJobJobId.isEmpty()) {
            qCInfo(cjobClient) << "Job complete, notifying C-Job server";
            
            QJsonObject json;
            json[QStringLiteral("type")] = QStringLiteral("complete_job");
            json[QStringLiteral("job_id")] = m_currentCJobJobId;
            
            sendJsonToCJob(json);
            m_currentCJobJobId.clear();
        }
        
        // Remove the completed job from the queue at the running job index
        if (!m_queue.isEmpty()) {
            const int removeIndex = (m_runningJobIndex >= 0 && m_runningJobIndex < m_queue.size()) ? m_runningJobIndex : 0;
            m_queue.remove(removeIndex);
            
            // Reset running job index after completion
            if (m_runningJobIndex >= 0) {
                m_runningJobIndex = -1;
                Q_EMIT runningJobIndexChanged();
            }
            
            Q_EMIT internalQueueChanged();
            
            // If internal queue is running and there are more jobs, start the next one
            if (m_internalQueueRunning && !m_queue.isEmpty() && !m_cjobEnabled) {
                qCInfo(cjobClient) << "Internal queue running - starting next job";
                QMetaObject::invokeMethod(this, [this]() {
                    launchNextFromQueue();
                }, Qt::QueuedConnection);
            }
        } else {
            // Queue is empty after removal - reset running index
            if (m_runningJobIndex >= 0) {
                m_runningJobIndex = -1;
                Q_EMIT runningJobIndexChanged();
            }
        }
    }
    updateProgress();
}

void StitchController::handleProcessOutput(ProcessState *state, QProcess::ProcessChannel channel)
{
    if (!state || !state->process) {
        return;
    }

    QByteArray &buffer = channel == QProcess::StandardOutput ? state->standardOutputBuffer : state->standardErrorBuffer;
    buffer += channel == QProcess::StandardOutput ? state->process->readAllStandardOutput() : state->process->readAllStandardError();

    qsizetype newline = buffer.indexOf('\n');
    while (newline >= 0) {
        const QByteArray line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        processOutputLine(state, QString::fromLocal8Bit(line), channel);
        newline = buffer.indexOf('\n');
    }
}

void StitchController::processOutputLine(ProcessState *state, const QString &line, QProcess::ProcessChannel channel)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    const QStringList parts = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (trimmed.startsWith(QStringLiteral("CriticalError "))) {
        const QString message = trimmed.mid(QStringLiteral("CriticalError ").size()).trimmed();
        appendLog(message);
        state->failed = true;
        return;
    }
    if (trimmed.startsWith(QStringLiteral("VerificationWarning "))) {
        const QString message = trimmed.mid(QStringLiteral("VerificationWarning ").size()).trimmed();
        appendLog(QStringLiteral("Warning: %1").arg(message));
        return;
    }
    if (channel == QProcess::StandardOutput && parts.size() >= 4 && parts.at(0) == QStringLiteral("Progress")) {
        bool completedOk = false;
        bool totalOk = false;
        const int completed = parts.at(1).toInt(&completedOk);
        const int total = parts.at(2).toInt(&totalOk);
        if (completedOk && totalOk) {
            state->completedFrames = std::clamp(completed, 0, total);
            state->totalFrames = std::max(state->totalFrames, total);
            updateProgress();
            return;
        }
    }

    appendLog(QStringLiteral("[%1] %2").arg(state->launch.activity, trimmed));
}

void StitchController::resetProgress(int totalFrames)
{
    m_finishedFrames = 0;
    m_progressCompleted = 0;
    m_progressTotal = totalFrames;
    m_processFailure = false;
    m_elapsedTimer.restart();
    m_progressTimer.start();
    Q_EMIT progressChanged();
}

void StitchController::updateProgress()
{
    int activeFrames = 0;
    int activeTotalFrames = 0;
    for (const ProcessState *state : m_activeProcesses) {
        activeFrames += state->completedFrames;
        activeTotalFrames += state->totalFrames;
    }
    if (activeTotalFrames > 0) {
        m_progressTotal = std::max(m_progressTotal, m_finishedFrames + activeTotalFrames);
    }
    m_progressCompleted = std::clamp(m_finishedFrames + activeFrames, 0, std::max(0, m_progressTotal));
    Q_EMIT progressChanged();
}

QString StitchController::formattedElapsedTime() const
{
    if (!m_elapsedTimer.isValid()) {
        return QStringLiteral("00:00:00");
    }
    return formatDuration(m_elapsedTimer.elapsed());
}

QString StitchController::formattedRemainingTime() const
{
    if (!running()) {
        return QStringLiteral("00:00:00");
    }
    if (m_progressCompleted <= 0 || m_progressTotal <= 0) {
        return QStringLiteral("Calculating...");
    }
    const qint64 elapsed = m_elapsedTimer.elapsed();
    const double millisecondsPerFrame = static_cast<double>(elapsed) / static_cast<double>(m_progressCompleted);
    const int remainingFrames = std::max(0, m_progressTotal - m_progressCompleted);
    return formatDuration(static_cast<qint64>(millisecondsPerFrame * remainingFrames));
}

void StitchController::verifyInputs()
{
    startNodeProcesses(buildLaunches(true));
}

void StitchController::launchStitch()
{
    startNodeProcesses(buildLaunches(false));
}

void StitchController::abortStitch()
{
    if (!running()) {
        return;
    }
    
    // Stop progress timer immediately to prevent further updates
    m_progressTimer.stop();
    
    appendLog(QStringLiteral("Aborting active C-Stitch node process(es)."));
    for (ProcessState *state : m_activeProcesses) {
        if (state && state->process && state->process->state() != QProcess::NotRunning) {
            // Use kill() instead of terminate() to force immediate termination
            // terminate() sends SIGTERM which allows graceful shutdown
            // kill() sends SIGKILL which forces immediate process death
            state->process->kill();
        }
    }
    
    // Reset progress state immediately so UI updates right away
    m_finishedFrames = 0;
    m_progressCompleted = 0;
    m_progressTotal = 0;
    m_processFailure = true;
    m_elapsedTimer.invalidate();
    Q_EMIT progressChanged();
    Q_EMIT runningChanged();
}

void StitchController::clearLog()
{
    m_logText.clear();
    Q_EMIT logTextChanged();
}

void StitchController::applyApplicationSettings()
{
    m_applyingApplicationSettings = true;
    setLeftFace(StitchSettings::leftFace());
    setRightFace(StitchSettings::rightFace());
    setTopFace(StitchSettings::topFace());
    setBottomFace(StitchSettings::bottomFace());
    setRightLeftFace(StitchSettings::rightLeftFace());
    setRightRightFace(StitchSettings::rightRightFace());
    setRightTopFace(StitchSettings::rightTopFace());
    setRightBottomFace(StitchSettings::rightBottomFace());
    const QString outputDirectory = normalizedPath(StitchSettings::outputDirectory());
    setOutputDirectory(outputDirectory.isEmpty() ? defaultOutputDirectory() : outputDirectory);
    // Right output directory is no longer used - paths are now constructed as OutputDirectory/OutputName/SubDir/Filename
    setOutputName(StitchSettings::outputName().isEmpty() ? QStringLiteral("stitched") : StitchSettings::outputName());
    setStereo(StitchSettings::stereo());
    setStereoEye(StitchSettings::stereoEye());
    setAlpha(StitchSettings::alpha());
    setFxaa(StitchSettings::fxaa());
    setCubic(StitchSettings::cubic());
    setAutoDetectCubemapFaces(StitchSettings::autoDetectCubemapFaces());
    setFisheyeFov(StitchSettings::fisheyeFov());
    setEyeSeparation(StitchSettings::eyeSeparation());
    setDomeDiameter(StitchSettings::domeDiameter());
    setResolution(StitchSettings::resolution());
    setCubemapResolution(StitchSettings::cubemapResolution());
    setMsaa(StitchSettings::msaa());
    setFormat(StitchSettings::format());
    setCompression(StitchSettings::compression());
    setQuality(StitchSettings::quality());
    setLoadingThreads(StitchSettings::loadingThreads());
    setEncodingThreads(StitchSettings::encodingThreads());
    setStartIndex(StitchSettings::startIndex());
    setStopIndex(StitchSettings::stopIndex());
    setOutputStartIndex(StitchSettings::outputStartIndex());
    setSteps(StitchSettings::steps());
    setCustomFrameList(StitchSettings::customFrameList());
    setLoadTextureAttemptsMax(StitchSettings::loadTextureAttemptsMax());
    setLoadTextureAttemptsSleep(StitchSettings::loadTextureAttemptsSleep());
    setLeftRotation(StitchSettings::leftRotation());
    setRightRotation(StitchSettings::rightRotation());
    setTopRotation(StitchSettings::topRotation());
    setBottomRotation(StitchSettings::bottomRotation());
    m_applyingApplicationSettings = false;
    if (!firstSequencePath().isEmpty()) {
        indexSequence(false);
    }
}

void StitchController::appendLog(const QString &message)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (!m_logText.isEmpty()) {
        m_logText += QLatin1Char('\n');
    }
    m_logText += trimmed;
    Q_EMIT logTextChanged();
}

void StitchController::emitCommandSignals()
{
    Q_EMIT commandLinePreviewChanged();
}

// ============= C-Job Implementation =============

void StitchController::connectToCJobServer()
{
    if (m_cjobSocket && m_cjobSocket->isOpen()) {
        return;  // Already connected
    }

    m_cjobSocket = new QLocalSocket(this);
    connect(m_cjobSocket, &QLocalSocket::connected, this, [this]() {
        qCInfo(cjobClient) << "Connected to C-Job server";
        sendRegisterInstance();
        if (!m_cjobEnabled) {
            return;
        }
        for (const JobInfo &job : m_queue) {
            submitToCJob(job.jobId);
        }
    });
    connect(m_cjobSocket, &QLocalSocket::disconnected, this, [this]() {
        qCInfo(cjobClient) << "Disconnected from C-Job server";
        m_cjobBuffer.clear();
    });
    connect(m_cjobSocket, &QLocalSocket::readyRead, this, &StitchController::onCJobReadyRead);

    m_cjobSocket->connectToServer(QStringLiteral("C-Job"));
}

void StitchController::disconnectFromCJobServer()
{
    if (m_cjobSocket) {
        m_cjobSocket->close();
        delete m_cjobSocket;
        m_cjobSocket = nullptr;
    }
    
    // Clear current job ID when disconnecting
    m_currentCJobJobId.clear();
}

void StitchController::sendRegisterInstance()
{
    if (!m_cjobSocket || !m_cjobSocket->isOpen()) {
        return;
    }

    QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("register_instance");
    json[QStringLiteral("instance_id")] = QString(QCoreApplication::applicationName() + QLatin1Char('-') + QString::number(QRandomGenerator::global()->generate()));
    sendJsonToCJob(json);
}

void StitchController::sendJsonToCJob(const QJsonObject &json)
{
    if (!m_cjobSocket || !m_cjobSocket->isOpen()) {
        return;
    }

    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    data.append('\n');
    m_cjobSocket->write(data);
}

void StitchController::onCJobReadyRead()
{
    while (m_cjobSocket && m_cjobSocket->bytesAvailable() > 0) {
        QByteArray data = m_cjobSocket->readAll();
        m_cjobBuffer += QString::fromUtf8(data);

        int newlinePos;
        while ((newlinePos = m_cjobBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
            QString message = m_cjobBuffer.left(newlinePos);
            m_cjobBuffer = m_cjobBuffer.mid(newlinePos + 1);

            if (!message.trimmed().isEmpty()) {
                processCJobMessage(message);
            }
        }
    }
}

void StitchController::processCJobMessage(const QString &message)
{
    qCDebug(cjobClient) << "Received from C-Job:" << message;

    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        qCWarning(cjobClient) << "Invalid JSON message";
        return;
    }

    QJsonObject obj = doc.object();
    QString type = obj[QStringLiteral("type")].toString();

    if (type == QLatin1String("launch_job")) {
        QString jobId = obj[QStringLiteral("job_id")].toString();
        if (!jobId.isEmpty()) {
            qCInfo(cjobClient) << "Received launch command from C-Job for job:" << jobId;
            
            // Find the matching job in the queue by ID and set runningJobIndex so UI updates correctly
            int runningIndex = -1;
            for (int i = 0; i < m_queue.size(); ++i) {
                if (m_queue[i].jobId == jobId) {
                    runningIndex = i;
                    break;
                }
            }
            
            // Store the current job ID and set the running index so UI updates correctly
            m_currentCJobJobId = jobId;
            m_runningJobIndex = runningIndex;
            Q_EMIT runningJobIndexChanged();
            
            // Retrieve stored parameters by job ID and apply them before launching
            JobInfo storedParams = retrieveStoredJob(jobId);
            if (!storedParams.jobId.isEmpty()) {
                qCInfo(cjobClient) << "Retrieved stored params for job:" << storedParams.jobName;
                
                // Temporarily store current settings to restore after launch
                QString origLeftFace = m_leftFace;
                QString origRightFace = m_rightFace;
                QString origTopFace = m_topFace;
                QString origBottomFace = m_bottomFace;
                QString origRightLeftFace = m_rightLeftFace;
                QString origRightRightFace = m_rightRightFace;
                QString origRightTopFace = m_rightTopFace;
                QString origRightBottomFace = m_rightBottomFace;
                QString origOutputDirectory = m_outputDirectory;
                QString origOutputName = m_outputName;
                bool origStereo = m_stereo;
                QString origStereoEye = m_stereoEye;
                bool origAlpha = m_alpha;
                bool origFxaa = m_fxaa;
                bool origCubic = m_cubic;
                double origFisheyeFov = m_fisheyeFov;
                double origEyeSeparation = m_eyeSeparation;
                double origDomeDiameter = m_domeDiameter;
                int origResolution = m_resolution;
                int origCubemapResolution = m_cubemapResolution;
                int origMsaa = m_msaa;
                QString origFormat = m_format;
                int origCompression = m_compression;
                int origQuality = m_quality;
                int origLoadingThreads = m_loadingThreads;
                int origEncodingThreads = m_encodingThreads;
                int origStartIndex = m_startIndex;
                int origStopIndex = m_stopIndex;
                int origOutputStartIndex = m_outputStartIndex;
                int origSteps = m_steps;
                QString origCustomFrameList = m_customFrameList;
                int origLoadTextureAttemptsMax = m_loadTextureAttemptsMax;
                int origLoadTextureAttemptsSleep = m_loadTextureAttemptsSleep;
                int origLeftRotation = m_leftRotation;
                int origRightRotation = m_rightRotation;
                int origTopRotation = m_topRotation;
                int origBottomRotation = m_bottomRotation;

                // Apply stored job parameters
                applyJobInfo(storedParams);

                // Launch the stitch with stored job parameters
                launchStitch();

                // Restore original settings after launch (queued to avoid signal conflicts)
                QMetaObject::invokeMethod(this, [this, origLeftFace, origRightFace, origTopFace, origBottomFace,
                    origRightLeftFace, origRightRightFace, origRightTopFace, origRightBottomFace,
                    origOutputDirectory, origOutputName, origStereo, origStereoEye, origAlpha, origFxaa, origCubic,
                    origFisheyeFov, origEyeSeparation, origDomeDiameter, origResolution, origCubemapResolution, origMsaa,
                    origFormat, origCompression, origQuality, origLoadingThreads, origEncodingThreads,
                    origStartIndex, origStopIndex, origOutputStartIndex, origSteps, origCustomFrameList,
                    origLoadTextureAttemptsMax, origLoadTextureAttemptsSleep, origLeftRotation, origRightRotation,
                    origTopRotation, origBottomRotation]() {
                    setLeftFace(origLeftFace);
                    setRightFace(origRightFace);
                    setTopFace(origTopFace);
                    setBottomFace(origBottomFace);
                    setRightLeftFace(origRightLeftFace);
                    setRightRightFace(origRightRightFace);
                    setRightTopFace(origRightTopFace);
                    setRightBottomFace(origRightBottomFace);
                    setOutputDirectory(origOutputDirectory);
                    setOutputName(origOutputName);
                    setStereo(origStereo);
                    setStereoEye(origStereoEye);
                    setAlpha(origAlpha);
                    setFxaa(origFxaa);
                    setCubic(origCubic);
                    setFisheyeFov(origFisheyeFov);
                    setEyeSeparation(origEyeSeparation);
                    setDomeDiameter(origDomeDiameter);
                    setResolution(origResolution);
                    setCubemapResolution(origCubemapResolution);
                    setMsaa(origMsaa);
                    setFormat(origFormat);
                    setCompression(origCompression);
                    setQuality(origQuality);
                    setLoadingThreads(origLoadingThreads);
                    setEncodingThreads(origEncodingThreads);
                    setStartIndex(origStartIndex);
                    setStopIndex(origStopIndex);
                    setOutputStartIndex(origOutputStartIndex);
                    setSteps(origSteps);
                    setCustomFrameList(origCustomFrameList);
                    setLoadTextureAttemptsMax(origLoadTextureAttemptsMax);
                    setLoadTextureAttemptsSleep(origLoadTextureAttemptsSleep);
                    setLeftRotation(origLeftRotation);
                    setRightRotation(origRightRotation);
                    setTopRotation(origTopRotation);
                    setBottomRotation(origBottomRotation);
                }, Qt::QueuedConnection);
            } else {
                qCWarning(cjobClient) << "No stored parameters found for job:" << jobId << "- launching with current settings";
                launchStitch();
            }
        }
    } else if (type == QLatin1String("job_complete")) {
        QString jobId = obj[QStringLiteral("job_id")].toString();
        qCInfo(cjobClient) << "Job completed in C-Job:" << jobId;
        
        // Clean up stored job parameters after completion
        m_storedJobs.remove(jobId);
    }
}

void StitchController::setCJobEnabled(bool enabled)
{
    if (m_cjobEnabled == enabled) {
        return;
    }

    m_cjobEnabled = enabled;

    if (m_cjobEnabled) {
        cancelEditingInternalQueueJob();
        connectToCJobServer();
    } else {
        disconnectFromCJobServer();
        // Clear current job ID when disabling C-Job
        m_currentCJobJobId.clear();
    }

    Q_EMIT cjobEnabledChanged();
}

int StitchController::queueSize() const
{
    return m_queue.size();
}

QStringList StitchController::queuedJobs() const
{
    QStringList jobs;
    for (const JobInfo &info : m_queue) {
        jobs << info.jobId;
    }
    return jobs;
}

bool StitchController::isCJobConnected() const
{
    return m_cjobSocket && m_cjobSocket->isOpen();
}

QString StitchController::generateJobName() const
{
    // Use the output name (filename stem) as the primary identifier
    QString jobStem = m_outputName.trimmed().isEmpty() ? QStringLiteral("stitched") : m_outputName;
    
    const int frameCount = expectedFrameCount();
    return QString(QStringLiteral("%1 - %2 frames @ %3x%4"))
        .arg(jobStem)
        .arg(frameCount)
        .arg(m_cubemapResolution)
        .arg(m_cubemapResolution);
}

StitchController::JobInfo StitchController::currentJobInfo() const
{
    JobInfo info;
    info.jobName = generateJobName();
    info.progressTotal = expectedFrameCount();
    info.leftFace = m_leftFace;
    info.rightFace = m_rightFace;
    info.topFace = m_topFace;
    info.bottomFace = m_bottomFace;
    info.rightLeftFace = m_rightLeftFace;
    info.rightRightFace = m_rightRightFace;
    info.rightTopFace = m_rightTopFace;
    info.rightBottomFace = m_rightBottomFace;
    info.outputDirectory = m_outputDirectory;
    info.outputName = m_outputName;
    info.stereo = m_stereo;
    info.stereoEye = m_stereoEye;
    info.alpha = m_alpha;
    info.fxaa = m_fxaa;
    info.cubic = m_cubic;
    info.fisheyeFov = m_fisheyeFov;
    info.eyeSeparation = m_eyeSeparation;
    info.domeDiameter = m_domeDiameter;
    info.resolution = m_resolution;
    info.cubemapResolution = m_cubemapResolution;
    info.msaa = m_msaa;
    info.format = m_format;
    info.compression = m_compression;
    info.quality = m_quality;
    info.loadingThreads = m_loadingThreads;
    info.encodingThreads = m_encodingThreads;
    info.startIndex = m_startIndex;
    info.stopIndex = m_stopIndex;
    info.outputStartIndex = m_outputStartIndex;
    info.steps = m_steps;
    info.customFrameList = m_customFrameList;
    info.loadTextureAttemptsMax = m_loadTextureAttemptsMax;
    info.loadTextureAttemptsSleep = m_loadTextureAttemptsSleep;
    info.leftRotation = m_leftRotation;
    info.rightRotation = m_rightRotation;
    info.topRotation = m_topRotation;
    info.bottomRotation = m_bottomRotation;
    return info;
}

QJsonObject StitchController::jobInfoToJson(const JobInfo &info)
{
    QJsonObject json;
    json[QStringLiteral("jobId")] = info.jobId;
    json[QStringLiteral("instanceId")] = info.instanceId;
    json[QStringLiteral("jobName")] = info.jobName;
    json[QStringLiteral("leftFace")] = info.leftFace;
    json[QStringLiteral("rightFace")] = info.rightFace;
    json[QStringLiteral("topFace")] = info.topFace;
    json[QStringLiteral("bottomFace")] = info.bottomFace;
    json[QStringLiteral("rightLeftFace")] = info.rightLeftFace;
    json[QStringLiteral("rightRightFace")] = info.rightRightFace;
    json[QStringLiteral("rightTopFace")] = info.rightTopFace;
    json[QStringLiteral("rightBottomFace")] = info.rightBottomFace;
    json[QStringLiteral("outputDirectory")] = info.outputDirectory;
    json[QStringLiteral("outputName")] = info.outputName;
    json[QStringLiteral("stereo")] = info.stereo;
    json[QStringLiteral("stereoEye")] = info.stereoEye;
    json[QStringLiteral("alpha")] = info.alpha;
    json[QStringLiteral("fxaa")] = info.fxaa;
    json[QStringLiteral("cubic")] = info.cubic;
    json[QStringLiteral("fisheyeFov")] = info.fisheyeFov;
    json[QStringLiteral("eyeSeparation")] = info.eyeSeparation;
    json[QStringLiteral("domeDiameter")] = info.domeDiameter;
    json[QStringLiteral("resolution")] = info.resolution;
    json[QStringLiteral("cubemapResolution")] = info.cubemapResolution;
    json[QStringLiteral("msaa")] = info.msaa;
    json[QStringLiteral("format")] = info.format;
    json[QStringLiteral("compression")] = info.compression;
    json[QStringLiteral("quality")] = info.quality;
    json[QStringLiteral("loadingThreads")] = info.loadingThreads;
    json[QStringLiteral("encodingThreads")] = info.encodingThreads;
    json[QStringLiteral("startIndex")] = info.startIndex;
    json[QStringLiteral("stopIndex")] = info.stopIndex;
    json[QStringLiteral("outputStartIndex")] = info.outputStartIndex;
    json[QStringLiteral("steps")] = info.steps;
    json[QStringLiteral("customFrameList")] = info.customFrameList;
    json[QStringLiteral("loadTextureAttemptsMax")] = info.loadTextureAttemptsMax;
    json[QStringLiteral("loadTextureAttemptsSleep")] = info.loadTextureAttemptsSleep;
    json[QStringLiteral("leftRotation")] = info.leftRotation;
    json[QStringLiteral("rightRotation")] = info.rightRotation;
    json[QStringLiteral("topRotation")] = info.topRotation;
    json[QStringLiteral("bottomRotation")] = info.bottomRotation;
    return json;
}

bool StitchController::jobInfoFromJson(const QJsonObject &json, JobInfo *info, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    auto stringValue = [&json, &fail](const char *key, QString *value) {
        const QJsonValue jsonValue = json.value(QLatin1String(key));
        if (!jsonValue.isString()) {
            return fail(QStringLiteral("'%1' must be a string").arg(QLatin1String(key)));
        }
        *value = jsonValue.toString();
        return true;
    };
    auto boolValue = [&json, &fail](const char *key, bool *value) {
        const QJsonValue jsonValue = json.value(QLatin1String(key));
        if (!jsonValue.isBool()) {
            return fail(QStringLiteral("'%1' must be a boolean").arg(QLatin1String(key)));
        }
        *value = jsonValue.toBool();
        return true;
    };
    auto intValue = [&json, &fail](const char *key, int *value) {
        const QJsonValue jsonValue = json.value(QLatin1String(key));
        if (!jsonValue.isDouble() || !qIsFinite(jsonValue.toDouble()) || jsonValue.toInt() != jsonValue.toDouble()) {
            return fail(QStringLiteral("'%1' must be an integer").arg(QLatin1String(key)));
        }
        *value = jsonValue.toInt();
        return true;
    };
    auto doubleValue = [&json, &fail](const char *key, double *value) {
        const QJsonValue jsonValue = json.value(QLatin1String(key));
        if (!jsonValue.isDouble() || !qIsFinite(jsonValue.toDouble())) {
            return fail(QStringLiteral("'%1' must be a number").arg(QLatin1String(key)));
        }
        *value = jsonValue.toDouble();
        return true;
    };

    JobInfo parsed;
    if (!stringValue("jobId", &parsed.jobId) || !stringValue("instanceId", &parsed.instanceId)
        || !stringValue("jobName", &parsed.jobName)
        || !stringValue("leftFace", &parsed.leftFace) || !stringValue("rightFace", &parsed.rightFace)
        || !stringValue("topFace", &parsed.topFace) || !stringValue("bottomFace", &parsed.bottomFace)
        || !stringValue("rightLeftFace", &parsed.rightLeftFace) || !stringValue("rightRightFace", &parsed.rightRightFace)
        || !stringValue("rightTopFace", &parsed.rightTopFace) || !stringValue("rightBottomFace", &parsed.rightBottomFace)
        || !stringValue("outputDirectory", &parsed.outputDirectory) || !stringValue("outputName", &parsed.outputName)
        || !boolValue("stereo", &parsed.stereo) || !stringValue("stereoEye", &parsed.stereoEye)
        || !boolValue("alpha", &parsed.alpha) || !boolValue("fxaa", &parsed.fxaa) || !boolValue("cubic", &parsed.cubic)
        || !doubleValue("fisheyeFov", &parsed.fisheyeFov) || !doubleValue("eyeSeparation", &parsed.eyeSeparation)
        || !doubleValue("domeDiameter", &parsed.domeDiameter) || !intValue("resolution", &parsed.resolution)
        || !intValue("cubemapResolution", &parsed.cubemapResolution) || !intValue("msaa", &parsed.msaa)
        || !stringValue("format", &parsed.format) || !intValue("compression", &parsed.compression)
        || !intValue("quality", &parsed.quality) || !intValue("loadingThreads", &parsed.loadingThreads)
        || !intValue("encodingThreads", &parsed.encodingThreads) || !intValue("startIndex", &parsed.startIndex)
        || !intValue("stopIndex", &parsed.stopIndex) || !intValue("outputStartIndex", &parsed.outputStartIndex)
        || !intValue("steps", &parsed.steps) || !stringValue("customFrameList", &parsed.customFrameList)
        || !intValue("loadTextureAttemptsMax", &parsed.loadTextureAttemptsMax)
        || !intValue("loadTextureAttemptsSleep", &parsed.loadTextureAttemptsSleep)
        || !intValue("leftRotation", &parsed.leftRotation) || !intValue("rightRotation", &parsed.rightRotation)
        || !intValue("topRotation", &parsed.topRotation) || !intValue("bottomRotation", &parsed.bottomRotation)) {
        return false;
    }

    if (parsed.stereoEye != QStringLiteral("both") && parsed.stereoEye != QStringLiteral("left") && parsed.stereoEye != QStringLiteral("right")) {
        return fail(QStringLiteral("'stereoEye' must be 'both', 'left', or 'right'"));
    }
    if (normalizedFormat(parsed.format) != parsed.format || parsed.fisheyeFov < 1.0 || parsed.fisheyeFov > 360.0
        || parsed.eyeSeparation < 0.0 || parsed.eyeSeparation > 2.0 || parsed.domeDiameter <= 0.0
        || parsed.resolution <= 0 || parsed.cubemapResolution <= 0 || parsed.msaa < 0 || parsed.compression < 0
        || parsed.quality < 0 || parsed.quality > 100 || parsed.loadingThreads <= 0 || parsed.encodingThreads <= 0
        || parsed.steps <= 0 || parsed.loadTextureAttemptsMax < 0 || parsed.loadTextureAttemptsSleep < 0
        || clampedRotation(parsed.leftRotation) != parsed.leftRotation || clampedRotation(parsed.rightRotation) != parsed.rightRotation
        || clampedRotation(parsed.topRotation) != parsed.topRotation || clampedRotation(parsed.bottomRotation) != parsed.bottomRotation) {
        return fail(QStringLiteral("job contains an unsupported setting value"));
    }

    *info = parsed;
    return true;
}

void StitchController::applyJobInfo(const JobInfo &info)
{
    setLeftFace(info.leftFace);
    setRightFace(info.rightFace);
    setTopFace(info.topFace);
    setBottomFace(info.bottomFace);
    setRightLeftFace(info.rightLeftFace);
    setRightRightFace(info.rightRightFace);
    setRightTopFace(info.rightTopFace);
    setRightBottomFace(info.rightBottomFace);
    setOutputDirectory(info.outputDirectory);
    setOutputName(info.outputName);
    setStereo(info.stereo);
    setStereoEye(info.stereoEye);
    setAlpha(info.alpha);
    setFxaa(info.fxaa);
    setCubic(info.cubic);
    setFisheyeFov(info.fisheyeFov);
    setEyeSeparation(info.eyeSeparation);
    setDomeDiameter(info.domeDiameter);
    setResolution(info.resolution);
    setCubemapResolution(info.cubemapResolution);
    setMsaa(info.msaa);
    setFormat(info.format);
    setCompression(info.compression);
    setQuality(info.quality);
    setLoadingThreads(info.loadingThreads);
    setEncodingThreads(info.encodingThreads);
    setStartIndex(info.startIndex);
    setStopIndex(info.stopIndex);
    setOutputStartIndex(info.outputStartIndex);
    setSteps(info.steps);
    setCustomFrameList(info.customFrameList);
    setLoadTextureAttemptsMax(info.loadTextureAttemptsMax);
    setLoadTextureAttemptsSleep(info.loadTextureAttemptsSleep);
    setLeftRotation(info.leftRotation);
    setRightRotation(info.rightRotation);
    setTopRotation(info.topRotation);
    setBottomRotation(info.bottomRotation);
}

bool StitchController::canEditInternalQueue() const
{
    return !m_cjobEnabled && !running();
}

bool StitchController::saveInternalQueue(const QString &path)
{
    const QString filePath = normalizedPath(path);
    if (filePath.isEmpty()) {
        appendLog(QStringLiteral("Cannot save queue: no file path was selected."));
        return false;
    }
    if (!canEditInternalQueue() || m_editingInternalQueueIndex >= 0) {
        appendLog(QStringLiteral("Cannot save queue while stitching, editing a job, or using C-Job."));
        return false;
    }

    QJsonArray jobs;
    for (const JobInfo &job : m_queue) {
        jobs.append(jobInfoToJson(job));
    }

    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("c-stitch-internal-queue");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("jobs")] = jobs;

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        appendLog(QStringLiteral("Cannot save queue to '%1': %2").arg(filePath, file.errorString()));
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        appendLog(QStringLiteral("Cannot write queue to '%1': %2").arg(filePath, file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        appendLog(QStringLiteral("Cannot finish saving queue to '%1': %2").arg(filePath, file.errorString()));
        return false;
    }

    appendLog(QStringLiteral("Saved %1 queue job(s) to '%2'.").arg(m_queue.size()).arg(filePath));
    return true;
}

bool StitchController::loadInternalQueue(const QString &path)
{
    const QString filePath = normalizedPath(path);
    if (filePath.isEmpty()) {
        appendLog(QStringLiteral("Cannot load queue: no file path was selected."));
        return false;
    }
    if (!canEditInternalQueue() || m_editingInternalQueueIndex >= 0) {
        appendLog(QStringLiteral("Cannot load queue while stitching, editing a job, or using C-Job."));
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        appendLog(QStringLiteral("Cannot open queue file '%1': %2").arg(filePath, file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        appendLog(QStringLiteral("Cannot load queue file '%1': invalid JSON (%2).").arg(filePath, parseError.errorString()));
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("c-stitch-internal-queue")
        || root.value(QStringLiteral("version")).toInt(-1) != 1 || !root.value(QStringLiteral("jobs")).isArray()) {
        appendLog(QStringLiteral("Cannot load queue file '%1': unsupported queue file format or version.").arg(filePath));
        return false;
    }

    QVector<JobInfo> loadedQueue;
    QSet<QString> jobIds;
    const QJsonArray jobs = root.value(QStringLiteral("jobs")).toArray();
    loadedQueue.reserve(jobs.size());
    for (qsizetype index = 0; index < jobs.size(); ++index) {
        if (!jobs.at(index).isObject()) {
            appendLog(QStringLiteral("Cannot load queue file '%1': job %2 is not an object.").arg(filePath).arg(index + 1));
            return false;
        }
        JobInfo job;
        QString error;
        if (!jobInfoFromJson(jobs.at(index).toObject(), &job, &error)) {
            appendLog(QStringLiteral("Cannot load queue file '%1': job %2 %3.").arg(filePath).arg(index + 1, 0).arg(error));
            return false;
        }
        if (job.jobId.isEmpty() || jobIds.contains(job.jobId)) {
            job.jobId = QStringLiteral("job-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')));
        }
        jobIds.insert(job.jobId);
        job.status = QStringLiteral("Pending");
        job.progressCompleted = 0;
        job.progressTotal = 0;
        loadedQueue.append(job);
    }

    m_queue = std::move(loadedQueue);
    m_storedJobs.clear();
    for (const JobInfo &job : m_queue) {
        m_storedJobs.insert(job.jobId, job);
    }
    const bool internalQueueWasRunning = m_internalQueueRunning;
    const bool runningJobChanged = m_runningJobIndex != -1;
    m_internalQueueRunning = false;
    m_runningJobIndex = -1;
    m_editingInternalQueueIndex = -1;

    if (internalQueueWasRunning) {
        Q_EMIT internalQueueRunningChanged();
    }
    if (runningJobChanged) {
        Q_EMIT runningJobIndexChanged();
    }
    Q_EMIT editingInternalQueueIndexChanged();
    Q_EMIT internalQueueChanged();
    Q_EMIT queueChanged();
    appendLog(QStringLiteral("Loaded %1 queue job(s) from '%2'.").arg(m_queue.size()).arg(filePath));
    return true;
}

void StitchController::queueJob()
{
    // Generate a unique random job ID (UUID-style) for generic identification
    QString jobId = QStringLiteral("job-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')));

    // Generate descriptive name from current settings
    QString jobName = generateJobName();

    // Create full JobInfo with all parameters and store locally keyed by job ID
    JobInfo info = currentJobInfo();
    info.jobId = jobId;
    info.jobName = jobName;
    info.instanceId = QCoreApplication::applicationName() + QLatin1Char('-') + QString::number(QRandomGenerator::global()->generate());
    info.status = QStringLiteral("Pending");
    info.progressCompleted = 0;

    // Store full job parameters locally keyed by unique job ID
    // This allows retrieval when C-Job sends a launch command with just the job ID
    m_storedJobs[jobId] = info;

    // Add to internal queue (display only, not used for parameter storage)
    m_queue.append(info);

    qCInfo(cjobClient) << "Job queued:" << jobName << "(ID:" << jobId << ", total frames:" << info.progressTotal << ")";

    Q_EMIT internalQueueChanged();
    Q_EMIT queueChanged();
    Q_EMIT progressChanged();  // For UI updates

    // If C-Job is enabled, submit to server (only job ID + name)
    if (m_cjobEnabled && m_cjobSocket && m_cjobSocket->isOpen()) {
        submitToCJob(jobId);
    }
}

void StitchController::reorderInternalQueue(int start, int destination)
{
    if (start < 0 || start >= m_queue.size() || destination < 0 || destination >= m_queue.size() || start == destination) {
        return;
    }

    // Move the job from 'start' position to 'destination' position in internal queue
    const JobInfo job = m_queue.takeAt(start);
    const int insertPos = qMin(destination, m_queue.size());
    m_queue.insert(insertPos, job);

    qCDebug(cjobClient) << "Reordered internal queue: moved item" << start << "to" << destination;
    
    // Emit both signals to ensure QML ListView updates properly
    Q_EMIT internalQueueChanged();
}

void StitchController::removeJobFromInternalQueue(int index)
{
    if (index < 0 || index >= m_queue.size()) {
        return;
    }

    // Only allow removing from queue if not currently running
    if (running()) {
        qCWarning(cjobClient) << "Cannot remove job while a stitch is running";
        return;
    }

    const JobInfo removedJob = m_queue.takeAt(index);

    if (m_editingInternalQueueIndex == index) {
        cancelEditingInternalQueueJob();
    } else if (m_editingInternalQueueIndex > index) {
        --m_editingInternalQueueIndex;
        Q_EMIT editingInternalQueueIndexChanged();
    }
    
    // Also remove stored parameters for this job
    m_storedJobs.remove(removedJob.jobId);
    
    qCInfo(cjobClient) << "Removed job from internal queue at index" << index << "(ID:" << removedJob.jobId << ")";
    
    Q_EMIT internalQueueChanged();
}

bool StitchController::editInternalQueueJob(int index)
{
    if (!canEditInternalQueue() || index < 0 || index >= m_queue.size()) {
        return false;
    }

    applyJobInfo(m_queue.at(index));
    if (m_editingInternalQueueIndex != index) {
        m_editingInternalQueueIndex = index;
        Q_EMIT editingInternalQueueIndexChanged();
    }
    return true;
}

bool StitchController::saveEditedInternalQueueJob()
{
    if (!canEditInternalQueue() || m_editingInternalQueueIndex < 0 || m_editingInternalQueueIndex >= m_queue.size()) {
        return false;
    }

    const JobInfo previous = m_queue.at(m_editingInternalQueueIndex);
    JobInfo updated = currentJobInfo();
    updated.jobId = previous.jobId;
    updated.instanceId = previous.instanceId;
    updated.status = previous.status;
    updated.progressCompleted = previous.progressCompleted;
    m_queue[m_editingInternalQueueIndex] = updated;
    m_storedJobs[updated.jobId] = updated;

    cancelEditingInternalQueueJob();
    Q_EMIT internalQueueChanged();
    Q_EMIT queueChanged();
    return true;
}

void StitchController::cancelEditingInternalQueueJob()
{
    if (m_editingInternalQueueIndex < 0) {
        return;
    }

    m_editingInternalQueueIndex = -1;
    Q_EMIT editingInternalQueueIndexChanged();
}

bool StitchController::submitToCJob(const QString &jobId)
{
    if (!m_cjobSocket || !m_cjobSocket->isOpen()) {
        qCWarning(cjobClient) << "Cannot submit to C-Job: not connected";
        return false;
    }

    QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("submit_job");
    json[QStringLiteral("instance_id")] = QString(QCoreApplication::applicationName() + QLatin1Char('-') + QString::number(QRandomGenerator::global()->generate()));
    json[QStringLiteral("job_id")] = jobId;
    const JobInfo job = m_storedJobs.value(jobId);
    json[QStringLiteral("job_name")] = job.jobName.isEmpty() ? generateJobName() : job.jobName;

    sendJsonToCJob(json);
    qCInfo(cjobClient) << "Submitted job to C-Job:" << jobId << "(" << json.value(QStringLiteral("job_name")).toString() << ")";
    return true;
}

void StitchController::launchNextFromQueue()
{
    if (m_queue.isEmpty()) {
        qCWarning(cjobClient) << "No jobs in queue";
        return;
    }

    // Get the first job from queue but keep it in the queue for status tracking
    JobInfo info = m_queue.first();

    // Temporarily store current settings
    QString origLeftFace = m_leftFace;
    QString origRightFace = m_rightFace;
    QString origTopFace = m_topFace;
    QString origBottomFace = m_bottomFace;
    QString origRightLeftFace = m_rightLeftFace;
    QString origRightRightFace = m_rightRightFace;
    QString origRightTopFace = m_rightTopFace;
    QString origRightBottomFace = m_rightBottomFace;
    QString origOutputDirectory = m_outputDirectory;
    QString origOutputName = m_outputName;
    bool origStereo = m_stereo;
    QString origStereoEye = m_stereoEye;
    bool origAlpha = m_alpha;
    bool origFxaa = m_fxaa;
    bool origCubic = m_cubic;
    double origFisheyeFov = m_fisheyeFov;
    double origEyeSeparation = m_eyeSeparation;
    double origDomeDiameter = m_domeDiameter;
    int origResolution = m_resolution;
    int origCubemapResolution = m_cubemapResolution;
    int origMsaa = m_msaa;
    QString origFormat = m_format;
    int origCompression = m_compression;
    int origQuality = m_quality;
    int origLoadingThreads = m_loadingThreads;
    int origEncodingThreads = m_encodingThreads;
    int origStartIndex = m_startIndex;
    int origStopIndex = m_stopIndex;
    int origOutputStartIndex = m_outputStartIndex;
    int origSteps = m_steps;
    QString origCustomFrameList = m_customFrameList;
    int origLoadTextureAttemptsMax = m_loadTextureAttemptsMax;
    int origLoadTextureAttemptsSleep = m_loadTextureAttemptsSleep;
    int origLeftRotation = m_leftRotation;
    int origRightRotation = m_rightRotation;
    int origTopRotation = m_topRotation;
    int origBottomRotation = m_bottomRotation;

    // Apply job parameters
    applyJobInfo(info);

    qCInfo(cjobClient) << "Launching queued job:" << info.jobId;

    // Track which index is running (always 0 for next-from-queue)
    m_runningJobIndex = 0;
    Q_EMIT runningJobIndexChanged();

    // Launch the stitch with job parameters
    launchStitch();

    // Restore original settings after launch
    QMetaObject::invokeMethod(this, [this, origLeftFace, origRightFace, origTopFace, origBottomFace,
        origRightLeftFace, origRightRightFace, origRightTopFace, origRightBottomFace,
        origOutputDirectory, origOutputName, origStereo, origStereoEye, origAlpha, origFxaa, origCubic,
        origFisheyeFov, origEyeSeparation, origDomeDiameter, origResolution, origCubemapResolution, origMsaa,
        origFormat, origCompression, origQuality, origLoadingThreads, origEncodingThreads,
        origStartIndex, origStopIndex, origOutputStartIndex, origSteps, origCustomFrameList,
        origLoadTextureAttemptsMax, origLoadTextureAttemptsSleep, origLeftRotation, origRightRotation,
        origTopRotation, origBottomRotation]() {
        setLeftFace(origLeftFace);
        setRightFace(origRightFace);
        setTopFace(origTopFace);
        setBottomFace(origBottomFace);
        setRightLeftFace(origRightLeftFace);
        setRightRightFace(origRightRightFace);
        setRightTopFace(origRightTopFace);
        setRightBottomFace(origRightBottomFace);
        setOutputDirectory(origOutputDirectory);
        setOutputName(origOutputName);
        setStereo(origStereo);
        setStereoEye(origStereoEye);
        setAlpha(origAlpha);
        setFxaa(origFxaa);
        setCubic(origCubic);
        setFisheyeFov(origFisheyeFov);
        setEyeSeparation(origEyeSeparation);
        setDomeDiameter(origDomeDiameter);
        setResolution(origResolution);
        setCubemapResolution(origCubemapResolution);
        setMsaa(origMsaa);
        setFormat(origFormat);
        setCompression(origCompression);
        setQuality(origQuality);
        setLoadingThreads(origLoadingThreads);
        setEncodingThreads(origEncodingThreads);
        setStartIndex(origStartIndex);
        setStopIndex(origStopIndex);
        setOutputStartIndex(origOutputStartIndex);
        setSteps(origSteps);
        setCustomFrameList(origCustomFrameList);
        setLoadTextureAttemptsMax(origLoadTextureAttemptsMax);
        setLoadTextureAttemptsSleep(origLoadTextureAttemptsSleep);
        setLeftRotation(origLeftRotation);
        setRightRotation(origRightRotation);
        setTopRotation(origTopRotation);
        setBottomRotation(origBottomRotation);
    }, Qt::QueuedConnection);

    Q_EMIT queueChanged();
    Q_EMIT internalQueueChanged();  // Also notify internal queue changes for UI updates
}

void StitchController::setInternalQueueRunning(bool running)
{
    if (m_internalQueueRunning == running) {
        return;
    }

    m_internalQueueRunning = running;
    
    qCInfo(cjobClient) << "Internal queue running state changed to:" << running;
    appendLog(QStringLiteral("Internal queue %1").arg(running ? QStringLiteral("started") : QStringLiteral("paused")));

    Q_EMIT internalQueueRunningChanged();

    // If starting the queue and no job is processing, try to start next
    if (running && !m_queue.isEmpty() && !this->running()) {
        launchNextFromQueue();
    }
}

void StitchController::launchJobAtQueueIndex(int index)
{
    if (index < 0 || index >= m_queue.size()) {
        qCWarning(cjobClient) << "Invalid queue index:" << index;
        return;
    }

    if (running()) {
        qCWarning(cjobClient) << "Cannot launch job while a stitch is running";
        return;
    }

    // Get the job at the specified index
    JobInfo info = m_queue[index];

    // Temporarily store current settings to restore after launch
    QString origLeftFace = m_leftFace;
    QString origRightFace = m_rightFace;
    QString origTopFace = m_topFace;
    QString origBottomFace = m_bottomFace;
    QString origRightLeftFace = m_rightLeftFace;
    QString origRightRightFace = m_rightRightFace;
    QString origRightTopFace = m_rightTopFace;
    QString origRightBottomFace = m_rightBottomFace;
    QString origOutputDirectory = m_outputDirectory;
    QString origOutputName = m_outputName;
    bool origStereo = m_stereo;
    QString origStereoEye = m_stereoEye;
    bool origAlpha = m_alpha;
    bool origFxaa = m_fxaa;
    bool origCubic = m_cubic;
    double origFisheyeFov = m_fisheyeFov;
    double origEyeSeparation = m_eyeSeparation;
    double origDomeDiameter = m_domeDiameter;
    int origResolution = m_resolution;
    int origCubemapResolution = m_cubemapResolution;
    int origMsaa = m_msaa;
    QString origFormat = m_format;
    int origCompression = m_compression;
    int origQuality = m_quality;
    int origLoadingThreads = m_loadingThreads;
    int origEncodingThreads = m_encodingThreads;
    int origStartIndex = m_startIndex;
    int origStopIndex = m_stopIndex;
    int origOutputStartIndex = m_outputStartIndex;
    int origSteps = m_steps;
    QString origCustomFrameList = m_customFrameList;
    int origLoadTextureAttemptsMax = m_loadTextureAttemptsMax;
    int origLoadTextureAttemptsSleep = m_loadTextureAttemptsSleep;
    int origLeftRotation = m_leftRotation;
    int origRightRotation = m_rightRotation;
    int origTopRotation = m_topRotation;
    int origBottomRotation = m_bottomRotation;

    // Apply job parameters
    applyJobInfo(info);

    qCInfo(cjobClient) << "Launching queued job at index" << index << ":" << info.jobId;

    // Track which index is running
    m_runningJobIndex = index;
    Q_EMIT runningJobIndexChanged();

    // Launch the stitch with job parameters
    launchStitch();

    // Restore original settings after launch (queued to avoid signal conflicts)
    QMetaObject::invokeMethod(this, [this, origLeftFace, origRightFace, origTopFace, origBottomFace,
        origRightLeftFace, origRightRightFace, origRightTopFace, origRightBottomFace,
        origOutputDirectory, origOutputName, origStereo, origStereoEye, origAlpha, origFxaa, origCubic,
        origFisheyeFov, origEyeSeparation, origDomeDiameter, origResolution, origCubemapResolution, origMsaa,
        origFormat, origCompression, origQuality, origLoadingThreads, origEncodingThreads,
        origStartIndex, origStopIndex, origOutputStartIndex, origSteps, origCustomFrameList,
        origLoadTextureAttemptsMax, origLoadTextureAttemptsSleep, origLeftRotation, origRightRotation,
        origTopRotation, origBottomRotation]() {
        setLeftFace(origLeftFace);
        setRightFace(origRightFace);
        setTopFace(origTopFace);
        setBottomFace(origBottomFace);
        setRightLeftFace(origRightLeftFace);
        setRightRightFace(origRightRightFace);
        setRightTopFace(origRightTopFace);
        setRightBottomFace(origRightBottomFace);
        setOutputDirectory(origOutputDirectory);
        setOutputName(origOutputName);
        setStereo(origStereo);
        setStereoEye(origStereoEye);
        setAlpha(origAlpha);
        setFxaa(origFxaa);
        setCubic(origCubic);
        setFisheyeFov(origFisheyeFov);
        setEyeSeparation(origEyeSeparation);
        setDomeDiameter(origDomeDiameter);
        setResolution(origResolution);
        setCubemapResolution(origCubemapResolution);
        setMsaa(origMsaa);
        setFormat(origFormat);
        setCompression(origCompression);
        setQuality(origQuality);
        setLoadingThreads(origLoadingThreads);
        setEncodingThreads(origEncodingThreads);
        setStartIndex(origStartIndex);
        setStopIndex(origStopIndex);
        setOutputStartIndex(origOutputStartIndex);
        setSteps(origSteps);
        setCustomFrameList(origCustomFrameList);
        setLoadTextureAttemptsMax(origLoadTextureAttemptsMax);
        setLoadTextureAttemptsSleep(origLoadTextureAttemptsSleep);
        setLeftRotation(origLeftRotation);
        setRightRotation(origRightRotation);
        setTopRotation(origTopRotation);
        setBottomRotation(origBottomRotation);
    }, Qt::QueuedConnection);

    Q_EMIT queueChanged();
    Q_EMIT internalQueueChanged();
}

QStringList StitchController::internalQueuedJobs() const
{
    QStringList jobs;
    for (const JobInfo &info : m_queue) {
        // Use the pre-generated job name if available, otherwise generate from parameters
        QString displayName = info.jobName;
        if (displayName.isEmpty()) {
            QString stemFilename = m_outputName.trimmed().isEmpty() ? QStringLiteral("stitched") : m_outputName;
            displayName = QString(QStringLiteral("%1 - %2 frames @ %3x%4"))
                            .arg(stemFilename)
                            .arg(info.progressTotal)
                            .arg(m_cubemapResolution)
                            .arg(m_cubemapResolution);
        }
        jobs << displayName;
    }
    return jobs;
}

