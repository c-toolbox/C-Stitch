#
# Copyright: 2026 Erik Sundén
# SPDX-License-Identifier: GPL-3.0-or-later
#

if(CSTITCH_INSTALL_DEPENDENCIES)
    install(CODE [[
        message("Copying C-Stitch data")
    ]])
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/data/presets" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/data" OPTIONAL)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/data/images" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/data" OPTIONAL)
endif()
