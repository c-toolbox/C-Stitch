/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSTITCH_STITCHQUEUEMODEL_H
#define CSTITCH_STITCHQUEUEMODEL_H

#include "jobqueuemodel.h"

class StitchQueueModel : public JobQueueModel {
    Q_OBJECT
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)

public:
    explicit StitchQueueModel(QObject *parent = nullptr) : JobQueueModel(parent) {}

    int selectedIndex() const { return m_selectedIndex; }
    void setSelectedIndex(int index);

Q_SIGNALS:
    void selectedIndexChanged();

private:
    int m_selectedIndex = -1;

    // Override to allow proper Qt meta-object system integration
    QHash<int, QByteArray> roleNames() const override;
};

#endif // CSTITCH_STITCHQUEUEMODEL_H