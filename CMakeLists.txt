cmake_minimum_required(VERSION 3.24)
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architectures (use \"arm64;x86_64\" for universal)")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
project(OpenRF VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)

# The portable core: everything except the host backends. Built without SDL (no include path, no
# library), so a platform dependency can't creep in; a gasm build compiles the same file list.
file(GLOB CORE_SRC src/*.c src/render/*.c src/game/*.c)
set(HOST_SRC src/platform_sdl.c src/vfs_host.c src/storage_file.c)
foreach(f ${HOST_SRC} src/platform_gasm.c)
  list(REMOVE_ITEM CORE_SRC "${CMAKE_SOURCE_DIR}/${f}")
endforeach()

# Platform backend: sdl (default: the macOS app + tests) or gasm (openrf.wasm, a gasm guest; configure with
# gasm's C SDK toolchain, see docs/guide/gasm.md):
#   cmake -S . -B build-gasm -DOPENRF_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=$GASM/sdk/c/cmake/gasm-toolchain.cmake [-DWASI_SDK_PREFIX=<wasi-sdk>]
set(OPENRF_PLATFORM sdl CACHE STRING "Platform backend: sdl or gasm")
set_property(CACHE OPENRF_PLATFORM PROPERTY STRINGS sdl gasm)
if(OPENRF_PLATFORM STREQUAL "gasm")
  include(Gasm)   # from the gasm toolchain file's module path
  # -O2: ~300 KB and ~20% faster than -Oz (~250 KB); -O3 is larger and not measurably faster.
  set(OPENRF_GASM_OPT "-O2" CACHE STRING "Optimisation flag for openrf.wasm")
  gasm_add_game(openrf ${CORE_SRC} src/platform_gasm.c)
  set_target_properties(openrf PROPERTIES OUTPUT_NAME openrf)
  target_compile_options(openrf PRIVATE -Wall -Wextra -Wno-unused-parameter ${OPENRF_GASM_OPT})
  # The core keeps some large arrays on the stack (mixer and decoder scratch): 1 MiB instead of 64 KiB.
  target_link_options(openrf PRIVATE -Wl,-z,stack-size=1048576 -Wl,--strip-debug)
  return()
endif()

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
# App icon: the site's skull favicon, scaled up (tools/make-icns.sh; skipped without iconutil/python3).
find_program(ICONUTIL iconutil)
find_program(PYTHON3 python3)
if(APPLE AND ICONUTIL AND PYTHON3)
  set(OPENRF_ICNS "${CMAKE_BINARY_DIR}/openrf.icns")
  add_custom_command(OUTPUT "${OPENRF_ICNS}"
    COMMAND "${CMAKE_SOURCE_DIR}/tools/make-icns.sh" "${OPENRF_ICNS}"
    DEPENDS "${CMAKE_SOURCE_DIR}/docs/public/favicon.png" "${CMAKE_SOURCE_DIR}/tools/icon.py"
            "${CMAKE_SOURCE_DIR}/tools/make-icns.sh"
    VERBATIM)
  target_sources(openrf PRIVATE "${OPENRF_ICNS}")
  set_source_files_properties("${OPENRF_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(openrf PROPERTIES MACOSX_BUNDLE_ICON_FILE openrf.icns)
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
