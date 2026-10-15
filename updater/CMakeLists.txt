# Standalone ReDotUpdater (no Qt). Build separately from the main app:
#   cmake -S updater -B build/redot-updater -G "Visual Studio 17 2022" -A x64
cmake_minimum_required(VERSION 3.25)
project(ReDotUpdater VERSION 1.0.0 LANGUAGES CXX RC)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # static CRT: no redistributable needed
add_library(rdu_core STATIC core.cpp zip.cpp winhttp_net.cpp core.h)
target_compile_features(rdu_core PUBLIC cxx_std_20)
target_compile_definitions(rdu_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(rdu_core PUBLIC /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
target_link_libraries(rdu_core PUBLIC winhttp bcrypt version shell32 ole32 user32 advapi32)

add_executable(ReDotSignTool sign_tool.cpp)
target_link_libraries(ReDotSignTool PRIVATE rdu_core)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/public_key.h")
  add_executable(ReDotUpdater WIN32 main.cpp updater.rc)
  target_link_libraries(ReDotUpdater PRIVATE rdu_core comctl32 gdi32 dwmapi)
  target_link_options(ReDotUpdater PRIVATE /MANIFEST:NO /OPT:REF /OPT:ICF)
  add_executable(ReDotUpdaterTests tests.cpp)
  target_link_libraries(ReDotUpdaterTests PRIVATE rdu_core)
endif()
