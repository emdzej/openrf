# Build from source

```sh
git clone https://github.com/emdzej/openrf.git
cd openrf
```

The game is one WebAssembly module, `openrf.wasm`, for the [gasm](https://gasm.emdzej.pl) runtime. The
native build has only the headless tests and tools.

## The game (`openrf.wasm`)

Requirements: CMake ≥ 3.24, curl and unzip on macOS or Linux. `tools/fetch-gasm-sdk.sh` downloads
wasi-sdk and the gasm C SDK (version `GASM_VERSION`, 0.6.0) into `.deps/`:

```sh
tools/fetch-gasm-sdk.sh
cmake -S . -B build-gasm -DOPENRF_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm -j        # -> build-gasm/openrf.wasm
```

A gasm checkout works too: `-DCMAKE_TOOLCHAIN_FILE=$GASM/sdk/c/cmake/gasm-toolchain.cmake`. Run it with
`gasm-run` 0.5.0 or newer (`tools/fetch-gasm-runner.sh <platform>` downloads the released one and prints
its folder), see [Running on gasm](/guide/gasm):

```sh
RUN="$(tools/fetch-gasm-runner.sh macos-universal)/gasm-run"    # or linux-x86_64, linux-arm64
"$RUN" build-gasm/openrf.wasm --asset-dir cd
```

## Bundles

`tools/package-gasm.sh <version> <platform> <runner dir> <out>` packages the module with a runner and the
launcher into the same archives as a release (`macos-universal`, `linux-x86_64`, `linux-arm64`,
`windows-x86_64`); `OPENRF_WASM=` picks the module:

```sh
OPENRF_WASM=build-gasm/openrf.wasm tools/package-gasm.sh 0.0.0-dev macos-universal \
  "$(tools/fetch-gasm-runner.sh macos-universal)" dist
```

## The browser player

```sh
docs/scripts/copy-wasm.sh && (cd docs && pnpm install && scripts/vendor-web.sh && pnpm build)
(cd docs/.vitepress/dist && python3 -m http.server 8080)   # then open http://localhost:8080/play/
```

## Tests (native)

Requirements: CMake ≥ 3.24 and a C11 compiler (macOS or Linux); no libraries.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This builds the portable core and the headless test programs; see [Run the tests](./tests).

## CMake options

| Option | Default | Meaning |
|---|---|---|
| `OPENRF_PLATFORM` | `native` | `native`: the tests; `gasm`: `openrf.wasm` (with the gasm toolchain file) |
| `OPENRF_GASM_OPT` | `-O2` | Optimisation flag for `openrf.wasm` |
