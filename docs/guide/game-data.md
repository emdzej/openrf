# Game data

OpenRF needs the contents of the Return Fire CD: the folders `ART`, `SOUND`, `TITLE` and
`WORLDS`, and the file `RFIRE.BIN` from the root of the CD, next to them (the original game program:
OpenRF reads its 3D models, object tables and sound tables from it). The other folders on the disc are
not used. It can read them from a folder or straight from a **disc image** (`.cue` + `.bin`, a raw
`.bin`, or an `.iso`), with no extraction needed.

## Where OpenRF looks

In this order:

1. A path given on the command line, a folder or an image:
   `"Return Fire.app/Contents/MacOS/Return Fire" /path/to/cd` or `... "/path/to/Return Fire.cue"`
2. `Return Fire.app/Contents/Resources/data` (a folder), or `data.cue` (with its `.bin`), `data.bin` or
   `data.iso` in the same place
3. A folder called `cd` (or `cd.cue`, `cd.bin`, `cd.iso`) next to the app

The simplest option is to put the disc image into the app:

```sh
cp "Return Fire (Europe) (En,Fr,De,Es,It).bin" "/Applications/Return Fire.app/Contents/Resources/data.bin"
```

or to copy the CD contents:

```sh
mkdir -p "/Applications/Return Fire.app/Contents/Resources/data"
cp -R /Volumes/RFIRE/* "/Applications/Return Fire.app/Contents/Resources/data/"
```

If the data is missing, OpenRF shows a dialog explaining where to put it. `RFIRE.BIN` is found
regardless of upper/lower case, and is checked against the supported build (431,616 bytes, CRC-32
`64c49a1b`).

## From a disc image

OpenRF reads the ISO 9660 file system of the image itself: plain `.iso` images (2048-byte sectors)
and raw `.bin` images (2352-byte sectors, MODE1/2352 or MODE2/2352, as in most `.bin`/`.cue` dumps).
Given a `.cue`, it opens the image named on its `FILE` line (or the `.bin` of the same name). The game
runs exactly as from the extracted folder. To extract the files anyway, see
[Extract the CD image](/howto/extract-cd).

## Which editions work

OpenRF targets the **Windows 95 edition** (`RFIRE.EXE` + `RFIRE.BIN`, `ART/ART.CAR`, `.RFM` maps,
`.STM` movies, `SOUND/SCORE.WAV`). It was developed with the European release
(English, French, German, Spanish, Italian). The 3DO version uses different file formats
and is not supported.
