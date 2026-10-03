# Debug options & screenshots

The game runs on gasm; debug options are launch parameters (`gasm-run --param name=value`, or URL query
parameters on the play page):

| Parameter | Effect |
|---|---|
| `demo=<mode>` | Scripted input for unattended runs: `1` (drive), `fire`, `jeep`, `msv`, `heli`, `turret`, `drone`, `sub`, `rules`, `win`; two-player: `2p`, `2pheli`, `2pwin`, `2pspectate` (with `play2=1`) |
| `sfx_log=1` | Log sound-effect instances and voices |
| `cam_h=<n>` | Driving camera height (0 = 1.0× zoom) |
| `p1=<name>`, `p2=<name>` | Player names for the high scores |

gasm-run's own options do the rest: `--headless N` runs N frames without a window or sound and prints
hashes of the video and audio, `--screenshot <png>` saves the last frame, `--mute` silences a windowed run,
`--input` scripts pads and keys ([Running on gasm](/guide/gasm#headless-runs-and-checks)). The clock is
16 ms per frame, so runs are reproducible: frame N shows the game at 16 × (N − 1) ms.

Example — the screenshot of a tank firing used on this site (11000 ms = frame 689):

```sh
gasm-run build-gasm/openrf.wasm --asset-dir cd --headless 689 --screenshot shot.png \
  --param skip_intro=1 --param play=1 --param demo=fire
```

`tools/screenshots.sh` regenerates every screenshot in `docs/public/screenshots/` that way, with the
released `gasm-run` (data from `./cd`, or `OPENRF_DATA=<folder or image>`).

The native build (tests only) reads:

| Variable | Effect |
|---|---|
| `OPENRF_DATA=<folder or image>` | Data for the tests and `render_test` (default `./cd`) |
| `OPENRF_HS=<file>` | High-score file of `rules_test` / `twoplayer_test` (default `~/Library/Application Support/Return Fire/RFire_HS`) |
