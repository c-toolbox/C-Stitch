/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSTITCH_STITCHAPPLICATION_H
#define CSTITCH_STITCHAPPLICATION_H

#include "stitchcontroller.h"

#include <QObject>
#include <QString>
#include <QUrl>

class QGuiApplication;
class KAboutData;
class QQmlApplicationEngine;

class StitchApplication : public QObject {
    Q_OBJECT
    Q_PROPERTY(StitchController *controller READ controller CONSTANT)

public:
    explicit StitchApplication(int &argc, char **argv);
    ~StitchApplication() override;

    int run();
    StitchController *controller();

    Q_INVOKABLE QString version() const;
    Q_INVOKABLE QUrl pathToUrl(const QString &path) const;
    Q_INVOKABLE QString urlToPath(const QUrl &url) const;
    Q_INVOKABLE QString stitchDataPath(const QString &relativePath) const;

private:
    QGuiApplication *m_app = nullptr;
    QQmlApplicationEngine *m_engine = nullptr;
    KAboutData* m_aboutData = nullptr;
    StitchController m_controller;
};

#endif // CSTITCH_STITCHAPPLICATION_H
