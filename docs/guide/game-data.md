# Game data

OpenRF needs the contents of the Return Fire CD: the folders `ART`, `SOUND`, `TITLE` and
`WORLDS`, and the file `RFIRE.BIN` from the root of the CD, next to them (the original game program:
OpenRF reads its 3D models, object tables and sound tables from it). The other folders on the disc are
not used. It can read them from a folder or straight from a **disc image** (a raw `.bin`, of a `.bin`/`.cue`
pair, or an `.iso`), with no extraction needed.

## Giving OpenRF the CD

- **The bundles** (`Return Fire (gasm).app`, `openrf.sh`, `OpenRF.cmd`) ask for the CD the first time and
  remember it: a folder (the disc itself, a mounted image, or a copy) or a `.bin` / `.iso` file. A `.cue`
  is refused: choose the `.bin` next to it. See [Installing](./install).
- **Your own `gasm-run`**: `gasm-run openrf.wasm --asset-dir <folder>` for a folder, `--asset cd=<image>`
  for a `.bin` or `.iso` ([Running on gasm](./gasm#run)).
- **The [browser player](/play/){target="_self"}** asks for the same folder (or image) and keeps a copy
  in the browser's storage for this site.

To use an image as a folder, [mount it](/howto/extract-cd#mount-it). `RFIRE.BIN` is found regardless of
upper/lower case, and is checked against the supported build (431,616 bytes, CRC-32 `64c49a1b`).

## From a disc image

OpenRF reads the ISO 9660 file system of the image itself: plain `.iso` images (2048-byte sectors)
and raw `.bin` images (2352-byte sectors, MODE1/2352 or MODE2/2352, as in most `.bin`/`.cue` dumps).
The game runs exactly as from the extracted folder. To extract the files anyway, see
[Extract the CD image](/howto/extract-cd).

## Which editions work

OpenRF targets the **Windows 95 edition** (`RFIRE.EXE` + `RFIRE.BIN`, `ART/ART.CAR`, `.RFM` maps,
`.STM` movies, `SOUND/SCORE.WAV`). It was developed with the European release
(English, French, German, Spanish, Italian). The 3DO version uses different file formats
and is not supported.
