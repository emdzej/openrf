cmake_minimum_required(VERSION 3.24)
project(OpenRF VERSION 0.3.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)

# The portable core: everything except the host backends. The native build has no platform library at
# all (tests and tools only); the game itself is the gasm build of the same file list.
file(GLOB CORE_SRC src/*.c src/render/*.c src/game/*.c)
set(HOST_SRC src/vfs_host.c src/storage_file.c)
foreach(f ${HOST_SRC} src/platform_gasm.c)
  list(REMOVE_ITEM CORE_SRC "${CMAKE_SOURCE_DIR}/${f}")
endforeach()

# Platform: native (the headless tests and render_test, against the host filesystem) or gasm (openrf.wasm,
# a gasm guest; configure with gasm's C SDK toolchain, see docs/guide/gasm.md):
#   cmake -S . -B build-gasm -DOPENRF_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=$GASM/sdk/c/cmake/gasm-toolchain.cmake [-DWASI_SDK_PREFIX=<wasi-sdk>]
# (tools/fetch-gasm-sdk.sh fetches the released SDK and wasi-sdk into .deps/)
set(OPENRF_PLATFORM native CACHE STRING "Platform: native (tests) or gasm (the game)")
set_property(CACHE OPENRF_PLATFORM PROPERTY STRINGS native gasm)
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

if(NOT APPLE)
  link_libraries(m)   # libm is separate from libc on Linux
endif()

# The whole core compiles natively too (no platform dependency can creep in).
add_library(openrf_core OBJECT ${CORE_SRC})
target_compile_options(openrf_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

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
