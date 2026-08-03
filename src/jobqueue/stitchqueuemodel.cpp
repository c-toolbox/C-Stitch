/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stitchqueuemodel.h"

void StitchQueueModel::setSelectedIndex(int index)
{
    if (m_selectedIndex == index) {
        return;
    }
    m_selectedIndex = index;
    Q_EMIT selectedIndexChanged();
}

QHash<int, QByteArray> StitchQueueModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "jobName";
    roles[StatusRole] = "status";
    return roles;
}
