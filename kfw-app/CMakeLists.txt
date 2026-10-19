# CMakeLists.txt — Kishan Forensic Workbench (kfw-app)
# Copyright (C) 2026 Kishan G A
#
# Builds the KFW forensic application as a standalone executable.
# KFW owns all of its sources (ui/, models/, cases, evidence, integrity,
# media, timeline, reporting). The upstream libVLC engine is consumed as a
# vendored SDK (third_party/vlc-3.0.23) that provides headers, an import
# library and the portable runtime DLLs shipped with KFW releases.
#
# Usage (MSYS2 UCRT64 shell, from the kfw-app directory):
#   cmake -S . -B build -G "Ninja" -DCMAKE_PREFIX_PATH="C:/msys64/ucrt64"
#   cmake --build build --parallel
#
# With a custom VLC SDK location:
#   -DKFW_VLC_SDK_DIR="path/to/vlc-sdk"   (expects include/ + lib/libvlc.lib)

cmake_minimum_required(VERSION 3.20)
project(KishanForensicWorkbench VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ── Options -----------------------------------------------------------------
option(KFW_BUILD_TESTS "Build KFW unit tests" ON)

# MSYS2/MinGW find_library only checks .dll.a/.a by default; allow plain
# .lib import libraries so find_library() can locate vendored vlc.lib.
list(APPEND CMAKE_FIND_LIBRARY_SUFFIXES ".lib")

# Qt's QtDeploySupport needs absolute Windows paths for its post-install
# deployment pass; we ship the runtime ourselves, so skip its setup.
set(QT_SKIP_SETUP_DEPLOYMENT ON CACHE BOOL "" FORCE)

# ── Find Qt -----------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia)
qt_standard_project_setup()

# ── VLC SDK (vendored libVLC 3.x) --------------------------------------------
if(NOT DEFINED KFW_VLC_SDK_DIR)
    set(KFW_VLC_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/vlc-3.0.23")
endif()

set(VLC_INCLUDE_DIR "${KFW_VLC_SDK_DIR}/include")
set(VLC_LIB_DIR     "${KFW_VLC_SDK_DIR}/lib")

find_library(LIBVLC_LIB
    NAMES vlc libvlc
    PATHS "${VLC_LIB_DIR}" NO_DEFAULT_PATH)

if(NOT LIBVLC_LIB)
    message(FATAL_ERROR
        "libvlc import library not found in ${VLC_LIB_DIR}. "
        "Set KFW_VLC_SDK_DIR to a VLC SDK tree (include/ + lib/).")
endif()

# ── SQLite -------------------------------------------------------------------
if(NOT DEFINED SQLITE3_INCLUDE_DIR)
    find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
endif()
if(NOT DEFINED SQLITE3_LIB)
    find_library(SQLITE3_LIB NAMES sqlite3 libsqlite3)
endif()

# MSYS2 inherenitslike quirks: system paths may omit the active MSYSTEM
# prefix (/ucrt64); fall back to it explicitly.
if(NOT SQLITE3_INCLUDE_DIR AND DEFINED ENV{MSYSTEM_PREFIX})
    find_path(SQLITE3_INCLUDE_DIR sqlite3.h
        PATHS "$ENV{MSYSTEM_PREFIX}/include" NO_DEFAULT_PATH)
endif()
if(NOT SQLITE3_LIB)
    find_library(SQLITE3_LIB NAMES sqlite3 libsqlite3
        PATHS "$ENV{MSYSTEM_PREFIX}/lib" NO_DEFAULT_PATH)
endif()

if(NOT SQLITE3_LIB)
    # Fallback: build sqlite3 from the amalgamation if present
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sqlite/sqlite3.c")
        message(STATUS "Using bundled SQLite amalgamation")
        add_library(sqlite3_amalg STATIC
            "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sqlite/sqlite3.c")
        set(SQLITE3_LIB sqlite3_amalg)
        set(SQLITE3_INCLUDE_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/sqlite")
    else()
        message(FATAL_ERROR
            "SQLite3 not found. Set SQLITE3_LIB or provide "
            "third_party/sqlite/sqlite3.c.")
    endif()
endif()

# ── Forensic layer (C sources) -----------------------------------------------
add_library(kfw_forensic STATIC
    integrity/kfw_integrity.c
    cases/kfw_db.c
    evidence/kfw_evidence.c
    timeline/kfw_timeline.c
    reporting/kfw_report.c
    media/kfw_media.c
    detect/kfw_detect.c
)

target_include_directories(kfw_forensic PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${VLC_INCLUDE_DIR}"
    "${SQLITE3_INCLUDE_DIR}"
)

target_link_libraries(kfw_forensic PUBLIC
    ${SQLITE3_LIB}
    ${LIBVLC_LIB}
)

if(WIN32)
    target_link_libraries(kfw_forensic PUBLIC bcrypt)
endif()

# ── Qt list models (C++) -------------------------------------------------------
qt_add_library(kfw_qt_model STATIC
    ui/models/kfw_case_model.cpp
    ui/models/kfw_evidence_model.cpp
    ui/models/kfw_timeline_model.cpp
)
target_include_directories(kfw_qt_model PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/ui"
)
target_link_libraries(kfw_qt_model PUBLIC Qt6::Core Qt6::Qml)

# ── Application shell -----------------------------------------------------------
qt_add_executable(kfw
    ui/kfw_main.cpp
    ui/kfw_shell.cpp
    ui/kfw_video_screen.cpp
    ui/kfw.exe.manifest
)

if(WIN32)
    target_sources(kfw PRIVATE kfw.rc)
endif()

# KFWTheme.qml is a QML singleton; mark it so the generated qmldir
# declares "singleton KFWTheme ...".
set_source_files_properties(ui/qml/KFWTheme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE)

# Embed the QML UI (and its icon resources) into the binary
qt_add_qml_module(kfw
    URI KFW
    VERSION 1.0
    QML_FILES
        ui/qml/KFWAppShell.qml
        ui/qml/KFWDashboard.qml
        ui/qml/KFWExaminationWorkspace.qml
        ui/qml/KFWTheme.qml
        ui/qml/CaseRow.qml
        ui/qml/InfoRow.qml
        ui/qml/KFWStatCard.qml
        ui/qml/KFWStatusChip.qml
        ui/qml/KFWToast.qml
        ui/qml/KFWButton.qml
        ui/qml/KFWField.qml
        ui/qml/KFWIconButton.qml
        ui/qml/KFWAvatarBadge.qml
    RESOURCES
        ui/qml/icons/logo.svg
        ui/qml/icons/dashboard.svg
        ui/qml/icons/folder.svg
        ui/qml/icons/examine.svg
        ui/qml/icons/shield.svg
        ui/qml/icons/report.svg
        ui/qml/icons/plus.svg
        ui/qml/icons/clock.svg
        ui/qml/icons/hash.svg
        ui/qml/icons/play.svg
        ui/qml/icons/pause.svg
        ui/qml/icons/skipback.svg
        ui/qml/icons/skipfwd.svg
        ui/qml/icons/back1.svg
        ui/qml/icons/fwd1.svg
        ui/qml/icons/back10.svg
        ui/qml/icons/fwd10.svg
        ui/qml/icons/pin.svg
        ui/qml/icons/export.svg
        ui/qml/icons/bookmark.svg
        ui/qml/icons/note.svg
        ui/qml/icons/chevron.svg
        ui/qml/icons/back.svg
        ui/qml/icons/save.svg
        ui/qml/icons/trailer.svg
        ui/qml/icons/external.svg
        ui/qml/icons/info.svg
        ui/qml/icons/warning.svg
        ui/qml/icons/critical.svg
        ui/qml/icons/ok.svg
        ui/qml/icons/close.svg
        ui/qml/icons/search.svg
        ui/qml/icons/film.svg
)

# Window icon for the running app (taskbar / alt-tab)
qt_add_resources(kfw "kfw_window_icon"
    PREFIX "/"
    FILES ui/kfw.ico
)

target_include_directories(kfw PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui"
    "${VLC_INCLUDE_DIR}"
)

target_link_libraries(kfw PRIVATE
    kfw_forensic
    kfw_qt_model
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

if(WIN32)
    # Deep QML compile/isolate teardown repeatedly blew through the 2 MiB
    # default MinGW stack (0xC00000FD); 16 MiB removes the cliff.
    target_link_options(kfw PRIVATE -Wl,--stack=16777216)
endif()

set_target_properties(kfw PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE    FALSE
    OUTPUT_NAME      "KishanForensicWorkbench"
)

# ── Tests -------------------------------------------------------------------
if(KFW_BUILD_TESTS)
    enable_testing()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../tests" kfw-tests)
endif()

# ── Runtime deployment (dev tree convenience) -----------------------------------
# Copy the VLC engine + SQLite + GCC runtime DLLs next to every binary so
# dev runs and ctest work without hand-moving files. Release packaging
# (windeployqt) copies those plus the full Qt runtime separately.
if(WIN32)
    find_program(KFW_WINDEPLOYQT_EXE windeployqt
        HINTS "${CMAKE_PREFIX_PATH}/bin")
    add_custom_command(TARGET kfw POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${KFW_VLC_SDK_DIR}/libvlc.dll"
            "${KFW_VLC_SDK_DIR}/libvlccore.dll"
            "$<TARGET_FILE_DIR:kfw>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${KFW_VLC_SDK_DIR}/libvlc.dll"
            "${KFW_VLC_SDK_DIR}/libvlccore.dll"
            "$<TARGET_FILE_DIR:kfw_forensic>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${KFW_VLC_SDK_DIR}/plugins"
            "$<TARGET_FILE_DIR:kfw>/plugins")
    foreach(_rt libsqlite3-0.dll libgcc_s_seh-1.dll libstdc++-6.dll
                libwinpthread-1.dll)
        if(EXISTS "${CMAKE_PREFIX_PATH}/bin/${_rt}")
            add_custom_command(TARGET kfw POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_PREFIX_PATH}/bin/${_rt}"
                    "$<TARGET_FILE_DIR:kfw>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_PREFIX_PATH}/bin/${_rt}"
                    "$<TARGET_FILE_DIR:kfw_forensic>")
        endif()
    endforeach()
    if(KFW_WINDEPLOYQT_EXE)
        add_custom_command(TARGET kfw POST_BUILD
            COMMAND "${KFW_WINDEPLOYQT_EXE}" --qmldir
                    "${CMAKE_CURRENT_SOURCE_DIR}/ui/qml" --release
                    "$<TARGET_FILE:kfw>"
            VERBATIM)
    endif()
endif()

# ── Install -----------------------------------------------------------------
install(TARGETS kfw DESTINATION bin)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md"
    DESTINATION share/kfw)
