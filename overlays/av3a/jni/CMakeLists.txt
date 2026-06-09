#
# AV3A 版 CMake：链接 libarcdav3a 及 AVS3 音频伴生共享库。
#
cmake_minimum_required(VERSION 3.21.0 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 11)

project(libffmpegJNI C CXX)

set(ffmpeg_location "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg")
set(ffmpeg_binaries "${ffmpeg_location}/android-libs/${ANDROID_ABI}")

foreach(ffmpeg_lib avutil swresample avcodec swscale av3a_binaural_render AVS3AudioDec)
 set(ffmpeg_lib_filename lib${ffmpeg_lib}.so)
 set(ffmpeg_lib_file_path ${ffmpeg_binaries}/${ffmpeg_lib_filename})
 add_library(
 ${ffmpeg_lib}
 SHARED
 IMPORTED)
 set_target_properties(
 ${ffmpeg_lib} PROPERTIES
 IMPORTED_LOCATION
 ${ffmpeg_lib_file_path})
endforeach()

include_directories(${ffmpeg_location})

find_library(android_log_lib log)

add_library(ffmpegJNI
 SHARED
 ffmpeg_jni.cc)

target_link_libraries(ffmpegJNI
 PRIVATE android
 PRIVATE m
 PRIVATE swresample
 PRIVATE avcodec
 PRIVATE avutil
 PRIVATE swscale
 PRIVATE av3a_binaural_render
 PRIVATE AVS3AudioDec
 PRIVATE ${android_log_lib})

if(ANDROID_ABI STREQUAL "arm64-v8a")
 target_link_options(ffmpegJNI PRIVATE "-Wl,-Bsymbolic")
endif()
