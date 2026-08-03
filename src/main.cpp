/*
 * SPDX-FileCopyrightText:
 * 2026 Erik Sundén
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stitchapplication.h"
#include "stitchnode.h"

int main(int argc, char **argv)
{
    if (CStitch::Node::isNodeMode(argc, argv)) {
        return CStitch::Node::run(argc, argv);
    }

    StitchApplication app(argc, argv);
    return app.run();
}
