# Extract the CD image

OpenRF needs the files from the disc as a normal folder.

## From a physical CD or a mounted image
Copy the `ART`, `SOUND`, `TITLE` and `WORLDS` folders from the mounted volume.

## From a `.iso`
Double-click it to mount it in Finder and copy the folders, or:

```sh
7z x "Return Fire.iso" -ocd          # brew install sevenzip
```

## From a `.bin` / `.cue` pair
Most dumps are raw **MODE1/2352** images (check the `.cue`: `TRACK 01 MODE1/2352`). Each
2352-byte sector holds 2048 bytes of data after a 16-byte header, so convert to an ISO first:

```sh
python3 - "Return Fire (Europe) (En,Fr,De,Es,It).bin" <<'PY'
import sys
with open(sys.argv[1], "rb") as f, open("rf.iso", "wb") as o:
    while (s := f.read(2352)):
        o.write(s[16:16 + 2048])
PY
7z x rf.iso -ocd
```

Then point OpenRF at the `cd` folder ([Game data](/guide/game-data)).
