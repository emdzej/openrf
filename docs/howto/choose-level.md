# Choose a level

On the **title screen**:

- **F2** starts the current map (level 1 at first, then wherever your last win took you).
- **1–9** start the first map of that difficulty level.
- **F3** and **Shift + 1–9** do the same for two-player maps.

From the command line:

```sh
"Return Fire.app/Contents/MacOS/Return Fire" --play --level 51                              # map number 1–100
"Return Fire.app/Contents/MacOS/Return Fire" --play --level WORLDS/1PLAYER/LEVEL6/RFMAP051.RFM
"Return Fire.app/Contents/MacOS/Return Fire" --play2 --level 120                            # 2P maps 101–204
```

`--play` skips the title screen, `--skip-intro` skips the intro movies.

A development map viewer shows every map with the real renderer (arrows scroll, `[` / `]`
switch maps):

```sh
"Return Fire.app/Contents/MacOS/Return Fire" --skip-intro --viewer
```
