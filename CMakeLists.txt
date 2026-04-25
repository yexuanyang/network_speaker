cmake_minimum_required(VERSION 3.31)

project(network_speaker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(GNUInstallDirs)

find_package(Threads REQUIRED)
set(NSPEAKER_GTEST_TARGET "")

if(WIN32)
    option(NSPEAKER_USE_BUNDLED_OPUS "Build Opus from third_party/opus" ON)
else()
    option(NSPEAKER_USE_BUNDLED_OPUS "Build Opus from third_party/opus" OFF)
endif()

find_package(PkgConfig QUIET)

set(NSPEAKER_OPUS_TARGET "")
if(NOT NSPEAKER_USE_BUNDLED_OPUS)
    find_package(Opus CONFIG QUIET)
    if(TARGET Opus::opus)
        set(NSPEAKER_OPUS_TARGET Opus::opus)
    elseif(PkgConfig_FOUND)
        pkg_check_modules(OPUS QUIET IMPORTED_TARGET opus)
        if(TARGET PkgConfig::OPUS)
            set(NSPEAKER_OPUS_TARGET PkgConfig::OPUS)
        endif()
    endif()
endif()

if(NSPEAKER_OPUS_TARGET STREQUAL "")
    set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(NSPEAKER_SAVED_BUILD_TESTING ${BUILD_TESTING})
    set(BUILD_TESTING OFF)
    add_subdirectory(third_party/opus EXCLUDE_FROM_ALL)
    set(BUILD_TESTING ${NSPEAKER_SAVED_BUILD_TESTING})
    set(NSPEAKER_OPUS_TARGET Opus::opus)
endif()

if(BUILD_TESTING)
    find_package(GTest CONFIG QUIET)
    if(TARGET GTest::gtest)
        set(NSPEAKER_GTEST_TARGET GTest::gtest)
    elseif(PkgConfig_FOUND AND NOT WIN32)
        pkg_check_modules(GTEST QUIET IMPORTED_TARGET gtest)
        if(TARGET PkgConfig::GTEST)
            set(NSPEAKER_GTEST_TARGET PkgConfig::GTEST)
        endif()
    endif()
    if(NOT NSPEAKER_GTEST_TARGET)
        include(FetchContent)

        set(_nspeaker_git_proxy_args "")
        if(DEFINED ENV{https_proxy})
            list(APPEND _nspeaker_git_proxy_args "https.proxy=$ENV{https_proxy}")
        elseif(DEFINED ENV{HTTPS_PROXY})
            list(APPEND _nspeaker_git_proxy_args "https.proxy=$ENV{HTTPS_PROXY}")
        endif()
        if(DEFINED ENV{http_proxy})
            list(APPEND _nspeaker_git_proxy_args "http.proxy=$ENV{http_proxy}")
        elseif(DEFINED ENV{HTTP_PROXY})
            list(APPEND _nspeaker_git_proxy_args "http.proxy=$ENV{HTTP_PROXY}")
        endif()

        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.14.0
            GIT_CONFIG     ${_nspeaker_git_proxy_args}
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
        set(NSPEAKER_GTEST_TARGET GTest::gtest)
    endif()
endif()

if(PkgConfig_FOUND AND UNIX AND NOT APPLE)
    pkg_check_modules(PULSE IMPORTED_TARGET libpulse)
endif()

add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>)

add_library(audio_base INTERFACE)
target_include_directories(audio_base INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/audio_base/include)
target_link_libraries(audio_base INTERFACE project_warnings)

add_library(transport STATIC
    libs/transport/src/audio_packet.cpp
    libs/transport/src/jitter_buffer.cpp
    libs/transport/src/udp_socket.cpp)
target_include_directories(transport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/transport/include)
target_link_libraries(transport PUBLIC audio_base Threads::Threads)
if(WIN32)
    target_link_libraries(transport PRIVATE ws2_32)
endif()

add_library(codec_opus STATIC
    libs/codec_opus/src/opus_codec.cpp)
target_include_directories(codec_opus PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/codec_opus/include)
target_link_libraries(codec_opus PUBLIC audio_base ${NSPEAKER_OPUS_TARGET})

add_library(host_audio STATIC
    server/hostd/src/capture_factory.cpp
    server/hostd/src/sine_wave_capture.cpp
    server/hostd/src/platform/linux/pulse_monitor_capture.cpp
    server/hostd/src/platform/linux/pulse_device_enumerator.cpp
    server/hostd/src/platform/windows/wasapi_loopback_capture.cpp
    server/hostd/src/platform/windows/wasapi_device_enumerator.cpp
    server/hostd/src/udp_audio_sender.cpp)
target_include_directories(host_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/server/hostd/include)
target_link_libraries(host_audio PUBLIC audio_base transport project_warnings)
if(PULSE_FOUND)
    target_link_libraries(host_audio PRIVATE PkgConfig::PULSE)
    target_compile_definitions(host_audio PRIVATE NSPEAKER_HAVE_PULSE=1)
endif()
if(WIN32)
    target_link_libraries(host_audio PRIVATE ole32 avrt)
endif()

add_library(client_core STATIC
    client/core/src/client_session.cpp
    client/core/src/receiver.cpp
    client/core/src/player_pipeline.cpp)
target_include_directories(client_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/client/core/include)
target_link_libraries(client_core PUBLIC audio_base codec_opus transport)

add_executable(hostd
    server/hostd/src/main.cpp)
target_link_libraries(hostd PRIVATE host_audio codec_opus transport)

add_executable(clientd
    client/core/src/main.cpp)
target_link_libraries(clientd PRIVATE client_core)

if(BUILD_TESTING AND NOT NSPEAKER_GTEST_TARGET STREQUAL "")
    add_executable(network_speaker_tests
        tests/test_main.cpp
        tests/audio_packet_test.cpp
        tests/callback_audio_sink_test.cpp
        tests/receiver_test.cpp
        tests/jitter_buffer_test.cpp
        tests/opus_codec_test.cpp
        tests/end_to_end_test.cpp
        tests/weak_network_test.cpp)
    target_link_libraries(network_speaker_tests PRIVATE
        client_core
        host_audio
        codec_opus
        transport
        ${NSPEAKER_GTEST_TARGET}
        Threads::Threads)
    add_test(NAME network_speaker_tests COMMAND network_speaker_tests)
elseif(BUILD_TESTING)
    message(WARNING "GoogleTest not found. Skipping network_speaker_tests target.")
endif()

# ---------------------------------------------------------------------------
# Optional: Tauri desktop GUI application
# ---------------------------------------------------------------------------
option(NSPEAKER_BUILD_DESKTOP_GUI
       "Build the Tauri desktop GUI (requires Node.js 20+, npm, Rust/Cargo)" OFF)

if(NSPEAKER_BUILD_DESKTOP_GUI)
    # -- Detect toolchain ---------------------------------------------------
    find_program(NSPEAKER_NPM NAMES npm npm.cmd)
    if(NOT NSPEAKER_NPM)
        message(FATAL_ERROR
            "NSPEAKER_BUILD_DESKTOP_GUI is ON but npm was not found.\n"
            "Install Node.js 20+ from https://nodejs.org/ "
            "or set -DNSPEAKER_BUILD_DESKTOP_GUI=OFF")
    endif()

    find_program(NSPEAKER_NPX NAMES npx npx.cmd)
    if(NOT NSPEAKER_NPX)
        message(FATAL_ERROR
            "NSPEAKER_BUILD_DESKTOP_GUI is ON but npx was not found.\n"
            "It should be installed alongside npm. Check your Node.js installation.")
    endif()

    find_program(NSPEAKER_CARGO NAMES cargo cargo.exe)
    if(NOT NSPEAKER_CARGO)
        message(FATAL_ERROR
            "NSPEAKER_BUILD_DESKTOP_GUI is ON but cargo was not found.\n"
            "Install Rust from https://rustup.rs/ "
            "or set -DNSPEAKER_BUILD_DESKTOP_GUI=OFF")
    endif()

    # -- Path definitions ---------------------------------------------------
    set(NSPEAKER_DESKTOP_DIR "${CMAKE_SOURCE_DIR}/apps/desktop")
    set(NSPEAKER_NPM_STAMP   "${CMAKE_BINARY_DIR}/desktop_npm_install.stamp")

    # -- Step 1: npm ci (stamp file avoids redundant runs) ------------------
    add_custom_command(
        OUTPUT  "${NSPEAKER_NPM_STAMP}"
        COMMAND "${NSPEAKER_NPM}" ci
        COMMAND "${CMAKE_COMMAND}" -E touch "${NSPEAKER_NPM_STAMP}"
        WORKING_DIRECTORY "${NSPEAKER_DESKTOP_DIR}"
        DEPENDS "${NSPEAKER_DESKTOP_DIR}/package-lock.json"
        COMMENT "Installing desktop app npm dependencies..."
    )

    # -- Step 2: npx tauri build --------------------------------------------
    add_custom_target(desktop_app
        COMMAND "${CMAKE_COMMAND}" -E env "NSPEAKER_HOSTD_PATH=$<TARGET_FILE:hostd>"
                "${NSPEAKER_NPX}" tauri build
        WORKING_DIRECTORY "${NSPEAKER_DESKTOP_DIR}"
        DEPENDS "${NSPEAKER_NPM_STAMP}"
        COMMENT "Building Tauri desktop application..."
    )

    # hostd must be built before desktop_app
    add_dependencies(desktop_app hostd)
endif()
