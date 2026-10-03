# Make a release

Releases are built by GitHub Actions (`.github/workflows/release.yml`):

Tags are plain semantic versions, without a `v` prefix:

```sh
git tag 0.1.0
git push origin 0.1.0
```

The workflow builds `openrf-<version>.wasm` and packages it with the released `gasm-run`
(`GASM_VERSION` in `tools/fetch-gasm-sdk.sh`) into the gasm bundles for macOS (universal), Linux x86_64
and arm64, and Windows x86_64, smoke-tests each bundle on its own system, and attaches the module, the
four bundles and their SHA-256 checksums to a new GitHub Release.

Run it manually from the Actions tab (*Release → Run workflow*) to get a snapshot build as
workflow artifacts without publishing a release.

Remember to bump `project(OpenRF VERSION …)` in `CMakeLists.txt` first.
