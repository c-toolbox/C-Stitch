/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stitchapplication.h"

#include "jobqueue/stitchqueuemodel.h"
#include "stitchsettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QModelIndex>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <qqml.h>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include <KAboutData>
#include <KColorSchemeManager>

void initializeImageResources()
{
    Q_INIT_RESOURCE(images);
}

namespace {

QString defaultStitchDataPath(const QString &relativePath)
{
    const QString dataPath = QStringLiteral("data/") + QDir::fromNativeSeparators(relativePath);
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList searchRoots = {
        applicationDir,
        QDir(applicationDir).filePath(QStringLiteral("..")),
        QDir(applicationDir).filePath(QStringLiteral("../..")),
        QDir(applicationDir).filePath(QStringLiteral("../../..")),
        QDir::currentPath(),
    };

    for (const QString &root : searchRoots) {
        const QString candidate = QDir::cleanPath(QDir(root).filePath(dataPath));
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(candidate);
        }
    }

    return QDir::toNativeSeparators(dataPath);
}

QGuiApplication *createApplication(int &argc, char **argv)
{
    initializeImageResources();

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication::setOrganizationName(QStringLiteral("Visualization Center C"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("github.com/c-toolbox/C-Stitch"));
    QGuiApplication::setApplicationName(QStringLiteral("C-Stitch"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("C-Stitch ") + QString::fromLatin1(CSTITCH_VERSION));
    QGuiApplication::setApplicationVersion(QString::fromLatin1(CSTITCH_VERSION));
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication *app = new QGuiApplication(argc, argv);
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    QIcon::setThemeName(QStringLiteral("breeze"));
    app->setWindowIcon(QIcon(QStringLiteral(":/C_transparent.png")));
    return app;
}

} // namespace

StitchApplication::StitchApplication(int &argc, char **argv)
    : m_app(createApplication(argc, argv))
{
    m_aboutData = new KAboutData(QStringLiteral("C-Stitch"),
        QStringLiteral("C-Stitch"),
        QString::fromLatin1(CSTITCH_VERSION));
    m_aboutData->setShortDescription(QStringLiteral("A stitcher utility for fisheye/fulldome images."));
    m_aboutData->setLicense(KAboutLicense::GPL_V3);
    m_aboutData->setCopyrightStatement(QString::fromUtf8("(c) 2026 Erik Sundén"));
    m_aboutData->setHomepage(QStringLiteral("https://c-toolbox.github.io/C-Stitch/guides/setup/c-stitch"));
    m_aboutData->setBugAddress(QStringLiteral("https://github.com/c-toolbox/C-Stitch/issues").toUtf8());
    m_aboutData->setDesktopFileName(QStringLiteral("org.ctoolbox.cstitch"));
    m_aboutData->addAuthor(QString::fromUtf8("Contact/owner: Erik Sundén"),
        QStringLiteral("Creator of C-Stitch"),
        QStringLiteral("eriksunden85@gmail.com"));
    KAboutData::setApplicationData(*m_aboutData);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("C-Stitch ") + QString::fromLatin1(CSTITCH_VERSION));

    KColorSchemeManager *schemes = KColorSchemeManager::instance();
    schemes->activateScheme(schemes->indexForScheme(QStringLiteral("Breeze Dark")));

    m_engine = new QQmlApplicationEngine(m_app);
    QObject::connect(m_engine, &QQmlApplicationEngine::quit, m_app, &QCoreApplication::quit);
    QQmlEngine::setObjectOwnership(m_app, QQmlEngine::CppOwnership);
    qmlRegisterSingletonInstance("org.ctoolbox.cstitch", 1, 0, "StitchSettings", StitchSettings::self());
    m_engine->rootContext()->setContextProperty(QStringLiteral("app"), this);
    
    // Create internal queue model for drag-and-drop reordering
    StitchQueueModel *internalQueueModel = new StitchQueueModel(this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("internalQueueModel"), internalQueueModel);
    
    // Sync controller's internalQueuedJobs to the model.
    // Using queued connection ensures setJobNames() is called AFTER any in-flight
    // model operations (like beginMoveRows/endMoveRows) complete, preventing conflicts.
    connect(&m_controller, &StitchController::internalQueueChanged, [this, internalQueueModel]() {
        QMetaObject::invokeMethod(internalQueueModel, [this, internalQueueModel]() {
            QStringList jobs = m_controller.internalQueuedJobs();
            internalQueueModel->setJobNames(jobs);
        }, Qt::QueuedConnection);
    });
    
    // Also sync when queue changes (C-Job related)
    connect(&m_controller, &StitchController::queueChanged, [this, internalQueueModel]() {
        QMetaObject::invokeMethod(internalQueueModel, [this, internalQueueModel]() {
            QStringList jobs = m_controller.internalQueuedJobs();
            internalQueueModel->setJobNames(jobs);
        }, Qt::QueuedConnection);
    });
    
    // Handle drag-and-drop reordering from the model.
    // When QML calls model.moveRow(), it emits rowsMoved which triggers controller reorder.
    // The controller updates its internal queue and emits internalQueueChanged().
    // Using queued connection ensures this happens asynchronously, avoiding conflicts
    // with any in-flight Qt model operations (beginMoveRows/endMoveRows).
    connect(internalQueueModel, &StitchQueueModel::rowsMoved, [this](int start, int /*end*/, int destination) {
        QMetaObject::invokeMethod(&m_controller, [this, start, destination]() {
            m_controller.reorderInternalQueue(start, destination);
        }, Qt::QueuedConnection);
    });

    const QUrl moduleUrl(QStringLiteral("qrc:/qt/qml/org/ctoolbox/cstitch/Main.qml"));
    QObject::connect(m_engine, &QQmlApplicationEngine::objectCreated,
        m_app,
        [moduleUrl](QObject *object, const QUrl &objectUrl) {
            if (!object && objectUrl == moduleUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    m_engine->loadFromModule("org.ctoolbox.cstitch", "Main");
}

StitchApplication::~StitchApplication()
{
    delete m_engine;
    delete m_aboutData;
    delete m_app;
}

int StitchApplication::run()
{
    return m_app->exec();
}

StitchController *StitchApplication::controller()
{
    return &m_controller;
}

QString StitchApplication::version() const
{
    return QString::fromLatin1(CSTITCH_VERSION);
}

QUrl StitchApplication::pathToUrl(const QString &path) const
{
    return QUrl::fromLocalFile(QDir::fromNativeSeparators(path));
}

QString StitchApplication::urlToPath(const QUrl &url) const
{
    return QDir::toNativeSeparators(url.toLocalFile());
}

QString StitchApplication::stitchDataPath(const QString &relativePath) const
{
    return defaultStitchDataPath(relativePath);
}
