cmake_minimum_required(VERSION 3.16)
project(Hexahedron CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- portable layers (no platform dependencies) ---------------------------
add_library(hx_core STATIC src/core/game.cpp)
target_include_directories(hx_core PUBLIC src)

add_library(hx_game STATIC
  src/game/app.cpp
  src/game/assets.cpp
  src/game/autopilot.cpp
  src/game/font.cpp
  src/game/help_scenes.cpp
  src/game/synth.cpp)
target_link_libraries(hx_game PUBLIC hx_core)
find_package(Threads REQUIRED)
target_link_libraries(hx_game PUBLIC Threads::Threads)

# --- tests -----------------------------------------------------------------
enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests hx_core)
add_test(NAME core_tests COMMAND core_tests)

add_executable(asset_preview tools/asset_preview.cpp)
target_link_libraries(asset_preview hx_game)

# --- platform backend -------------------------------------------------------
# sdl3_gl: PC (Linux/Windows), also the base for Android (with HX_GLES).
# ps3:     PSL1GHT backend, planned (see src/platform/ps3/README.md).
set(HX_PLATFORM "sdl3_gl" CACHE STRING "Platform backend: sdl3_gl | ps3")
if(HX_PLATFORM STREQUAL "sdl3_gl")
  find_package(SDL3 CONFIG)
endif()
if(HX_PLATFORM STREQUAL "sdl3_gl" AND SDL3_FOUND)
  add_executable(hexahedron
    src/platform/sdl3_gl/main.cpp
    src/platform/sdl3_gl/gl_renderer.cpp)
  target_link_libraries(hexahedron hx_game SDL3::SDL3)
  target_compile_definitions(hexahedron PRIVATE HX_SOURCE_ASSETS="${CMAKE_SOURCE_DIR}/assets")
  find_package(OpenGL REQUIRED)
  target_link_libraries(hexahedron OpenGL::GL)
  add_custom_command(TARGET hexahedron POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:hexahedron>/assets)
else()
  message(WARNING "No backend built for HX_PLATFORM=${HX_PLATFORM} (SDL3 missing or backend not implemented yet) - only the portable libraries and tests")
endif()
add_executable(audio_preview tools/audio_preview.cpp)
target_link_libraries(audio_preview hx_game)
