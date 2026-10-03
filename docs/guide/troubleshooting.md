# Troubleshooting

### “game data not found”
gasm-run got no CD: neither an asset `cd` (a disc image) nor the CD's files (`--asset-dir`). The bundle
launchers pass the saved CD; `--change-cd` picks it again. See [Game data](./game-data).

### “…/RFIRE.BIN was not found”
The game data folder has `ART`, `SOUND`, `TITLE` and `WORLDS` but not `RFIRE.BIN`. Copy `RFIRE.BIN`
from the root of the Return Fire CD into the same folder. OpenRF reads the original game's tables
(models, objects, sounds) from it. See [Game data](./game-data).

### “RFIRE.BIN … is not the supported version”
The file is a different build or was damaged in copying. OpenRF supports the Windows 95 release
(431,616 bytes, CRC-32 `64c49a1b`). Copy it from the CD again. If you have a different
release, please [report it](https://github.com/emdzej/openrf/issues) with the size and CRC-32
shown in the message.

### “Return Fire (gasm).app” is damaged, or the Windows launcher is blocked
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
driver.

### Nothing happens in the bunker
You are in the vehicle-select lift: choose with W/A/S/D and press **H** to launch.

### No sound or music
Check the system's output device, and that the launcher wasn't given `--mute`. The music is streamed from `SOUND/SCORE.WAV` (223 MB), so
make sure that file was copied completely.

### Starting from a terminal
The launchers print gasm-run's messages (macOS: `"Return Fire (gasm).app/Contents/MacOS/OpenRF"`;
Linux: `./openrf.sh`; Windows: `OpenRF.cmd` from a command prompt). Please include that output, or the
log above, when [reporting an issue](https://github.com/emdzej/openrf/issues).
