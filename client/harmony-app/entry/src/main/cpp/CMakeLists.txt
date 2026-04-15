# CMake build for HarmonyOS native library (networkspeaker entry module)
cmake_minimum_required(VERSION 3.5.0)
project(harmony_networkspeaker LANGUAGES CXX)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

# --- Opus codec (build from source) ---
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_STACK_PROTECTOR OFF CACHE BOOL "" FORCE)

add_subdirectory(../../../../../../third_party/opus ${CMAKE_CURRENT_BINARY_DIR}/third_party_opus)

# --- Shared library: libentry.so ---
add_library(entry SHARED
    napi_init.cpp
    ohaudio_sink.cpp
    # Client core
    ../../../../../../client/core/src/client_session.cpp
    ../../../../../../client/core/src/player_pipeline.cpp
    ../../../../../../client/core/src/receiver.cpp
    # Transport layer
    ../../../../../../libs/transport/src/audio_packet.cpp
    ../../../../../../libs/transport/src/jitter_buffer.cpp
    ../../../../../../libs/transport/src/udp_socket.cpp
    # Codec
    ../../../../../../libs/codec_opus/src/opus_codec.cpp
)

target_compile_features(entry PRIVATE cxx_std_20)

target_include_directories(entry PRIVATE
    ${NATIVERENDER_ROOT_PATH}
    ${NATIVERENDER_ROOT_PATH}/include
    ../../../../../../client/core/include
    ../../../../../../libs/audio_base/include
    ../../../../../../libs/transport/include
    ../../../../../../libs/codec_opus/include
)

target_link_libraries(entry PUBLIC
    libace_napi.z.so
    libohaudio.so
    libhilog_ndk.z.so
    opus
)
