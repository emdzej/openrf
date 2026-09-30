# Game data

OpenRF needs the contents of the Return Fire CD: the folders `ART`, `SOUND`, `TITLE` and
`WORLDS`, and the file `RFIRE.BIN` from the root of the CD, next to them (the original game program:
OpenRF reads its 3D models, object tables and sound tables from it). The other folders on the disc are
not used.

## Where OpenRF looks

In this order:

1. A path given on the command line: `"Return Fire.app/Contents/MacOS/Return Fire" /path/to/cd`
2. `Return Fire.app/Contents/Resources/data`
3. A folder called `cd` next to the app

The simplest option is to copy the CD contents into the app:

```sh
mkdir -p "/Applications/Return Fire.app/Contents/Resources/data"
cp -R /Volumes/RFIRE/* "/Applications/Return Fire.app/Contents/Resources/data/"
```

If the data is missing, OpenRF shows a dialog explaining where to put it. `RFIRE.BIN` is found
regardless of upper/lower case, and is checked against the supported build (431,616 bytes, CRC-32
`64c49a1b`).

## From a disc image

If you have a `.bin`/`.cue` pair (a raw MODE1/2352 image) or an `.iso`, see
[Extract the CD image](/howto/extract-cd).

## Which editions work

OpenRF targets the **Windows 95 edition** (`RFIRE.EXE` + `RFIRE.BIN`, `ART/ART.CAR`, `.RFM` maps,
`.STM` movies, `SOUND/SCORE.WAV`). It was developed with the European release
(English, French, German, Spanish, Italian). The 3DO version uses different file formats
and is not supported.
