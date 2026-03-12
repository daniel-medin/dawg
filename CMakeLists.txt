cmake_minimum_required(VERSION 3.27)

project(dawg VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(DAWG_ENABLE_FFMPEG "Link against FFmpeg when available." ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets)
find_package(OpenCV REQUIRED COMPONENTS core imgproc video videoio)
find_package(PkgConfig QUIET)

if(DAWG_ENABLE_FFMPEG AND PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavcodec libavformat libavutil libswscale)
endif()

add_executable(dawg WIN32
    src/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/app/PlayerController.cpp
    src/app/PlayerController.h
    src/core/tracking/MotionTracker.cpp
    src/core/tracking/MotionTracker.h
    src/core/tracking/TrackTypes.h
    src/core/video/DecodedFrame.h
    src/core/video/OpenCvVideoDecoder.cpp
    src/core/video/OpenCvVideoDecoder.h
    src/core/video/VideoDecoder.h
    src/ui/VideoCanvas.cpp
    src/ui/VideoCanvas.h
)

target_include_directories(dawg PRIVATE src)

target_link_libraries(dawg
    PRIVATE
        Qt6::Widgets
        ${OpenCV_LIBS}
)

if(TARGET PkgConfig::FFMPEG)
    target_link_libraries(dawg PRIVATE PkgConfig::FFMPEG)
    target_compile_definitions(dawg PRIVATE DAWG_HAS_FFMPEG=1)
else()
    target_compile_definitions(dawg PRIVATE DAWG_HAS_FFMPEG=0)
endif()

if(MSVC)
    target_compile_options(dawg PRIVATE /W4 /permissive-)
else()
    target_compile_options(dawg PRIVATE -Wall -Wextra -Wpedantic)
endif()

