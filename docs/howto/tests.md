# Run the tests

The tests exercise the real engine code headlessly against the original data, so they need
the game data in `./cd` (they cannot run in CI, which only compiles them). `OPENRF_DATA=<folder or
image>` points them (and `render_test`) elsewhere, e.g. at the `.cue`, to check the disc-image reader:
the output must be identical.

```sh
cmake --build build -j
for t in sim_test combat_test sfx_test ai_test rules_test twoplayer_test; do ./build/$t > /dev/null && echo "$t ok"; done
python3 tools/render_cmp.py        # renderer vs. the Python reference: expect 0 mismatches
```

| Test | Covers |
|---|---|
| `sim_test` | Bunker lift, driving all vehicles, collision, water, docking |
| `combat_test` | Weapons, projectiles, explosions, debris, damage states |
| `sfx_test` | Sound mixer: priorities, budget, attenuation, looping, pitch (writes `out/sfx/test.wav`) |
| `ai_test` | Gun turrets, drone, submarine |
| `rules_test` | Flag, winning and losing, soldiers, gates, mines, high scores |
| `twoplayer_test` | Two-player split screen: both sides launching, one winning |
| `render_test` / `render_cmp.py` | Perspective renderer, pixel-identical to `tools/view.py` in 7 scenes |
