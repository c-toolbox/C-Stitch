#
# Copyright: 2026 Erik Sundén
# SPDX-License-Identifier: GPL-3.0-or-later
#

set(TARGET_NAME C-Stitch)
set(CJOB_TARGET_NAME C-Job)
set(CSTITCH_VERSION_MAJOR 1)
set(CSTITCH_VERSION_MINOR 0)
set(CSTITCH_VERSION_PATCH 0)
#set(CSTITCH_VERSION_PR_NAME "Beta")
#set(CSTITCH_VERSION_PR_VERS 2)

# --- C-Stitch (Main Application) ---
qt_add_executable(${TARGET_NAME})

set(CSTITCH_SOURCE_FILES
    main.cpp
    stitchapplication.cpp
    stitchapplication.h
    stitchcontroller.cpp
    stitchcontroller.h
    stitchnode.cpp
    stitchnode.h
    stitchwuffs.cpp
    stitchwuffs.h
    settings/stitchsettings.kcfg
    settings/stitchsettings.kcfgc
    utils/imagesequenceutils.cpp
    utils/imagesequenceutils.h
    jobqueue/jobqueuemodel.cpp
    jobqueue/jobqueuemodel.h
    jobqueue/stitchqueuemodel.h
    jobqueue/stitchqueuemodel.cpp
)

target_sources(${TARGET_NAME}
    PRIVATE
    ${CSTITCH_SOURCE_FILES}
    ../data/presets/default.conf
    ../data/images/images.qrc
    ../data/images/icon.rc
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Cpp" FILES ${CSTITCH_SOURCE_FILES})

find_package(QT NAMES Qt6)
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6 AND ${QT_VERSION_MINOR} GREATER_EQUAL 8)
    qt_standard_project_setup(REQUIRES 6.8)
else()
    qt_standard_project_setup(REQUIRES 6.5)
endif()
qt_policy(SET QTP0001 NEW)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8.0)
    qt_policy(SET QTP0004 NEW)
endif()

set(CSTITCH_QML_FILES
    qml/Main.qml
    qml/StitchSettingsDialog.qml
)

if(CSTITCH_FAST_QML_BUILD)
    foreach(QML_FILE ${CSTITCH_QML_FILES})
        set_source_files_properties(${QML_FILE} PROPERTIES
            QT_QML_SKIP_QMLLINT TRUE
            QT_QML_SKIP_CACHEGEN TRUE
        )
    endforeach()
endif()

qt_add_qml_module(${TARGET_NAME}
    URI org.ctoolbox.cstitch
    QML_FILES
    ${CSTITCH_QML_FILES}
)
get_target_property(_qt_core_target_type Qt6::Core TYPE)
if(_qt_core_target_type STREQUAL "STATIC_LIBRARY")
    qt_import_qml_plugins(${TARGET_NAME})
else()
    set_target_properties(${TARGET_NAME} PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
endif()
ecm_find_qmlmodule(org.kde.kirigami REQUIRED)
source_group(Qml FILES ${CSTITCH_QML_FILES})

kconfig_add_kcfg_files(${TARGET_NAME} GENERATE_MOC settings/stitchsettings.kcfgc)

set(TARGET_LIBRARIES
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2

    KF6::ColorScheme
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::CoreAddons
    KF6::I18n
    KF6::IconThemes
    KF6::Kirigami

    sgct::sgct
)

if(WIN32)
    list(APPEND TARGET_LIBRARIES opengl32)
endif()

if(EXISTS "${CSTITCH_WUFFS_RELEASE_DIR}/wuffs-unsupported-snapshot.c")
    target_compile_definitions(${TARGET_NAME} PRIVATE CSTITCH_WITH_WUFFS=1)
    target_include_directories(${TARGET_NAME} PRIVATE ${CSTITCH_WUFFS_RELEASE_DIR})
else()
    target_compile_definitions(${TARGET_NAME} PRIVATE CSTITCH_WITH_WUFFS=0)
    message(WARNING "Wuffs snapshot not found at ${CSTITCH_WUFFS_RELEASE_DIR}; image decode/encode wrappers will build as stubs")
endif()

if(EXISTS "${CSTITCH_SGCT_SOURCE_DIR}/CMakeLists.txt")
    option(SGCT_FREETYPE_SUPPORT "Build SGCT with Freetype2" ON)
    option(SGCT_SPOUT_SUPPORT "SGCT Spout support" OFF)
    option(SGCT_NDI_SUPPORT "SGCT NDI Support" OFF)
    option(SGCT_DEP_INCLUDE_FREETYPE "Include FreeType library" OFF)
    option(SGCT_BUILD_TESTS "Build SGCT tests" OFF)
    set(SCN_USE_EXTERNAL_FAST_FLOAT OFF CACHE BOOL "Use an external FastFloat package for scnlib" FORCE)
    set(SCN_DISABLE_FAST_FLOAT ON CACHE BOOL "Use std::from_chars instead of fetching FastFloat for scnlib" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    add_subdirectory(${CSTITCH_SGCT_SOURCE_DIR} sgct)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
    list(APPEND TARGET_LIBRARIES sgct::sgct)
    target_compile_definitions(${TARGET_NAME} PRIVATE CSTITCH_WITH_SGCT=1)
    target_include_directories(${TARGET_NAME} PRIVATE "${CSTITCH_SGCT_SOURCE_DIR}/ext/stb")
else()
    target_compile_definitions(${TARGET_NAME} PRIVATE CSTITCH_WITH_SGCT=0)
    message(WARNING "SGCT source not found at ${CSTITCH_SGCT_SOURCE_DIR}; node mode will validate arguments but rendering is disabled")
endif()

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE
        /utf-8
        /wd26439
        /wd26444
        /wd26495
        /wd26813
        /wd4702
        /experimental:external /external:anglebrackets /external:templates- /external:W0
    )
endif()

if(WIN32)
    set_target_properties(${TARGET_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TARGET_NAME} PRIVATE ${TARGET_LIBRARIES})
target_compile_features(${TARGET_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${TARGET_NAME} PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>)
target_compile_definitions(${TARGET_NAME} PUBLIC CSTITCH_VERSION="${CSTITCH_VERSION_MAJOR}.${CSTITCH_VERSION_MINOR}.${CSTITCH_VERSION_PATCH} ${CSTITCH_VERSION_PR_NAME} ${CSTITCH_VERSION_PR_VERS}")
target_compile_definitions(${TARGET_NAME} PUBLIC CSTITCH_VERSION_MAJOR=${CSTITCH_VERSION_MAJOR})
target_compile_definitions(${TARGET_NAME} PUBLIC CSTITCH_VERSION_MINOR=${CSTITCH_VERSION_MINOR})
target_compile_definitions(${TARGET_NAME} PUBLIC CSTITCH_VERSION_PATCH=${CSTITCH_VERSION_PATCH})

install(TARGETS ${TARGET_NAME} DESTINATION ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(DIRECTORY ../data/presets DESTINATION ${KDE_INSTALL_DATADIR}/${TARGET_NAME}/data FILES_MATCHING PATTERN "*.conf")

if(CSTITCH_INSTALL_DEPENDENCIES)
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    get_filename_component(_qt_dir "${_qt_bin_dir}" DIRECTORY)

    option(CSTITCH_INSTALL_PLUGINS_FROM_CRAFT "Copy complete plugins folder for Qt/KDE" ON)
    if(CSTITCH_INSTALL_PLUGINS_FROM_CRAFT)
        install(CODE [[
            message("Copying Qt/KDE plugins folder")
        ]])
        install(DIRECTORY "${_qt_dir}/plugins" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" FILES_MATCHING PATTERN "*.dll")
    endif()

    option(CSTITCH_INSTALL_QML_FROM_CRAFT "Copy complete qml folder for Qt/KDE" ON)
    if(CSTITCH_INSTALL_QML_FROM_CRAFT)
        install(CODE [[
            message("Copying KDE qml folder")
        ]])
        install(DIRECTORY "${_qt_dir}/qml" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" FILES_MATCHING PATTERN "*.dll")
        install(DIRECTORY "${_qt_dir}/qml" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" FILES_MATCHING PATTERN "*.qmltypes")
        install(DIRECTORY "${_qt_dir}/qml" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" FILES_MATCHING PATTERN "qmldir")
        install(DIRECTORY "${_qt_dir}/qml/org" DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/qml")
    endif()

    set_property(TARGET ${TARGET_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:${TARGET_NAME}>)

    if(EXISTS "${CSTITCH_SGCT_SOURCE_DIR}/ext/scalable")
        set(_scalable_dir "${CSTITCH_SGCT_SOURCE_DIR}/ext/scalable")
    endif()

    cmake_host_system_information(RESULT OS_VERSION QUERY OS_VERSION)
    string(REGEX MATCH "[0-9]+" OS_BUILD_NUM "${OS_VERSION}")
    if(${OS_BUILD_NUM} VERSION_GREATER_EQUAL 22000)
        message("Windows 11 or above detected, recommended to copy all DLLS from path instead of using GET_RUNTIME_DEPENDENCIES")
        set(INSTALL_GRP FALSE)
        set(INSTALL_DFP TRUE)
    else()
        message("Windows 10 or below detected, recommended to use GET_RUNTIME_DEPENDENCIES instead of copying all DLLS from path")
        set(INSTALL_GRP TRUE)
        set(INSTALL_DFP FALSE)
    endif()

    option(CSTITCH_INSTALL_GET_RUNTIME_DEPENDENCIES "When running INSTALL, look for dependencies with GET_RUNTIME_DEPENDENCIES and copy them" ${INSTALL_GRP})
    if(CSTITCH_INSTALL_GET_RUNTIME_DEPENDENCIES)
        set(MY_DEPENDENCY_PATHS ${_qt_bin_dir} ${_scalable_dir})
        install(CODE "set(MY_DEPENDENCY_PATHS \"${MY_DEPENDENCY_PATHS}\")")
        install(CODE "set(target \"$<TARGET_FILE:${TARGET_NAME}>\")")
        install(CODE [[
            message("Looking for direct dependencies for C-Stitch.exe")
            file(GET_RUNTIME_DEPENDENCIES
                EXECUTABLES ${target}
                PRE_EXCLUDE_REGEXES api-ms msvcp vcruntime kernel32
                POST_EXCLUDE_REGEXES C:[/\\]Windows[/\\]
                CONFLICTING_DEPENDENCIES_PREFIX CONFLICTING_DEPENDENCIES
                RESOLVED_DEPENDENCIES_VAR RESOLVED_DEPENDENCIES
                UNRESOLVED_DEPENDENCIES_VAR UNRESOLVED_DEPENDENCIES
                DIRECTORIES ${MY_DEPENDENCY_PATHS}
            )
            foreach(DEPENDENCY ${CONFLICTING_DEPENDENCIES})
                message(WARNING "Conflicting dependency ${DEPENDENCY}")
            endforeach()
            message("Copying detected dependencies")
            foreach(DEPENDENCY ${RESOLVED_DEPENDENCIES})
                message("Copying ${DEPENDENCY}")
                file(INSTALL
                    DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
                    FILES "${DEPENDENCY}"
                )
            endforeach()
        ]])
    endif()

    option(CSTITCH_INSTALL_DLLS_FROM_PATHS "When running INSTALL, copy all DLLS from CSTITCH_INSTALL_DLLS_FROM_PATH_LIST to the INSTALL/bin folder" ${INSTALL_DFP})
    if(CSTITCH_INSTALL_DLLS_FROM_PATHS)
        set(CIDFPL ${_qt_bin_dir})
        if(BUILD_WITH_VCPKG_SUPPORT)
            list(APPEND CIDFPL "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        endif()
        if(_scalable_dir)
            list(APPEND CIDFPL ${_scalable_dir})
        endif()

        set(CSTITCH_INSTALL_DLLS_FROM_PATH_LIST ${CIDFPL} CACHE STRING "The paths where to look for DLLs")
        install(CODE "set(_all_dll_copy_paths \"${CSTITCH_INSTALL_DLLS_FROM_PATH_LIST}\")")
        install(CODE [[
            message("Copying all DLLs from CSTITCH_INSTALL_DLLS_FROM_PATH_LIST to install folder")
            foreach(DLL_DIR ${_all_dll_copy_paths})
                message("Looking for DLLs in ${DLL_DIR}")
                if(EXISTS "${DLL_DIR}")
                    file(GLOB DLL_FILES "${DLL_DIR}/*.dll")
                    foreach(DLL_FILE ${DLL_FILES})
                        message("Copying ${DLL_FILE}")
                        file(INSTALL
                            DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
                            FILES "${DLL_FILE}"
                        )
                    endforeach()
                else()
                    message("Directory ${DLL_DIR} does not exist")
                endif()
            endforeach()
        ]])
    endif()
endif()

# --- Include C-Job via git subtree ---
add_subdirectory(cjob/src)
