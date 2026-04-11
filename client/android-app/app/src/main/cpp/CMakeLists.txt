cmake_minimum_required(VERSION 3.22)
project(networkspeaker_android LANGUAGES CXX)

set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_STACK_PROTECTOR OFF CACHE BOOL "" FORCE)

add_subdirectory(../../../../../../third_party/opus third_party_opus)

add_library(networkspeaker_android SHARED
    native_bridge.cpp
    ../../../../../../client/core/src/client_session.cpp
    ../../../../../../client/core/src/player_pipeline.cpp
    ../../../../../../client/core/src/receiver.cpp
    ../../../../../../libs/transport/src/audio_packet.cpp
    ../../../../../../libs/transport/src/jitter_buffer.cpp
    ../../../../../../libs/transport/src/udp_socket.cpp
    ../../../../../../libs/codec_opus/src/opus_codec.cpp)

target_compile_features(networkspeaker_android PRIVATE cxx_std_20)
target_include_directories(networkspeaker_android PRIVATE
    ../../../../../../client/core/include
    ../../../../../../libs/audio_base/include
    ../../../../../../libs/transport/include
    ../../../../../../libs/codec_opus/include)
find_library(log-lib log)
target_link_libraries(networkspeaker_android PRIVATE ${log-lib} opus)
