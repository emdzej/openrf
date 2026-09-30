cmake_minimum_required(VERSION 3.24)
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architectures (use \"arm64;x86_64\" for universal)")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
project(OpenRF VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)

# SDL3: system package by default; OPENRF_BUNDLED_SDL=ON builds it from source and links it
# statically (used for universal release builds, so the .app needs no external libraries).
option(OPENRF_BUNDLED_SDL "Build SDL3 from source and link statically" OFF)
if(OPENRF_BUNDLED_SDL)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                           GIT_TAG release-3.4.16 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)
else()
  find_package(SDL3 REQUIRED CONFIG)
endif()
# The portable core: everything except the host backends. Built without SDL (no include path, no
# library), so a platform dependency can't creep in; a gasm build compiles the same file list.
file(GLOB CORE_SRC src/*.c src/render/*.c src/game/*.c)
set(HOST_SRC src/platform_sdl.c src/vfs_host.c src/storage_file.c)
foreach(f ${HOST_SRC})
  list(REMOVE_ITEM CORE_SRC "${CMAKE_SOURCE_DIR}/${f}")
endforeach()
add_library(openrf_core OBJECT ${CORE_SRC})
target_compile_options(openrf_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
# SDL3 backend + host filesystem backends (vfs_host.c, storage_file.c: POSIX).
add_executable(openrf MACOSX_BUNDLE ${HOST_SRC})
target_compile_options(openrf PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(openrf PRIVATE openrf_core SDL3::SDL3)
# Point the bundle at the extracted CD (Contents/Resources/data -> ./cd) so the app runs from anywhere.
if(EXISTS "${CMAKE_SOURCE_DIR}/cd")
  add_custom_command(TARGET openrf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:openrf>/Resources"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_SOURCE_DIR}/cd" "$<TARGET_BUNDLE_CONTENT_DIR:openrf>/Resources/data"
    VERBATIM)
endif()
set_target_properties(openrf PROPERTIES
  OUTPUT_NAME "Return Fire"
  MACOSX_BUNDLE_GUI_IDENTIFIER org.openrf.returnfire
  MACOSX_BUNDLE_BUNDLE_NAME "Return Fire"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_COPYRIGHT "OpenRF contributors, GPL-3.0. Return Fire is (c) Silent Software.")

# Tests and tools read the data through the file layer (./cd, or OPENRF_DATA=<dir|image>).
set(VFS_SRC src/vfs.c src/vfs_host.c)

# Renderer verification against tools/view.py (python3 tools/render_cmp.py runs it).
add_executable(render_test tests/render_test.c src/render/cel.c src/render/view.c src/render/cellmap.c
  src/render/models_data.c src/sprites.c src/assets.c src/world_tables.c src/exe.c ${VFS_SRC})
target_compile_options(render_test PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)

# Headless simulation test (tests/sim_test.c): drives vehicles around level 1.
file(GLOB GAME_SRC src/game/*.c)
add_executable(sim_test tests/sim_test.c ${GAME_SRC} src/world.c src/world_tables.c src/assets.c src/exe.c src/render/models_data.c ${VFS_SRC})
target_include_directories(sim_test PRIVATE src)

# Headless sound-effect mixer test (tests/sfx_test.c): writes out/sfx/test.wav.
add_executable(sfx_test tests/sfx_test.c src/sfx.c src/sfx_tables.c src/assets.c src/exe.c ${VFS_SRC})
target_include_directories(sfx_test PRIVATE src)
target_compile_options(sfx_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Headless combat test (tests/combat_test.c): weapons, projectiles, explosions, debris, damage.
add_executable(combat_test tests/combat_test.c ${GAME_SRC} src/world.c src/world_tables.c src/assets.c src/exe.c src/render/models_data.c ${VFS_SRC})
target_include_directories(combat_test PRIVATE src)
target_compile_options(combat_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Headless enemy test (tests/ai_test.c): turrets, drone, submarine (renders the view for the tower hook).
add_executable(ai_test tests/ai_test.c ${GAME_SRC} src/world.c src/world_tables.c src/assets.c src/exe.c src/render/models_data.c
  src/render/cel.c src/render/view.c src/render/cellmap.c src/sprites.c ${VFS_SRC})
target_include_directories(ai_test PRIVATE src)
target_compile_options(ai_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Headless rules test (tests/rules_test.c): flag, win/loss, men, gates, random mines, high scores.
add_executable(rules_test tests/rules_test.c ${GAME_SRC} src/world.c src/world_tables.c src/assets.c src/exe.c src/render/models_data.c src/highscore.c src/storage_file.c ${VFS_SRC})
target_include_directories(rules_test PRIVATE src)
target_compile_options(rules_test PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Headless 2-player test (tests/twoplayer_test.c): GameSetup2P / GameFrame2P, both sides, win, spectate, draw.
add_executable(twoplayer_test tests/twoplayer_test.c ${GAME_SRC} src/world.c src/world_tables.c src/assets.c src/exe.c src/render/models_data.c src/highscore.c src/storage_file.c ${VFS_SRC})
target_include_directories(twoplayer_test PRIVATE src)
target_compile_options(twoplayer_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
