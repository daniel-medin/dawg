cmake_minimum_required(VERSION 3.27)

project(dawg VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(DAWG_ENABLE_FFMPEG "Link against FFmpeg when available." ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Quick QuickControls2 Qml CoreTools)
find_package(OpenCV REQUIRED COMPONENTS core imgproc video videoio)
find_package(JUCE CONFIG REQUIRED)
find_package(PkgConfig QUIET)

if(DAWG_ENABLE_FFMPEG AND PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET libavcodec libavformat libavutil libswscale libswresample)
endif()

add_executable(dawg WIN32
    src/main.cpp
    src/app_icon.rc
    src/resources.qrc
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/app/AudioPlaybackCoordinator.cpp
    src/app/AudioPlaybackCoordinator.h
    src/app/ActionRegistry.cpp
    src/app/ActionRegistry.h
    src/app/AudioPoolQuickController.cpp
    src/app/AudioPoolQuickController.h
    src/app/ContextMenuController.cpp
    src/app/ContextMenuController.h
    src/app/DialogController.cpp
    src/app/DialogController.h
    src/app/FilePickerController.cpp
    src/app/FilePickerController.h
    src/app/AudioPoolService.cpp
    src/app/AudioPoolService.h
    src/app/ClipEditorSession.cpp
    src/app/ClipEditorSession.h
    src/app/DebugUiController.cpp
    src/app/DebugUiController.h
    src/app/MainWindowActions.cpp
    src/app/MainWindowActions.h
    src/app/MediaImportController.cpp
    src/app/MediaImportController.h
    src/app/MixStateStore.cpp
    src/app/MixStateStore.h
    src/app/NodeController.cpp
    src/app/NodeController.h
    src/app/PanelLayoutController.cpp
    src/app/PanelLayoutController.h
    src/app/TimelineLayoutService.cpp
    src/app/TimelineLayoutService.h
    src/app/ProjectDocument.cpp
    src/app/ProjectDocument.h
    src/app/ProjectWindowController.cpp
    src/app/ProjectWindowController.h
    src/app/PerformanceLogger.cpp
    src/app/PerformanceLogger.h
    src/app/PlayerController.cpp
    src/app/PlayerController.h
    src/app/ProjectSessionAdapter.cpp
    src/app/ProjectSessionAdapter.h
    src/app/SelectionController.cpp
    src/app/SelectionController.h
    src/app/ShellLayoutController.cpp
    src/app/ShellLayoutController.h
    src/app/ShellOverlayController.cpp
    src/app/ShellOverlayController.h
    src/app/TrackEditService.cpp
    src/app/TrackEditService.h
    src/app/TransportController.cpp
    src/app/TransportController.h
    src/app/VideoPlaybackCoordinator.cpp
    src/app/VideoPlaybackCoordinator.h
    src/app/WindowChromeController.cpp
    src/app/WindowChromeController.h
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
    src/ui/VideoViewportQuickController.cpp
    src/ui/VideoViewportQuickController.h
    src/ui/VideoViewportQuickItem.cpp
    src/ui/VideoViewportQuickItem.h
    src/ui/NativeVideoViewport.cpp
    src/ui/NativeVideoViewport.h
    src/ui/DebugOverlayWindow.cpp
    src/ui/DebugOverlayWindow.h
    src/ui/ClipEditorTypes.h
    src/ui/ClipEditorQuickController.cpp
    src/ui/ClipEditorQuickController.h
    src/ui/ClipWaveformQuickItem.cpp
    src/ui/ClipWaveformQuickItem.h
    src/ui/MixTypes.h
    src/ui/MixQuickController.cpp
    src/ui/MixQuickController.h
    src/ui/QuickEngineSupport.cpp
    src/ui/QuickEngineSupport.h
    src/ui/TimelineQuickController.cpp
    src/ui/TimelineQuickController.h
    src/ui/TimelineThumbnailCache.cpp
    src/ui/TimelineThumbnailCache.h
    src/ui/TimelineTypes.h
    src/ui/VideoOverlayQuickItem.cpp
    src/ui/VideoOverlayQuickItem.h
)

target_include_directories(dawg PRIVATE src)

target_link_libraries(dawg
    PRIVATE
        Qt6::Widgets
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Qml
        ${OpenCV_LIBS}
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_events
)

if(WIN32)
    target_link_libraries(dawg PRIVATE Psapi Winmm d3d11 dxgi d3dcompiler Pdh)
endif()

if(TARGET PkgConfig::FFMPEG)
    target_link_libraries(dawg PRIVATE PkgConfig::FFMPEG)
    target_compile_definitions(dawg PRIVATE DAWG_HAS_FFMPEG=1)
else()
    target_compile_definitions(dawg PRIVATE DAWG_HAS_FFMPEG=0)
endif()

if(MSVC)
    target_compile_options(dawg PRIVATE /W4 /permissive- /FS)
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

    add_custom_command(
        TARGET dawg
        POST_BUILD
        COMMAND
            "$<TARGET_FILE:Qt6::windeployqt>"
            --dir "$<TARGET_FILE_DIR:dawg>"
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "$<TARGET_FILE:dawg>"
        COMMENT "Deploying Qt runtime and QML modules"
        VERBATIM
    )
endif()
