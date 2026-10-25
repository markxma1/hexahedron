cmake_minimum_required(VERSION 3.16)
project(Hexahedron CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- portable layers (no platform dependencies) ---------------------------
# All .cpp files in src/core and src/game are picked up automatically, so a new
# shared file needs no build change on any platform (the PS3 Makefile does the same).
file(GLOB HX_CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB HX_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp)
add_library(hx_core STATIC ${HX_CORE_SOURCES})
target_include_directories(hx_core PUBLIC src)
add_library(hx_game STATIC ${HX_GAME_SOURCES})
target_link_libraries(hx_game PUBLIC hx_core)
find_package(Threads REQUIRED)
target_link_libraries(hx_game PUBLIC Threads::Threads)

# --- platform backend -------------------------------------------------------
#   sdl3_gl : PC (Linux/Windows), SDL3 + OpenGL 3.3        (default)
#   android : same sdl3_gl code as a shared library, GLES 3.0 + touch
#             (built by Gradle from src/platform/android, selected automatically)
#   ps3     : PSL1GHT backend, built with its own Makefile in src/platform/ps3
set(HX_PLATFORM "sdl3_gl" CACHE STRING "Platform backend: sdl3_gl | android")
if(ANDROID)
  set(HX_PLATFORM "android" CACHE STRING "" FORCE)
endif()
set(HX_SDL_BACKEND_SOURCES
  src/platform/sdl3_gl/main.cpp
  src/platform/sdl3_gl/gl_renderer.cpp)

if(HX_PLATFORM STREQUAL "android")
  include(FetchContent)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3)
  add_library(main SHARED ${HX_SDL_BACKEND_SOURCES})   # SDL's Java side loads libmain.so
  target_link_libraries(main hx_game SDL3::SDL3 GLESv3 android log)
  return()
endif()

# --- tests and tools (host only) --------------------------------------------
enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests hx_core)
add_test(NAME core_tests COMMAND core_tests)
add_executable(touch_tests tests/touch_tests.cpp src/game/touch.cpp)
target_include_directories(touch_tests PRIVATE src)
add_test(NAME touch_tests COMMAND touch_tests)
add_executable(asset_preview tools/asset_preview.cpp)
target_link_libraries(asset_preview hx_game)
add_executable(audio_preview tools/audio_preview.cpp)
target_link_libraries(audio_preview hx_game)

find_package(SDL3 CONFIG)
if(SDL3_FOUND)
  add_executable(hexahedron ${HX_SDL_BACKEND_SOURCES})
  target_link_libraries(hexahedron hx_game SDL3::SDL3)
  target_compile_definitions(hexahedron PRIVATE HX_SOURCE_ASSETS="${CMAKE_SOURCE_DIR}/assets")
  find_package(OpenGL REQUIRED)
  target_link_libraries(hexahedron OpenGL::GL)
  add_custom_command(TARGET hexahedron POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:hexahedron>/assets)
else()
  message(WARNING "SDL3 not found - only the portable libraries and tests are built")
endif()
