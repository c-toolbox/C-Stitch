/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CSTITCH_STITCHNODE_H
#define CSTITCH_STITCHNODE_H

namespace CStitch::Node {

bool isNodeMode(int argc, char **argv);
int run(int argc, char **argv);

} // namespace CStitch::Node

#endif // CSTITCH_STITCHNODE_H
