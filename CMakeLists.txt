cmake_minimum_required(VERSION 3.25)
project(ReDot VERSION 1.0.1 LANGUAGES CXX RC)
# One version for the executable resource, the About/Qt application version and redot-version.json (read by ReDotUpdater).
add_compile_definitions(REDOT_VERSION="${PROJECT_VERSION}")

if(NOT WIN32)
  message(FATAL_ERROR "ReDot requires Windows 10 version 1903 or later.")
endif()

add_executable(ReDot WIN32 src/main.cpp src/recording.cpp src/audio_capture.cpp src/replay.cpp src/recording.h src/audio_capture.h src/replay.h src/logging.h src/diagnostics.h src/timeline.h src/borderless_access.h src/video_config.h src/settings_store.h src/workspace_ui.inl src/clip_library.h src/clip_track_names.h src/audio_device_catalog.h)
target_include_directories(ReDot PRIVATE third_party/NVIDIA/VideoCodecSDK/include)
# Version manifest next to ReDot.exe. The public v1.0.0 ZIP embedded 0.3.0 and had no manifest; ReDotUpdater relies on this file from v1.0.1 on.
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:ReDot>/redot-version.json"
  CONTENT "{\"product\":\"ReDot\",\"version\":\"${PROJECT_VERSION}\",\"platform\":\"win-x64\",\"updaterProtocol\":1}
")
target_compile_features(ReDot PRIVATE cxx_std_20)
target_compile_definitions(ReDot PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_link_libraries(ReDot PRIVATE d3d11 dxgi d3dcompiler windowsapp mf mfplat mfreadwrite mfuuid ole32 oleaut32 shell32 mmdevapi avrt propsys comctl32 wtsapi32 psapi uxtheme dwmapi advapi32 winmm)
if(MSVC)
  target_compile_options(ReDot PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
endif()


# Matroska (MKV) container: pinned libwebm mkvmuxer/mkvparser (BSD-3-Clause, vendored unmodified) + ReDot's writer.
set(REDOT_LIBWEBM_SOURCES
  third_party/libwebm/mkvmuxer/mkvmuxer.cc third_party/libwebm/mkvmuxer/mkvmuxerutil.cc third_party/libwebm/mkvmuxer/mkvwriter.cc
  third_party/libwebm/mkvparser/mkvparser.cc third_party/libwebm/mkvparser/mkvreader.cc)
add_library(ReDotMatroska STATIC src/mkv_writer.cpp src/mkv_writer.h ${REDOT_LIBWEBM_SOURCES})
target_include_directories(ReDotMatroska PUBLIC src third_party/libwebm)
target_compile_features(ReDotMatroska PUBLIC cxx_std_20)
target_compile_definitions(ReDotMatroska PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
if(MSVC)
  target_compile_options(ReDotMatroska PRIVATE /EHsc /utf-8 /Zc:__cplusplus)
  set_source_files_properties(${REDOT_LIBWEBM_SOURCES} PROPERTIES COMPILE_OPTIONS "/W1")
  set_source_files_properties(src/mkv_writer.cpp PROPERTIES COMPILE_OPTIONS "/W4;/permissive-")
endif()
target_link_libraries(ReDot PRIVATE ReDotMatroska)
add_custom_command(TARGET ReDot POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ReDot>/licenses/libwebm"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/libwebm/LICENSE.TXT"
    "${CMAKE_SOURCE_DIR}/third_party/libwebm/PATENTS.TXT" "${CMAKE_SOURCE_DIR}/third_party/libwebm/AUTHORS.TXT"
    "${CMAKE_SOURCE_DIR}/third_party/libwebm/README-ReDot.txt" "$<TARGET_FILE_DIR:ReDot>/licenses/libwebm"
  COMMENT "Including libwebm license notices" VERBATIM)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/third_party/Qt/6.8.3/msvc2022_64")
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Quick QuickWidgets QuickControls2 Multimedia ShaderTools Concurrent)
add_custom_target(ReDotPlaybackRuntime
  COMMAND py -3 "${CMAKE_SOURCE_DIR}/tools/build_qt_audio.py" "$<CONFIG>"
  # Refresh the DLL even when the application itself needs no relink. A later
  # windeployqt invocation is followed by the target's POST_BUILD repair copy.
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/build/qt-output-runtime-ninja/bin/Qt6Multimedia$<$<CONFIG:Debug>:d>.dll"
    "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Qt6Multimedia$<$<CONFIG:Debug>:d>.dll"
  COMMENT "Building cached Qt WASAPI playback padding repair" VERBATIM)
function(redot_playback_runtime target)
  add_dependencies(${target} ReDotPlaybackRuntime)
  # Must run AFTER windeployqt: only this core DLL differs from the pinned SDK.
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/build/qt-output-runtime-ninja/bin/Qt6Multimedia$<$<CONFIG:Debug>:d>.dll"
      "$<TARGET_FILE_DIR:${target}>/Qt6Multimedia$<$<CONFIG:Debug>:d>.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses/Qt/changes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/tools/build_qt_audio.py"
      "${CMAKE_SOURCE_DIR}/tools/qt_playback_trace.h" "${CMAKE_SOURCE_DIR}/docs/qt-audio-padding.patch" "$<TARGET_FILE_DIR:${target}>/licenses/Qt/changes"
    VERBATIM)
endfunction()
# Brand resources are rendered from one authored SVG using existing Qt Gui.
add_executable(ReDotBrandAssets tools/brand_assets.cpp)
target_compile_features(ReDotBrandAssets PRIVATE cxx_std_20)
target_link_libraries(ReDotBrandAssets PRIVATE Qt6::Gui)
set(BRAND_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/brand")
set(brand_outputs)
foreach(state idle replay recording)
  list(APPEND brand_outputs "${BRAND_DIRECTORY}/${state}.ico" "${BRAND_DIRECTORY}/${state}.svg")
  foreach(size 16 20 24 32 40 44 48 50 64 96 128 150 256)
    list(APPEND brand_outputs "${BRAND_DIRECTORY}/${state}-${size}.png")
  endforeach()
endforeach()
add_custom_command(OUTPUT ${brand_outputs}
  COMMAND ${CMAKE_COMMAND} -E env "PATH=${Qt6_DIR}/../../../bin" "QT_PLUGIN_PATH=${Qt6_DIR}/../../../plugins" $<TARGET_FILE:ReDotBrandAssets> "${CMAKE_SOURCE_DIR}/branding/redot.svg" "${BRAND_DIRECTORY}"
  DEPENDS ReDotBrandAssets "${CMAKE_SOURCE_DIR}/branding/redot.svg" VERBATIM)
add_custom_target(ReDotBrandResources DEPENDS ${brand_outputs})
configure_file(src/redot.rc.in "${CMAKE_CURRENT_BINARY_DIR}/redot.rc" @ONLY)
target_sources(ReDot PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/redot.rc")
add_dependencies(ReDot ReDotBrandResources)
set(CMAKE_AUTOMOC ON)
option(REDOT_BUILD_GLASS_LAB "Build the standalone material approval prototype" OFF)
if(REDOT_BUILD_GLASS_LAB)
  add_subdirectory(lab/glass)
endif()

# One resource/shader definition for shipping and verification. qsb 6.8 emits
# unescaped spaces in MSBuild depfiles; this include-free shader uses an explicit
# quoted source dependency instead, without modifying the Qt distribution.
set(material_shader "${CMAKE_CURRENT_BINARY_DIR}/material/desktop.frag.qsb")
add_custom_command(OUTPUT "${material_shader}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/material"
  COMMAND Qt6::qsb --glsl "100es,120,150" --hlsl 50 --msl 12 -o "${material_shader}" "${CMAKE_SOURCE_DIR}/qml/desktop.frag"
  DEPENDS "${CMAKE_SOURCE_DIR}/qml/desktop.frag" VERBATIM)
function(redot_frontend_resources target)
  add_dependencies(${target} ReDotBrandResources)
  qt_add_resources(${target} "${target}_brand" PREFIX "/ReDot" BASE "${BRAND_DIRECTORY}"
    FILES "${BRAND_DIRECTORY}/idle.ico" "${BRAND_DIRECTORY}/replay.ico" "${BRAND_DIRECTORY}/recording.ico" "${BRAND_DIRECTORY}/idle-64.png")
  qt_add_resources(${target} "${target}_frontend" PREFIX "/PandaClip" BASE "${CMAKE_SOURCE_DIR}/qml"
    FILES qml/Media.qml qml/GlassPanel.qml qml/WorkspaceGlass.qml qml/GlassTokens.qml
          qml/GlassButton.qml qml/GlassChoice.qml qml/GlassSlider.qml qml/design-system.json qml/controls.qss)
  qt_add_resources(${target} "${target}_material" PREFIX "/PandaClip" BASE "${CMAKE_CURRENT_BINARY_DIR}/material" FILES "${material_shader}")
endfunction()
set_target_properties(ReDot PROPERTIES AUTOMOC ON)
target_sources(ReDot PRIVATE src/frontend_qt.cpp src/frontend_qt.h src/frontend_bridge.h src/scene_model.cpp src/scene_model.h src/scene_editor.cpp src/scene_editor.h src/application_picker.cpp src/application_picker.h src/scene_runtime.cpp src/scene_runtime.h src/scene_audio.cpp src/scene_audio.h)
target_compile_definitions(ReDot PRIVATE QT_NO_KEYWORDS)
target_link_libraries(ReDot PRIVATE Qt6::Widgets Qt6::Quick Qt6::QuickWidgets Qt6::QuickControls2 Qt6::Multimedia Qt6::Concurrent)
redot_frontend_resources(ReDot)
add_custom_command(TARGET ReDot POST_BUILD
  COMMAND "${Qt6_DIR}/../../../bin/windeployqt.exe" --no-compiler-runtime
    --qmldir "${CMAKE_SOURCE_DIR}/qml" "$<TARGET_FILE:ReDot>"
  COMMAND ${CMAKE_COMMAND} "-DDEPLOY_DIRECTORY=$<TARGET_FILE_DIR:ReDot>" -P "${CMAKE_SOURCE_DIR}/tools/qt_deployment_cleanup.cmake"
  COMMENT "Deploying dynamic Qt runtime and QML modules alongside ReDot")
add_custom_command(TARGET ReDot POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/README.md" "$<TARGET_FILE_DIR:ReDot>/README.md"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/docs/QT-NOTICE.txt" "$<TARGET_FILE_DIR:ReDot>/QT-NOTICE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/docs" "$<TARGET_FILE_DIR:ReDot>/docs"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/third_party/Qt/notices" "$<TARGET_FILE_DIR:ReDot>/licenses/Qt/notices"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/third_party/Qt/sources" "$<TARGET_FILE_DIR:ReDot>/licenses/Qt/sources"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${BRAND_DIRECTORY}" "$<TARGET_FILE_DIR:ReDot>/branding"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/branding/redot.svg" "$<TARGET_FILE_DIR:ReDot>/branding/redot.svg"
  COMMENT "Including Qt license notices, sources and ReDot brand assets")
install(TARGETS ReDot RUNTIME DESTINATION .)
redot_playback_runtime(ReDot)

option(REDOT_BUILD_REPLAY_STRESS "Build replay buffer and MP4 verification tools" OFF)
if(REDOT_BUILD_REPLAY_STRESS)
  add_executable(ReDotClipSoundTest tests/clip_sound_test.cpp)
  target_include_directories(ReDotClipSoundTest PRIVATE src)
  target_compile_features(ReDotClipSoundTest PRIVATE cxx_std_20)
  target_compile_definitions(ReDotClipSoundTest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(ReDotClipSoundTest PRIVATE /utf-8)
  target_link_libraries(ReDotClipSoundTest PRIVATE winmm)
  add_executable(ReDotBrandTest tests/branding_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/redot.rc")
  target_include_directories(ReDotBrandTest PRIVATE src)
  target_compile_features(ReDotBrandTest PRIVATE cxx_std_20)
  target_compile_definitions(ReDotBrandTest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(ReDotBrandTest PRIVATE /utf-8)
  target_link_libraries(ReDotBrandTest PRIVATE Qt6::Widgets)
  redot_frontend_resources(ReDotBrandTest)
  add_executable(ReDotScenesTest tests/scenes_test.cpp src/scene_model.cpp src/scene_model.h src/scene_editor.cpp src/scene_editor.h src/application_picker.cpp src/application_picker.h src/scene_runtime.cpp src/recording.cpp src/audio_capture.cpp src/replay.cpp)
  target_include_directories(ReDotScenesTest PRIVATE src third_party/NVIDIA/VideoCodecSDK/include)
  target_compile_features(ReDotScenesTest PRIVATE cxx_std_20)
  target_compile_definitions(ReDotScenesTest PRIVATE QT_NO_KEYWORDS UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  get_target_property(scenes_libraries ReDot LINK_LIBRARIES)
  target_link_libraries(ReDotScenesTest PRIVATE ${scenes_libraries})
  target_compile_options(ReDotScenesTest PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
  redot_frontend_resources(ReDotScenesTest)
  add_executable(ReDotSceneIntegration tests/scene_integration.cpp src/scene_audio.cpp src/scene_model.cpp src/application_picker.cpp src/scene_runtime.cpp src/recording.cpp src/audio_capture.cpp src/replay.cpp)
  target_include_directories(ReDotSceneIntegration PRIVATE src third_party/NVIDIA/VideoCodecSDK/include)
  target_compile_features(ReDotSceneIntegration PRIVATE cxx_std_20)
  target_compile_definitions(ReDotSceneIntegration PRIVATE REDOT_UI_VERIFICATION REDOT_TEST_FIXTURE QT_NO_KEYWORDS UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(ReDotSceneIntegration PRIVATE ${scenes_libraries})
  target_compile_options(ReDotSceneIntegration PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
  add_executable(ReDotScenePerformance tests/scene_performance.cpp src/scene_audio.cpp src/scene_model.cpp src/application_picker.cpp src/scene_runtime.cpp src/recording.cpp src/audio_capture.cpp src/replay.cpp)
  target_include_directories(ReDotScenePerformance PRIVATE src third_party/NVIDIA/VideoCodecSDK/include)
  target_compile_features(ReDotScenePerformance PRIVATE cxx_std_20)
  target_compile_definitions(ReDotScenePerformance PRIVATE REDOT_UI_VERIFICATION REDOT_TEST_FIXTURE QT_NO_KEYWORDS UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(ReDotScenePerformance PRIVATE ${scenes_libraries})
  target_compile_options(ReDotScenePerformance PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
  add_executable(ReDotLifecycleTest tests/final_lifecycle.cpp src/recording.cpp src/audio_capture.cpp src/replay.cpp)
  add_executable(ReDotMuxMemoryProbe tests/mp4_retention_probe.cpp)
  target_compile_features(ReDotMuxMemoryProbe PRIVATE cxx_std_20)
  target_compile_definitions(ReDotMuxMemoryProbe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(ReDotMuxMemoryProbe PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_link_libraries(ReDotMuxMemoryProbe PRIVATE mfplat mfreadwrite mfuuid ole32 psapi)
  target_include_directories(ReDotLifecycleTest PRIVATE src third_party/NVIDIA/VideoCodecSDK/include)
  target_compile_features(ReDotLifecycleTest PRIVATE cxx_std_20)
  target_compile_definitions(ReDotLifecycleTest PRIVATE REDOT_UI_VERIFICATION UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(ReDotLifecycleTest PRIVATE d3d11 dxgi d3dcompiler windowsapp mfplat mfreadwrite mfuuid ole32 oleaut32 mmdevapi avrt propsys psapi)
  target_compile_options(ReDotLifecycleTest PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
  add_executable(ReDotPlaybackAudioProbe tests/playback_audio_probe.cpp)
  target_compile_features(ReDotPlaybackAudioProbe PRIVATE cxx_std_20)
  target_link_libraries(ReDotPlaybackAudioProbe PRIVATE Qt6::Multimedia)
  redot_playback_runtime(ReDotPlaybackAudioProbe)
  add_executable(ReDotFocusPolicyProbe tests/focus_policy_probe.cpp)
  target_include_directories(ReDotFocusPolicyProbe PRIVATE src)
  target_compile_features(ReDotFocusPolicyProbe PRIVATE cxx_std_20)
  target_compile_definitions(ReDotFocusPolicyProbe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(ReDotFocusPolicyProbe PRIVATE Qt6::Widgets dwmapi comctl32 advapi32)
  # Optional pre-edit policy measurement, never a production dependency.
  if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/baseline/desktop_backdrop.h")
    add_executable(ReDotFocusPolicyBaseline tests/focus_policy_probe.cpp)
    target_include_directories(ReDotFocusPolicyBaseline PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/baseline" src)
    target_compile_features(ReDotFocusPolicyBaseline PRIVATE cxx_std_20)
    target_compile_definitions(ReDotFocusPolicyBaseline PRIVATE REDOT_BASELINE_FOCUS_POLICY UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(ReDotFocusPolicyBaseline PRIVATE Qt6::Widgets dwmapi comctl32 advapi32)
  endif()
  add_executable(ReDotAudioContinuityTest tests/audio_continuity_test.cpp)
  target_include_directories(ReDotAudioContinuityTest PRIVATE src)
  target_compile_features(ReDotAudioContinuityTest PRIVATE cxx_std_20)
  target_compile_definitions(ReDotAudioContinuityTest PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(ReDotAudioContinuityTest PRIVATE ole32)
  add_executable(ReDotAudioMeterTest tests/audio_meter_test.cpp)
  target_include_directories(ReDotAudioMeterTest PRIVATE src)
  target_compile_features(ReDotAudioMeterTest PRIVATE cxx_std_20)
  get_target_property(redot_sources ReDot SOURCES)
  # Qt resource sources belong to their target's generation rule. Cloning
  # shipping qrc files races rcc during parallel builds and duplicates resources.
  list(FILTER redot_sources EXCLUDE REGEX "qrc_.*\\.cpp$")
  add_executable(ReDotQuietApp WIN32 ${redot_sources} tests/quiet_app_source.inl)
  add_dependencies(ReDotQuietApp ReDotBrandResources)
  target_include_directories(ReDotQuietApp PRIVATE third_party/NVIDIA/VideoCodecSDK/include)
  target_compile_features(ReDotQuietApp PRIVATE cxx_std_20)
  target_compile_definitions(ReDotQuietApp PRIVATE QT_NO_KEYWORDS)
  redot_frontend_resources(ReDotQuietApp)
  add_executable(ReDotQtQuietLauncher tests/qt_quiet_launcher.cpp)
  target_compile_features(ReDotQtQuietLauncher PRIVATE cxx_std_20)
  target_link_libraries(ReDotQtQuietLauncher PRIVATE dwmapi ole32 oleaut32 uiautomationcore windowsapp d3d11 dxgi windowscodecs)
  target_compile_definitions(ReDotQtQuietLauncher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  add_executable(ReDotBackdropProbe tests/backdrop_probe.cpp)
  target_include_directories(ReDotBackdropProbe PRIVATE src)
  target_compile_features(ReDotBackdropProbe PRIVATE cxx_std_20)
  target_compile_definitions(ReDotBackdropProbe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(ReDotBackdropProbe PRIVATE Qt6::Widgets Qt6::QuickWidgets dwmapi advapi32 comctl32)
  qt_add_resources(ReDotBackdropProbe "backdrop_probe" PREFIX "/PandaClip" BASE qml FILES qml/BackdropProbe.qml)
  target_compile_definitions(ReDotQuietApp PRIVATE REDOT_TEST_FIXTURE REDOT_UI_VERIFICATION UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  get_target_property(redot_libraries ReDot LINK_LIBRARIES)
  target_link_libraries(ReDotQuietApp PRIVATE ${redot_libraries})
  redot_playback_runtime(ReDotQuietApp)
  target_compile_options(ReDotQuietApp PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
  add_executable(ReDotQuietMediaFixture tests/quiet_media_fixture.cpp src/recording.cpp src/audio_capture.cpp src/replay.cpp)
  target_include_directories(ReDotQuietMediaFixture PRIVATE src third_party/NVIDIA/VideoCodecSDK/include)
  target_compile_features(ReDotQuietMediaFixture PRIVATE cxx_std_20)
  target_compile_definitions(ReDotQuietMediaFixture PRIVATE REDOT_TEST_FIXTURE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(ReDotQuietMediaFixture PRIVATE d3d11 dxgi d3dcompiler mfplat mfreadwrite mfuuid ole32 mmdevapi avrt propsys)
  target_compile_options(ReDotQuietMediaFixture PRIVATE /W4 /permissive- /EHsc /utf-8)
  add_executable(ReDotM6ServicesTest tests/m6_services_test.cpp)
  target_include_directories(ReDotM6ServicesTest PRIVATE src)
  target_compile_features(ReDotM6ServicesTest PRIVATE cxx_std_20)
  target_compile_definitions(ReDotM6ServicesTest PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(ReDotM6ServicesTest PRIVATE mfplay mfplat mfreadwrite mfuuid ole32 oleaut32 propsys)
  target_compile_options(ReDotM6ServicesTest PRIVATE /W4 /permissive- /EHsc /utf-8)
  add_executable(ReDotAudioInputProbe tests/audio_input_probe.cpp src/audio_capture.cpp src/recording.cpp src/replay.cpp)
  target_include_directories(ReDotAudioInputProbe PRIVATE src third_party/NVIDIA/VideoCodecSDK/include)
  target_compile_features(ReDotAudioInputProbe PRIVATE cxx_std_20)
  target_compile_definitions(ReDotAudioInputProbe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(ReDotAudioInputProbe PRIVATE windowsapp d3d11 dxgi d3dcompiler mfplat mfreadwrite mfuuid ole32 mmdevapi avrt propsys)
  if(MSVC)
    target_compile_options(ReDotAudioInputProbe PRIVATE /W4 /permissive- /EHsc /utf-8)
  endif()
  add_executable(ReDotBorderPolicyTest tests/border_policy_test.cpp)
  target_include_directories(ReDotBorderPolicyTest PRIVATE src)
  target_compile_features(ReDotBorderPolicyTest PRIVATE cxx_std_20)
  target_compile_definitions(ReDotBorderPolicyTest PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(ReDotBorderPolicyTest PRIVATE windowsapp)
  if(MSVC)
    target_compile_options(ReDotBorderPolicyTest PRIVATE /W4 /permissive- /EHsc /utf-8)
  endif()
  add_executable(ReDotLoggingStress tests/logging_stress.cpp)
  target_include_directories(ReDotLoggingStress PRIVATE src)
  target_compile_features(ReDotLoggingStress PRIVATE cxx_std_20)
  target_compile_definitions(ReDotLoggingStress PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(MSVC)
    target_compile_options(ReDotLoggingStress PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
  endif()
  add_executable(ReDotAacDelayProbe tests/aac_delay_probe.cpp src/recording.cpp src/audio_capture.cpp src/replay.cpp)
  target_include_directories(ReDotAacDelayProbe PRIVATE src third_party/NVIDIA/VideoCodecSDK/include)
  target_compile_features(ReDotAacDelayProbe PRIVATE cxx_std_20)
  target_compile_definitions(ReDotAacDelayProbe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(ReDotAacDelayProbe PRIVATE d3d11 dxgi d3dcompiler mfplat mfreadwrite mfuuid ole32 mmdevapi avrt propsys)
  if(MSVC)
    target_compile_options(ReDotAacDelayProbe PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
  endif()
  add_executable(ReDotReplayBufferStress src/replay.cpp tests/replay_buffer_stress.cpp)
  target_include_directories(ReDotReplayBufferStress PRIVATE src)
  target_compile_features(ReDotReplayBufferStress PRIVATE cxx_std_20)
  target_compile_definitions(ReDotReplayBufferStress PRIVATE REDOT_REPLAY_TEST_ALLOC_FAILURE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(ReDotReplayBufferStress PRIVATE mfplat mfreadwrite mfuuid ole32)
  if(MSVC)
    target_compile_options(ReDotReplayBufferStress PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
  endif()

  add_executable(ReDotReplaySyncVerify tests/replay_sync_verify.cpp)
  target_compile_features(ReDotReplaySyncVerify PRIVATE cxx_std_20)
  target_compile_definitions(ReDotReplaySyncVerify PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(ReDotReplaySyncVerify PRIVATE mfplat mfreadwrite mfuuid ole32)
  if(MSVC)
    target_compile_options(ReDotReplaySyncVerify PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
  endif()

  add_executable(ReDotMkvCrashTest tests/mkv_crash_test.cpp src/recording.cpp src/audio_capture.cpp src/replay.cpp)
  target_include_directories(ReDotMkvCrashTest PRIVATE src third_party/NVIDIA/VideoCodecSDK/include)
  target_compile_features(ReDotMkvCrashTest PRIVATE cxx_std_20)
  target_compile_definitions(ReDotMkvCrashTest PRIVATE REDOT_UI_VERIFICATION UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(ReDotMkvCrashTest PRIVATE d3d11 dxgi d3dcompiler windowsapp mfplat mfreadwrite mfuuid ole32 oleaut32 mmdevapi avrt propsys psapi)
  target_compile_options(ReDotMkvCrashTest PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)

  add_executable(ReDotMkvPerformance tests/mkv_performance.cpp src/recording.cpp src/audio_capture.cpp src/replay.cpp)
  target_include_directories(ReDotMkvPerformance PRIVATE src third_party/NVIDIA/VideoCodecSDK/include)
  target_compile_features(ReDotMkvPerformance PRIVATE cxx_std_20)
  target_compile_definitions(ReDotMkvPerformance PRIVATE REDOT_UI_VERIFICATION UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(ReDotMkvPerformance PRIVATE d3d11 dxgi d3dcompiler windowsapp mfplat mfreadwrite mfuuid ole32 oleaut32 mmdevapi avrt propsys psapi)
  target_compile_options(ReDotMkvPerformance PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)

  add_executable(ReDotStartStopStress tests/start_stop_stress.cpp src/recording.cpp src/audio_capture.cpp src/replay.cpp)
  target_include_directories(ReDotStartStopStress PRIVATE src third_party/NVIDIA/VideoCodecSDK/include)
  target_compile_features(ReDotStartStopStress PRIVATE cxx_std_20)
  target_compile_definitions(ReDotStartStopStress PRIVATE REDOT_UI_VERIFICATION UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(ReDotStartStopStress PRIVATE d3d11 dxgi d3dcompiler windowsapp mfplat mfreadwrite mfuuid ole32 oleaut32 mmdevapi avrt propsys psapi)
  target_compile_options(ReDotStartStopStress PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)

  add_executable(ReDotMkvPrototype tests/mkv_prototype.cpp)
  target_compile_features(ReDotMkvPrototype PRIVATE cxx_std_20)
  target_compile_definitions(ReDotMkvPrototype PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
  target_link_libraries(ReDotMkvPrototype PRIVATE ReDotMatroska mfplat mfreadwrite mfuuid mf wmcodecdspuuid strmiids ole32)
  if(MSVC)
    target_compile_options(ReDotMkvPrototype PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
  endif()

  # Every verification target that compiles the recorder needs the Matroska writer.
  foreach(redot_recorder_target ReDotScenesTest ReDotSceneIntegration ReDotScenePerformance ReDotLifecycleTest
      ReDotQuietMediaFixture ReDotAudioInputProbe ReDotAacDelayProbe ReDotQuietApp ReDotM6ServicesTest ReDotMkvCrashTest ReDotMkvPerformance ReDotStartStopStress)
    if(TARGET ${redot_recorder_target})
      target_link_libraries(${redot_recorder_target} PRIVATE ReDotMatroska)
    endif()
  endforeach()
endif()
