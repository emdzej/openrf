# Choose a level

On the **title screen**:

- **F2** starts the current map (level 1 at first, then wherever your last win took you).
- **1–9** start the first map of that difficulty level.
- **F3** and **Shift + 1–9** do the same for two-player maps.

From the command line, with launch parameters (the bundle launchers pass anything after the CD on to
`gasm-run`; in the browser, add them to the play page's URL, e.g. `/play/?play=1&level=51`):

```sh
gasm-run openrf.wasm --asset-dir cd --param play=1 --param level=51          # map number 1–100
gasm-run openrf.wasm --asset-dir cd --param play=1 --param level=WORLDS/1PLAYER/LEVEL6/RFMAP051.RFM
gasm-run openrf.wasm --asset-dir cd --param play2=1 --param level=120        # 2P maps 101–204
./openrf.sh --param play=1 --param level=51                                  # Linux bundle, saved CD
```

`play=1` skips the title screen, `skip_intro=1` skips the intro movies.

A development map viewer shows every map with the real renderer (arrows scroll, `[` / `]`
switch maps):

```sh
gasm-run openrf.wasm --asset-dir cd --param skip_intro=1 --param viewer=1
```
