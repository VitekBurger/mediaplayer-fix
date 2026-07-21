cmake_minimum_required(VERSION 3.20)
project(MediaPlayer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(JNI_INCLUDE_DIRS "${JNI_INCLUDE_DIRS}" CACHE PATH "JNI include directories")
if(NOT JNI_INCLUDE_DIRS)
    find_package(JNI REQUIRED)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec libavformat libavutil libswresample libswscale)

add_library(MediaPlayer SHARED
    mediaplayer.cpp
    ../../common/src/main/cpp/gl.c)

target_include_directories(MediaPlayer PRIVATE ${JNI_INCLUDE_DIRS})
target_link_libraries(MediaPlayer PRIVATE PkgConfig::FFMPEG dl)
set_target_properties(MediaPlayer PROPERTIES OUTPUT_NAME MediaPlayer)

install(TARGETS MediaPlayer LIBRARY DESTINATION .)
