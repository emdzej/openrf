# Build from source

Requirements: macOS 11+, Xcode command line tools, CMake ≥ 3.24.

```sh
git clone https://github.com/emdzej/openrf.git
cd openrf
```

## Quick build (Homebrew SDL3)

```sh
brew install cmake sdl3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
open "build/Return Fire.app"
```

If a `cd/` folder with the [game data](/guide/game-data) exists in the checkout at configure
time, the build links it into the app bundle (`Contents/Resources/data`), so the app runs from
Finder straight away.

## Universal, self-contained build

This is what the release workflow does: SDL3 is built from source and linked statically, for
both architectures.

```sh
cmake -S . -B build-universal -DCMAKE_BUILD_TYPE=Release \
      -DOPENRF_BUNDLED_SDL=ON -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-universal -j --target openrf
lipo -info "build-universal/Return Fire.app/Contents/MacOS/Return Fire"
```

## CMake options

| Option | Default | Meaning |
|---|---|---|
| `CMAKE_OSX_ARCHITECTURES` | `arm64` | `arm64;x86_64` for a universal binary |
| `CMAKE_OSX_DEPLOYMENT_TARGET` | `11.0` | Minimum macOS version |
| `OPENRF_BUNDLED_SDL` | `OFF` | Fetch SDL3 and link it statically |
