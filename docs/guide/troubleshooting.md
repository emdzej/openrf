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

### “Return Fire (gasm).app” or the Windows launcher is blocked
`Return Fire (gasm).app` is signed ad hoc, not notarized: right-click → **Open**, or
`xattr -dr com.apple.quarantine "Return Fire (gasm).app"`. On Windows, SmartScreen may say it
protected your PC: **More info → Run anyway**.

### “Operation not permitted” (macOS)
macOS privacy protection (Files and Folders) keeps apps out of `Downloads`, `Documents`, `Desktop`,
removable volumes and network volumes until you allow it. If the game, the launcher or Terminal
reports *Operation not permitted* for your CD folder or disc image:

- Allow the prompt when macOS asks, or turn it on later in **System Settings → Privacy & Security →
  Files and Folders** (the app, or Terminal if you start it from there). **Full Disk Access** also works.
- Or move the CD copy or disc image out of `Downloads`, e.g. to `~/Games/ReturnFire`, and choose it
  again (`Return Fire (gasm).app`: hold Option while opening it).

### The gasm bundle does not start
The launchers keep a log:

| Bundle | Log |
|---|---|
| `Return Fire (gasm).app` | `~/Library/Logs/OpenRF/gasm.log` (the last lines are also shown in an alert) |
| Linux `openrf.sh` | the terminal, or `~/.local/state/openrf/gasm.log` when started from a menu |
| Windows `OpenRF.cmd` | the console window (it stays open on an error) |

`--dry-run` prints the `gasm-run` command the launcher would run, and `--change-cd` picks the CD
again. On Linux, gasm-run needs ALSA (`libasound2t64` or `libasound2`) and a Vulkan or OpenGL
driver. “game data not found” means gasm-run got no CD: see [Running on gasm](./gasm#run).

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
