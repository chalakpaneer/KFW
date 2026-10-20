# tests/CMakeLists.txt — KFW unit tests
# Copyright (C) 2026 Kishan G A

cmake_minimum_required(VERSION 3.20)

# Called as a subdirectory of kfw-app/CMakeLists.txt.
# kfw_forensic target and VLC/SQLITE variables are provided by the parent.

include(CTest)

# Runtime DLLs needed beside the test executables (VLC engine, SQLite,
# GCC runtime). Copied post-build so ctest runs straight from the tree.
set(KFW_TEST_RUNTIME
    "${CMAKE_PREFIX_PATH}/bin/libsqlite3-0.dll"
    "${CMAKE_PREFIX_PATH}/bin/libgcc_s_seh-1.dll"
    "${CMAKE_PREFIX_PATH}/bin/libstdc++-6.dll"
    "${CMAKE_PREFIX_PATH}/bin/libwinpthread-1.dll"
    "${KFW_VLC_SDK_DIR}/libvlc.dll"
    "${KFW_VLC_SDK_DIR}/libvlccore.dll")
foreach(_rt IN LISTS KFW_TEST_RUNTIME)
    if(NOT EXISTS "${_rt}")
        list(REMOVE_ITEM KFW_TEST_RUNTIME "${_rt}")
    endif()
endforeach()

function(kfw_test_runtime _tgt)
    add_custom_command(TARGET "${_tgt}" POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${KFW_TEST_RUNTIME}
            "$<TARGET_FILE_DIR:${_tgt}>")
endfunction()

# ── integrity tests ---------------------------------------------------------
add_executable(test_integrity
    integrity/test_integrity.c
)
target_include_directories(test_integrity PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../kfw-app"
    "${SQLITE3_INCLUDE_DIR}"
)
target_link_libraries(test_integrity PRIVATE kfw_forensic)
kfw_test_runtime(test_integrity)
add_test(NAME integrity COMMAND test_integrity)

# ── playback / media adapter tests -----------------------------------------
add_executable(test_playback
    playback/test_playback.c
)
target_include_directories(test_playback PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../kfw-app"
    "${VLC_INCLUDE_DIR}"
)
target_link_libraries(test_playback PRIVATE kfw_forensic)
kfw_test_runtime(test_playback)
add_test(NAME playback COMMAND test_playback
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures")
if(WIN32)
    set_tests_properties(playback PROPERTIES
        ENVIRONMENT "VLC_PLUGIN_PATH=${KFW_VLC_SDK_DIR}/plugins")
endif()

# ── report tests -----------------------------------------------------------
add_executable(test_reports
    reports/test_reports.c
)
target_include_directories(test_reports PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../kfw-app"
    "${SQLITE3_INCLUDE_DIR}"
)
target_link_libraries(test_reports PRIVATE kfw_forensic)
kfw_test_runtime(test_reports)
add_test(NAME reports COMMAND test_reports)

# ── live-analysis detector tests ---------------------------------------------
add_executable(test_detect
    detect/test_detect.c
)
target_include_directories(test_detect PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../kfw-app"
)
target_link_libraries(test_detect PRIVATE kfw_forensic)
kfw_test_runtime(test_detect)
add_test(NAME detect COMMAND test_detect)

# ── SHA-256 known-answer vectors (FIPS 180-4) — portable, Windows included ----
add_executable(test_sha256_vectors integrity/test_sha256_vectors.c)
target_include_directories(test_sha256_vectors PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../kfw-app")
target_link_libraries(test_sha256_vectors PRIVATE kfw_forensic)
kfw_test_runtime(test_sha256_vectors)
add_test(NAME sha256_vectors COMMAND test_sha256_vectors WORKING_DIRECTORY
    "${CMAKE_CURRENT_BINARY_DIR}")

# ── v2 forensic services: migration, duplicates, tags, search, custody, packages
# Uses POSIX helpers (sleep, system) so it is built on non-Windows hosts only;
# run it on Linux/CI with tests/run_linux.sh.
if(NOT WIN32)
    add_executable(test_v2 v2/test_v2.c)
    target_link_libraries(test_v2 PRIVATE kfw_forensic)
    add_test(NAME v2_services COMMAND test_v2)
endif()
