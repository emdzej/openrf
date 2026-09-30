# Debug options & screenshots

Environment variables understood by OpenRF:

| Variable | Effect |
|---|---|
| `OPENRF_SHOT=<file.bmp>` | Save the frame shown at `OPENRF_SHOT_MS` (default 1500 ms) and quit |
| `OPENRF_SHOT_MS=<ms>` | When to take the screenshot |
| `OPENRF_DEMO=<mode>` | Scripted input for unattended runs: `1` (drive), `fire`, `jeep`, `msv`, `heli`, `turret`, `drone`, `sub`, `rules`, `win`; two-player: `2p`, `2pheli`, `2pwin`, `2pspectate` (with `--play2`) |
| `OPENRF_MUTE=1` | Silence all audio output (the game still mixes normally) |
| `OPENRF_FIXED_STEP=1` | Replace the wall clock with a virtual one (16 ms per presented frame), so a run and its `OPENRF_SHOT` frame are reproducible (compare builds with `cmp`) |
| `OPENRF_SFX_LOG=1` | Log sound-effect instances and voices |
| `OPENRF_CAM_H=<n>` | Driving camera height (0 = 1.0× zoom) |

Example — the screenshot of a tank firing used on this site:

```sh
OPENRF_DEMO=fire OPENRF_SHOT=/tmp/shot.bmp OPENRF_SHOT_MS=11000 \
  "build/Return Fire.app/Contents/MacOS/Return Fire" cd --skip-intro --play
sips -s format png /tmp/shot.bmp --out shot.png
```

`tools/screenshots.sh` regenerates every screenshot in `docs/public/screenshots/`.
