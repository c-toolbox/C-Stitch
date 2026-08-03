/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSTITCH_STITCHCONTROLLER_H
#define CSTITCH_STITCHCONTROLLER_H

#include <QElapsedTimer>
#include <QMap>
#include <QObject>
#include <QLocalSocket>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

class StitchController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString leftFace READ leftFace WRITE setLeftFace NOTIFY faceInputsChanged)
    Q_PROPERTY(QString rightFace READ rightFace WRITE setRightFace NOTIFY faceInputsChanged)
    Q_PROPERTY(QString topFace READ topFace WRITE setTopFace NOTIFY faceInputsChanged)
    Q_PROPERTY(QString bottomFace READ bottomFace WRITE setBottomFace NOTIFY faceInputsChanged)
    Q_PROPERTY(QString rightLeftFace READ rightLeftFace WRITE setRightLeftFace NOTIFY faceInputsChanged)
    Q_PROPERTY(QString rightRightFace READ rightRightFace WRITE setRightRightFace NOTIFY faceInputsChanged)
    Q_PROPERTY(QString rightTopFace READ rightTopFace WRITE setRightTopFace NOTIFY faceInputsChanged)
    Q_PROPERTY(QString rightBottomFace READ rightBottomFace WRITE setRightBottomFace NOTIFY faceInputsChanged)
    Q_PROPERTY(QString outputDirectory READ outputDirectory WRITE setOutputDirectory NOTIFY outputChanged)
    Q_PROPERTY(QString rightOutputDirectory READ rightOutputDirectory WRITE setRightOutputDirectory NOTIFY outputChanged)
    Q_PROPERTY(QString outputName READ outputName WRITE setOutputName NOTIFY outputChanged)
    Q_PROPERTY(bool stereo READ stereo WRITE setStereo NOTIFY stitchSettingsChanged)
    Q_PROPERTY(QString stereoEye READ stereoEye WRITE setStereoEye NOTIFY stitchSettingsChanged)
    Q_PROPERTY(bool alpha READ alpha WRITE setAlpha NOTIFY stitchSettingsChanged)
    Q_PROPERTY(bool fxaa READ fxaa WRITE setFxaa NOTIFY stitchSettingsChanged)
    Q_PROPERTY(bool cubic READ cubic WRITE setCubic NOTIFY stitchSettingsChanged)
    Q_PROPERTY(bool autoDetectCubemapFaces READ autoDetectCubemapFaces WRITE setAutoDetectCubemapFaces NOTIFY stitchSettingsChanged)
    Q_PROPERTY(double fisheyeFov READ fisheyeFov WRITE setFisheyeFov NOTIFY stitchSettingsChanged)
    Q_PROPERTY(double eyeSeparation READ eyeSeparation WRITE setEyeSeparation NOTIFY stitchSettingsChanged)
    Q_PROPERTY(double domeDiameter READ domeDiameter WRITE setDomeDiameter NOTIFY stitchSettingsChanged)
    Q_PROPERTY(int resolution READ resolution WRITE setResolution NOTIFY stitchSettingsChanged)
    Q_PROPERTY(int cubemapResolution READ cubemapResolution WRITE setCubemapResolution NOTIFY stitchSettingsChanged)
    Q_PROPERTY(int msaa READ msaa WRITE setMsaa NOTIFY stitchSettingsChanged)
    Q_PROPERTY(QString format READ format WRITE setFormat NOTIFY encodingChanged)
    Q_PROPERTY(int compression READ compression WRITE setCompression NOTIFY encodingChanged)
    Q_PROPERTY(int quality READ quality WRITE setQuality NOTIFY encodingChanged)
    Q_PROPERTY(int loadingThreads READ loadingThreads WRITE setLoadingThreads NOTIFY encodingChanged)
    Q_PROPERTY(int encodingThreads READ encodingThreads WRITE setEncodingThreads NOTIFY encodingChanged)
    Q_PROPERTY(int startIndex READ startIndex WRITE setStartIndex NOTIFY sequenceChanged)
    Q_PROPERTY(int stopIndex READ stopIndex WRITE setStopIndex NOTIFY sequenceChanged)
    Q_PROPERTY(int outputStartIndex READ outputStartIndex WRITE setOutputStartIndex NOTIFY sequenceChanged)
    Q_PROPERTY(int steps READ steps WRITE setSteps NOTIFY sequenceChanged)
    Q_PROPERTY(QString customFrameList READ customFrameList WRITE setCustomFrameList NOTIFY sequenceChanged)
    Q_PROPERTY(int loadTextureAttemptsMax READ loadTextureAttemptsMax WRITE setLoadTextureAttemptsMax NOTIFY sequenceChanged)
    Q_PROPERTY(int loadTextureAttemptsSleep READ loadTextureAttemptsSleep WRITE setLoadTextureAttemptsSleep NOTIFY sequenceChanged)
    Q_PROPERTY(int leftRotation READ leftRotation WRITE setLeftRotation NOTIFY rotationsChanged)
    Q_PROPERTY(int rightRotation READ rightRotation WRITE setRightRotation NOTIFY rotationsChanged)
    Q_PROPERTY(int topRotation READ topRotation WRITE setTopRotation NOTIFY rotationsChanged)
    Q_PROPERTY(int bottomRotation READ bottomRotation WRITE setBottomRotation NOTIFY rotationsChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool sequenceIndexing READ sequenceIndexing NOTIFY sequenceStatusChanged)
    Q_PROPERTY(QString sequenceStatus READ sequenceStatus NOTIFY sequenceStatusChanged)
    Q_PROPERTY(int progressCompleted READ progressCompleted NOTIFY progressChanged)
    Q_PROPERTY(int progressTotal READ progressTotal NOTIFY progressChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressStatus READ progressStatus NOTIFY progressChanged)
    Q_PROPERTY(QString elapsedTime READ elapsedTime NOTIFY progressChanged)
    Q_PROPERTY(QString remainingTime READ remainingTime NOTIFY progressChanged)
    Q_PROPERTY(QString commandLinePreview READ commandLinePreview NOTIFY commandLinePreviewChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)

    // C-Job properties
    Q_PROPERTY(bool cjobEnabled READ isCJobEnabled WRITE setCJobEnabled NOTIFY cjobEnabledChanged)
    Q_PROPERTY(int queueSize READ queueSize NOTIFY queueChanged)
    Q_PROPERTY(QStringList queuedJobs READ queuedJobs NOTIFY queueChanged)

    // Internal queue properties (disabled when C-Job enabled)
    Q_PROPERTY(bool internalQueueRunning READ isInternalQueueRunning WRITE setInternalQueueRunning NOTIFY internalQueueRunningChanged)
    Q_PROPERTY(int internalQueueSize READ internalQueueSize NOTIFY internalQueueChanged)
    Q_PROPERTY(QStringList internalQueuedJobs READ internalQueuedJobs NOTIFY internalQueueChanged)
    Q_PROPERTY(int runningJobIndex READ runningJobIndex NOTIFY runningJobIndexChanged)
    Q_PROPERTY(int editingInternalQueueIndex READ editingInternalQueueIndex NOTIFY editingInternalQueueIndexChanged)

public:
    explicit StitchController(QObject *parent = nullptr);

    QString leftFace() const;
    void setLeftFace(const QString &path);
    QString rightFace() const;
    void setRightFace(const QString &path);
    QString topFace() const;
    void setTopFace(const QString &path);
    QString bottomFace() const;
    void setBottomFace(const QString &path);
    QString rightLeftFace() const;
    void setRightLeftFace(const QString &path);
    QString rightRightFace() const;
    void setRightRightFace(const QString &path);
    QString rightTopFace() const;
    void setRightTopFace(const QString &path);
    QString rightBottomFace() const;
    void setRightBottomFace(const QString &path);

    QString outputDirectory() const;
    void setOutputDirectory(const QString &path);
    QString rightOutputDirectory() const;
    void setRightOutputDirectory(const QString &path);
    QString outputName() const;
    void setOutputName(const QString &name);

    bool stereo() const;
    void setStereo(bool stereo);
    QString stereoEye() const;
    void setStereoEye(const QString &eye);
    bool alpha() const;
    void setAlpha(bool alpha);
    bool fxaa() const;
    void setFxaa(bool fxaa);
    bool cubic() const;
    void setCubic(bool cubic);
    bool autoDetectCubemapFaces() const;
    void setAutoDetectCubemapFaces(bool autoDetect);
    double fisheyeFov() const;
    void setFisheyeFov(double fov);
    double eyeSeparation() const;
    void setEyeSeparation(double value);
    double domeDiameter() const;
    void setDomeDiameter(double value);
    int resolution() const;
    void setResolution(int resolution);
    int cubemapResolution() const;
    void setCubemapResolution(int resolution);
    int msaa() const;
    void setMsaa(int msaa);

    QString format() const;
    void setFormat(const QString &format);
    int compression() const;
    void setCompression(int compression);
    int quality() const;
    void setQuality(int quality);
    int loadingThreads() const;
    void setLoadingThreads(int loadingThreads);
    int encodingThreads() const;
    void setEncodingThreads(int encodingThreads);

    int startIndex() const;
    void setStartIndex(int index);
    int stopIndex() const;
    void setStopIndex(int index);
    int outputStartIndex() const;
    void setOutputStartIndex(int index);
    int steps() const;
    void setSteps(int steps);
    QString customFrameList() const;
    void setCustomFrameList(const QString &list);
    int loadTextureAttemptsMax() const;
    void setLoadTextureAttemptsMax(int attempts);
    int loadTextureAttemptsSleep() const;
    void setLoadTextureAttemptsSleep(int seconds);

    int leftRotation() const;
    void setLeftRotation(int rotation);
    int rightRotation() const;
    void setRightRotation(int rotation);
    int topRotation() const;
    void setTopRotation(int rotation);
    int bottomRotation() const;
    void setBottomRotation(int rotation);

    bool running() const;
    bool sequenceIndexing() const;
    QString sequenceStatus() const;
    int progressCompleted() const;
    int progressTotal() const;
    double progress() const;
    QString progressStatus() const;
    QString elapsedTime() const;
    QString remainingTime() const;
    QString commandLinePreview() const;
    QString logText() const;

    Q_INVOKABLE void indexSequence(bool adoptDetectedRange = true);
    Q_INVOKABLE void verifyInputs();
    Q_INVOKABLE void launchStitch();
    Q_INVOKABLE void abortStitch();
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE void applyApplicationSettings();

    // Queue management
    Q_INVOKABLE void queueJob();  // Add current job to internal queue
    Q_INVOKABLE void reorderInternalQueue(int start, int destination);  // Reorder items in internal queue via drag-drop
    Q_INVOKABLE void removeJobFromInternalQueue(int index);  // Remove job at specified index from internal queue
    Q_INVOKABLE bool editInternalQueueJob(int index);
    Q_INVOKABLE bool saveEditedInternalQueueJob();
    Q_INVOKABLE void cancelEditingInternalQueueJob();
    Q_INVOKABLE bool saveInternalQueue(const QString &path);
    Q_INVOKABLE bool loadInternalQueue(const QString &path);
    Q_INVOKABLE void launchJobAtQueueIndex(int index);  // Launch specific job at given index in internal queue
    Q_INVOKABLE bool submitToCJob(const QString& jobId);  // Submit current job to C-Job (returns true if successful)
    Q_INVOKABLE bool isCJobConnected() const;
    Q_INVOKABLE void launchNextFromQueue();  // Launch next queued job

    // Returns list of output paths that will be created for each eye type
    QStringList outputPaths() const;

    // C-Job methods
    Q_INVOKABLE bool isCJobEnabled() const { return m_cjobEnabled; }
    Q_INVOKABLE void setCJobEnabled(bool enabled);
    int queueSize() const;
    QStringList queuedJobs() const;

    // Internal queue methods (disabled when C-Job enabled)
    Q_INVOKABLE bool isInternalQueueRunning() const { return m_internalQueueRunning; }
    Q_INVOKABLE void setInternalQueueRunning(bool running);
    int internalQueueSize() const { return m_queue.size(); }
    QStringList internalQueuedJobs() const;
    int runningJobIndex() const { return m_runningJobIndex; }
    int editingInternalQueueIndex() const { return m_editingInternalQueueIndex; }

Q_SIGNALS:
    void faceInputsChanged();
    void outputChanged();
    void stitchSettingsChanged();
    void encodingChanged();
    void sequenceChanged();
    void rotationsChanged();
    void runningChanged();
    void sequenceStatusChanged();
    void progressChanged();
    void commandLinePreviewChanged();
    void logTextChanged();

    // C-Job signals
    void cjobEnabledChanged();
    void queueChanged();

    // Internal queue signals
    void internalQueueRunningChanged();
    void internalQueueChanged();
    void runningJobIndexChanged();
    void editingInternalQueueIndexChanged();

private:
    enum class StitchEye { Mono, Left, Right };

    struct ProcessLaunch {
        QStringList arguments;
        QString activity;
        int totalFrames = 0;
    };

    struct ProcessState {
        QProcess *process = nullptr;
        ProcessLaunch launch;
        QByteArray standardOutputBuffer;
        QByteArray standardErrorBuffer;
        int completedFrames = 0;
        int totalFrames = 0;
        bool failed = false;
        bool finishing = false;
    };

    // C-Job structures
    struct JobInfo {
        QString jobId;
        QString instanceId;
        QString status;
        int progressCompleted = 0;
        int progressTotal = 0;

        // Job parameters (copied from controller when queued)
        QString leftFace;
        QString rightFace;
        QString topFace;
        QString bottomFace;
        QString rightLeftFace;
        QString rightRightFace;
        QString rightTopFace;
        QString rightBottomFace;
        QString outputDirectory;
        QString outputName;
        bool stereo = false;
        QString stereoEye;
        bool alpha = false;
        bool fxaa = false;
        bool cubic = true;
        double fisheyeFov = 180.0;
        double eyeSeparation = 0.065;
        double domeDiameter = 14.8;
        int resolution = 2048;
        int cubemapResolution = 2048;
        int msaa = 4;
        QString format = QStringLiteral("png");
        int compression = 6;
        int quality = 95;
        int loadingThreads = 8;
        int encodingThreads = 8;
        int startIndex = 0;
        int stopIndex = 0;
        int outputStartIndex = 0;
        int steps = 1;
        QString customFrameList;
        QString jobName;  // Pre-generated descriptive name for display
        int loadTextureAttemptsMax = 5;
        int loadTextureAttemptsSleep = 1;
        int leftRotation = 0;
        int rightRotation = 0;
        int topRotation = 0;
        int bottomRotation = 0;
    };

    JobInfo currentJobInfo() const;
    void applyJobInfo(const JobInfo &info);
    bool canEditInternalQueue() const;
    static QJsonObject jobInfoToJson(const JobInfo &info);
    static bool jobInfoFromJson(const QJsonObject &json, JobInfo *info, QString *error);
    QStringList buildArguments(StitchEye eye = StitchEye::Mono, bool verifyOnly = false) const;
    QVector<ProcessLaunch> buildLaunches(bool verifyOnly = false) const;
    int expectedFrameCount() const;
    static QString quoteArgument(const QString &argument);
    static QString normalizedPath(QString path);
    static QString cleanNativePath(const QString &path);
    static QString absolutePathFromWorkingDirectory(const QString &path);
    void startNodeProcess(const ProcessLaunch &launch);
    void startNodeProcesses(QVector<ProcessLaunch> launches);
    void finishNodeProcess(ProcessState *state, int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessOutput(ProcessState *state, QProcess::ProcessChannel channel);
    void processOutputLine(ProcessState *state, const QString &line, QProcess::ProcessChannel channel);
    void resetProgress(int totalFrames);
    void updateProgress();
    QString formattedElapsedTime() const;
    QString formattedRemainingTime() const;
    void appendLog(const QString &message);
    void emitCommandSignals();
    bool setFace(QString &target, const QString &path);
    bool setFaceValue(QString &target, const QString &path);
    bool completeCubemapFacesFromPath(const QString &path);
    static QString extractFilenameStem(const QString &filePath);
    bool validateLaunchInputs();
    QString firstSequencePath() const;
    QString generateJobName() const;

    // C-Job client methods
    void connectToCJobServer();
    void sendRegisterInstance();
    void disconnectFromCJobServer();
    void sendJsonToCJob(const QJsonObject &json);
    void onCJobReadyRead();
    void processCJobMessage(const QString& message);

    QString m_leftFace;
    QString m_rightFace;
    QString m_topFace;
    QString m_bottomFace;
    QString m_rightLeftFace;
    QString m_rightRightFace;
    QString m_rightTopFace;
    QString m_rightBottomFace;
    QString m_outputDirectory;
    QString m_rightOutputDirectory;
    QString m_outputName = QStringLiteral("stitched");
    bool m_stereo = false;
    QString m_stereoEye = QStringLiteral("both");
    bool m_alpha = false;
    bool m_fxaa = false;
    bool m_cubic = true;
    bool m_autoDetectCubemapFaces = true;
    double m_fisheyeFov = 180.0;
    double m_eyeSeparation = 0.065;
    double m_domeDiameter = 14.8;
    int m_resolution = 2048;
    int m_cubemapResolution = 2048;
    int m_msaa = 4;
    QString m_format = QStringLiteral("png");
    int m_compression = 6;
    int m_quality = 95;
    int m_loadingThreads = 8;
    int m_encodingThreads = 8;
    int m_startIndex = 0;
    int m_stopIndex = 0;
    int m_outputStartIndex = 0;
    int m_steps = 1;
    QString m_customFrameList;
    int m_loadTextureAttemptsMax = 5;
    int m_loadTextureAttemptsSleep = 1;
    int m_leftRotation = 0;
    int m_rightRotation = 0;
    int m_topRotation = 0;
    int m_bottomRotation = 0;
    bool m_internalQueueRunning = false;  // Internal queue auto-start state
    int m_runningJobIndex = -1;  // Index of currently running job in queue (-1 = none)
    int m_editingInternalQueueIndex = -1;
    bool m_sequenceIndexing = false;
    int m_sequenceIndexRequestId = 0;
    bool m_applyingApplicationSettings = false;
    QString m_sequenceStatus = QStringLiteral("No sequence indexed.");
    QString m_logText;
    QVector<ProcessState *> m_activeProcesses;
    QElapsedTimer m_elapsedTimer;
    QTimer m_progressTimer;
    int m_finishedFrames = 0;
    int m_progressCompleted = 0;
    int m_progressTotal = 0;
    bool m_processFailure = false;

    // C-Job state
    bool m_cjobEnabled = false;
    QLocalSocket *m_cjobSocket = nullptr;
    QString m_cjobBuffer;
    QString m_currentCJobJobId;  // Current job ID being processed from C-Job
    QVector<JobInfo> m_queue;  // Internal job queue (display only when C-Job enabled)

    // Local storage for job parameters keyed by unique job ID
    // This allows C-Stitch to retrieve full params when C-Job sends a launch command with just the job ID
    QMap<QString, JobInfo> m_storedJobs;

    Q_INVOKABLE JobInfo retrieveStoredJob(const QString &jobId) const { return m_storedJobs.value(jobId, JobInfo{}); }
};

#endif // CSTITCH_STITCHCONTROLLER_H