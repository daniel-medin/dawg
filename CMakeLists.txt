cmake_minimum_required(VERSION 3.27)

project(dawg VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(DAWG_ENABLE_FFMPEG "Link against FFmpeg when available." ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets)
find_package(OpenCV REQUIRED COMPONENTS core imgproc video videoio)
find_package(JUCE CONFIG REQUIRED)
find_package(PkgConfig QUIET)

if(DAWG_ENABLE_FFMPEG AND PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavcodec libavformat libavutil libswscale libswresample)
endif()

add_executable(dawg WIN32
    src/main.cpp
    src/resources.qrc
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/app/AudioPoolService.cpp
    src/app/AudioPoolService.h
    src/app/PerformanceLogger.cpp
    src/app/PerformanceLogger.h
    src/app/PlayerController.cpp
    src/app/PlayerController.h
    src/app/TransportController.cpp
    src/app/TransportController.h
    src/core/audio/AudioDurationProbe.cpp
    src/core/audio/AudioDurationProbe.h
    src/core/audio/AudioEngine.cpp
    src/core/audio/AudioEngine.h
    src/core/audio/JuceAudioEngine.cpp
    src/core/audio/JuceAudioEngine.h
    src/core/audio/MciAudioEngine.cpp
    src/core/audio/MciAudioEngine.h
    src/core/audio/VideoAudioExtractor.cpp
    src/core/audio/VideoAudioExtractor.h
    src/core/tracking/MotionTracker.cpp
    src/core/tracking/MotionTracker.h
    src/core/tracking/TrackTypes.h
    src/core/render/D3D11RenderBackend.cpp
    src/core/render/D3D11RenderBackend.h
    src/core/render/IRenderBackend.h
    src/core/render/RenderService.cpp
    src/core/render/RenderService.h
    src/core/video/AnalysisFrameProvider.cpp
    src/core/video/AnalysisFrameProvider.h
    src/core/video/DecodedFrame.h
    src/core/video/FfmpegVideoDecoder.cpp
    src/core/video/FfmpegVideoDecoder.h
    src/core/video/OpenCvVideoDecoder.cpp
    src/core/video/OpenCvVideoDecoder.h
    src/core/video/VideoDecoder.h
    src/core/video/VideoFrame.h
    src/core/video/VideoFrameQueue.h
    src/core/video/VideoPlaybackService.cpp
    src/core/video/VideoPlaybackService.h
    src/ui/VideoCanvas.cpp
    src/ui/VideoCanvas.h
    src/ui/NativeVideoViewport.cpp
    src/ui/NativeVideoViewport.h
    src/ui/DebugOverlayWindow.cpp
    src/ui/DebugOverlayWindow.h
    src/ui/MixView.cpp
    src/ui/MixView.h
    src/ui/TimelineView.cpp
    src/ui/TimelineView.h
)

target_include_directories(dawg PRIVATE src)

target_link_libraries(dawg
    PRIVATE
        Qt6::Widgets
        ${OpenCV_LIBS}
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_events
)

if(WIN32)
    target_link_libraries(dawg PRIVATE Psapi Winmm d3d11 dxgi d3dcompiler)
endif()

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

if(WIN32)
    foreach(_qt_plugin_dir generic imageformats networkinformation platforms sqldrivers styles tls)
        add_custom_command(
            TARGET dawg
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_directory
                "$<TARGET_FILE_DIR:Qt6::Core>/../Qt6/plugins/${_qt_plugin_dir}"
                "$<TARGET_FILE_DIR:dawg>/${_qt_plugin_dir}"
            COMMENT "Copying Qt ${_qt_plugin_dir} plugins"
            VERBATIM
        )
    endforeach()
endif()
