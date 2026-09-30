# Make a release

Releases are built by GitHub Actions (`.github/workflows/release.yml`):

Tags are plain semantic versions, without a `v` prefix:

```sh
git tag 0.1.0
git push origin 0.1.0
```

The workflow builds a universal `Return Fire.app` with SDL3 statically linked, checks that
it depends only on system libraries, signs it ad hoc, and attaches
`OpenRF-<version>-macos-universal.zip` plus a SHA-256 checksum to a new GitHub Release.

Run it manually from the Actions tab (*Release → Run workflow*) to get a snapshot build as a
workflow artifact without publishing a release.

Remember to bump `project(OpenRF VERSION …)` in `CMakeLists.txt` first.
