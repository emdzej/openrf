# Extract the CD image

OpenRF can read a `.cue`/`.bin` or `.iso` image directly ([Game data](/guide/game-data)), so
extracting is optional. It is useful for development (the tests, `tools/*.py` and the reference
renderer read `./cd`) or to look at the files. To use an image as a folder without copying it
(`gasm-run --asset-dir`, or the [browser player](/play/){target="_self"}), [mount it](#mount-it).

## Mount it

Mounting makes a disc image appear as a drive, like the CD itself. Nothing is copied.

- **macOS:** double-click the `.iso` (or `hdiutil attach -readonly rf.iso`). It appears as
  `/Volumes/RFIRE`; eject it in Finder when done.
- **Windows 10/11:** right-click the `.iso`, **Mount** (or double-click it). It gets a drive letter,
  e.g. `E:\`; right-click the drive, **Eject** when done.
- **Linux:** `sudo mkdir -p /mnt/rfire && sudo mount -o loop,ro rf.iso /mnt/rfire` (unmount with
  `sudo umount /mnt/rfire`), or in GNOME Disks: menu, **Attach Disk Image** (read-only), then mount the new device; it
  appears under `/media/$USER/`.

These tools mount ISO 9660 images (2048-byte sectors) only. A raw **`.bin`/`.cue`** pair
(MODE1/2352, most dumps) has to be converted to an `.iso` first, [as below](#from-a-bin-cue-pair)
(the `python3` snippet writes `rf.iso`; skip the `7z` step), then mounted. OpenRF itself reads the raw
`.bin` directly, so you only need this to use the files as a folder.

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
