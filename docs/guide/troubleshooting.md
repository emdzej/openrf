# Troubleshooting

### “Return Fire game data was not found”
OpenRF could not find the `ART`, `SOUND`, `TITLE` and `WORLDS` folders (and `RFIRE.BIN`). See
[Game data](./game-data).

### “…/RFIRE.BIN was not found”
The game data folder has `ART`, `SOUND`, `TITLE` and `WORLDS` but not `RFIRE.BIN`. Copy `RFIRE.BIN`
from the root of the Return Fire CD into the same folder. OpenRF reads the original game's tables
(models, objects, sounds) from it. See [Game data](./game-data).

### “RFIRE.BIN … is not the supported version”
The file is a different build or was damaged in copying. OpenRF supports the Windows 95 release
(431,616 bytes, CRC-32 `64c49a1b`). Copy it from the CD again. If you have a different
release, please [report it](https://github.com/emdzej/openrf/issues) with the size and CRC-32
shown in the message.

### “Return Fire.app is damaged / can’t be opened”
Gatekeeper blocks apps that aren't notarized. Right-click → **Open**, or run
`xattr -dr com.apple.quarantine "Return Fire.app"`.

### Nothing happens in the bunker
You are in the vehicle-select lift: choose with W/A/S/D and press **H** to launch.

### No sound or music
Check the macOS output device. The music is streamed from `SOUND/SCORE.WAV` (223 MB), so
make sure that file was copied completely.

### Starting from Terminal
Running the binary directly prints diagnostics:

```sh
"/Applications/Return Fire.app/Contents/MacOS/Return Fire" /path/to/cd
```

Please include that output when [reporting an issue](https://github.com/emdzej/openrf/issues).
