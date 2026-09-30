# `.STM` movie format (TITLE/*.STM)

STM is an AVI (Cinepak video + PCM audio) that Silent Software pre-interleaved into
fixed-size super-blocks so it can be streamed from CD by a reader thread and played
with no AVIFile parsing at runtime. The game does not use AVIFile for STMs; it reads
the blocks itself and calls the Cinepak ICM driver directly (`ICOpen('vidc','cvid')`,
`ICDecompressEx*` via `ICSendMessage`). Audio is copied into a DirectSound ring buffer.

All integers are little-endian unless noted. Decoder: `tools/stm.py`
(writes lossless `out/stm/<name>.avi` and `out/stm/<name>.mp4`).
Confidence: high. All 7 files parse with every cross-check passing (audio byte totals,
frame counts, frame-length fields, padding).

## File layout

```
0x0000            header (0x9A0 bytes used, zero-padded up to data_start)
data_start        chunk[0]   (chunk_size bytes)
+chunk_size       chunk[1]
...               chunk[nchunks-1]
```
File size = `data_start + nchunks*chunk_size` exactly.

## Header (loaded verbatim to `0x458900`, 0x9A0 bytes, by FUN_00424b70)

| off | size | field | notes / global |
|---|---|---|---|
| 0x000 | 4 | data_start | always 0x1000. `DAT_00458900` (first read offset) |
| 0x004 | 4 | chunk_size | 0x10000 (TWI.STM: 0xE800). `DAT_00458904` |
| 0x008 | 4 | nchunks | `DAT_00458908` |
| 0x00C | 140 | audio `AVISTREAMINFOA` | fccType `auds`, fccHandler 0 |
| 0x020 |  4 |  .dwScale = 4 | `DAT_00458920` |
| 0x024 |  4 |  .dwRate = 88200 | `DAT_00458924` (rate/scale = 22050 blocks/s) |
| 0x02C |  4 |  .dwLength (blocks) | |
| 0x030 |  4 |  .dwInitialFrames = 11 | unused by game |
| 0x03C |  4 |  .dwSampleSize = 4 | `DAT_0045893c`, audio block size |
| 0x058 | 64 |  .szName e.g. `"rf.avi Audio #1"` | garbage after NUL (uninitialised memory, contains "PAL " etc. - not real chunks) |
| 0x098 | 20 | audio format buffer | `PCMWAVEFORMAT`: tag 1, 2 ch, 22050 Hz, 88200 B/s, align 4, 16 bit; + zero pad. Passed to `IDirectSound::CreateSoundBuffer` (`DAT_00458998`) |
| 0x0AC | 4 | audio_total_bytes | = dwLength*4. `DAT_004589ac` (end-of-audio detection) |
| 0x0B0 | 4 | audio_ring_bytes | 132300 (1.5 s). DirectSound buffer size `DAT_004589b0` |
| 0x0B4 | 4 | 0 | |
| 0x0B8 | 140 | video `AVISTREAMINFOA` | fccType `vids`, fccHandler `cvid` (`DAT_004589bc`, passed to ICOpen) |
| 0x0CC |  4 |  .dwScale = 1 | `DAT_004589cc` |
| 0x0D0 |  4 |  .dwRate = 15 | `DAT_004589d0` -> 15 fps |
| 0x0D8 |  4 |  .dwLength (frames) | |
| 0x0E0 |  4 |  .dwSuggestedBufferSize | |
| 0x0EC | 16 |  .rcFrame = 0,0,320,240 | |
| 0x104 | 64 |  .szName `"rf.avi Video #1"` | |
| 0x144 | 0x428 | input format: `BITMAPINFOHEADER` + 256 RGBQUAD space | 320x240, planes 1, biBitCount 24 (RF.STM: 40 = Cinepak greyscale), compression `cvid`, sizeImage 29700. Palette area all zero. `DAT_00458a44` = lpbiSrc |
| 0x56C | 0x428 | output format: `BITMAPINFOHEADER` + RGBQUAD[256] | 320 x **-240** (top-down), 8 bpp BI_RGB. `DAT_00458e6c` = lpbiDst; sent with `ICM_DECOMPRESS_SET_PALETTE` (0x401D) so Cinepak dithers to this palette. All files share one palette except RF.STM |
| 0x994 | 4 | last_frame_number | = dwLength-1 |
| 0x998 | 4 | max compressed frame bytes | |
| 0x99C | 4 | decoded image bytes = 76800 | |
| 0x9A0.. | | zero to data_start | |

(AVISTREAMINFOA offsets: fccType+0, fccHandler+4, dwFlags+8, dwCaps+0xC, wPriority+0x10,
wLanguage+0x12, dwScale+0x14, dwRate+0x18, dwStart+0x1C, dwLength+0x20, dwInitialFrames+0x24,
dwSuggestedBufferSize+0x28, dwQuality+0x2C, dwSampleSize+0x30, rcFrame+0x34, dwEditCount+0x44,
dwFormatChangeCount+0x48, szName+0x4C.) The header is basically an AVIFile dump: `AVIStreamInfo` + `AVIStreamReadFormat` for each stream.

## Chunk (super-block) layout

Each chunk is read whole into a `chunk_size` buffer; the buffer is linked into a list
in memory, so the first 8 bytes are reserved for runtime link pointers.

| off | size | field |
|---|---|---|
| 0x00 | 4 | 0 (runtime: next ptr) |
| 0x04 | 4 | 0 (runtime: prev ptr) |
| 0x08 | 4 | audio_offset (from chunk start; 0 = no audio) |
| 0x0C | 4 | audio_count in 4-byte blocks (game multiplies by hdr+0x3C at load) |
| 0x10 | 4 | first_video_record offset (0 = no video) |
| 0x14 | 4 | 0 (runtime frame counter) |
| 0x18 | 8 | 0 |
| 0x20 | | video records, then audio block(s) packed at the end |

Audio is raw 16-bit stereo 22050 Hz PCM, filling `[audio_offset, audio_offset+count*4)`.
Typically it runs to the exact end of the chunk. Concatenating all chunks' audio in order
gives exactly `audio_total_bytes`. The first 1-2 chunks are audio-only (about 1.5 s preroll to
fill the DirectSound ring). The last chunks are video-only.

Video record (starts 4-byte aligned):

| off | size | field |
|---|---|---|
| +0 | 4 | next record offset from chunk start (0 = last in this chunk) |
| +4 | 1 | flags: bit0 = keyframe (AVIIF_KEYFRAME) |
| +5 | 1 | 0 |
| +6 | 2 | frame number (0-based, u16) |
| +8 | n | raw Cinepak frame. n = big-endian 24-bit length at frame bytes 1..3 (standard cvid frame header: flags(1) len(3) w(2) h(2) nstrips(2)) |

`next - (rec+8) - n` is 0..3 (padding to 4). Frame numbers are contiguous 0..last in all 7
files, so there are no dropped or null frames. The video stream is a standard 320x240 Cinepak stream.
RF.STM uses greyscale codebook chunk IDs (0x24xx-0x27xx, 4-byte Y-only entries) and is genuinely monochrome.

## Playback (runtime)

- `FUN_00424b70(path, bufsize)` opens and plays an STM. It resets state (`FUN_00424a10`), starts the async reader
  (`FUN_00432a40` creates the thread `FUN_00432c10`), reads the 0x9A0 header (`FUN_00432dd0(h,1,0,0x458900,0x9a0)` +
  wait `FUN_00432ee0`), allocates `bufsize/chunk_size` chunk buffers (default `DAT_0044ae7c`), queues
  chunk reads (`FUN_00432dd0`), creates the DirectSound buffer (DSBUFFERDESC size 0x14, flags 0x80, bytes=hdr+0xB0,
  fmt=hdr+0x98), `ICOpen('vidc', hdr.fccHandler, ICMODE_DECOMPRESS)`, `ICM_DECOMPRESS_SET_PALETTE`,
  allocates the 8-bit frame buffer, then collects completed chunks (`FUN_00432fa0`) and pre-fills audio (`FUN_00425380`).
- `FUN_00425380` / `FUN_00425460` lock the DSound ring and copy PCM from the chunk list, padding with silence
  (0x00, or 0x80 for 8-bit) after the end.
- `FUN_004255d0`: audio clock. `ms = (played_bytes/4)*1000/(rate/scale)`. Current frame is
  `DAT_004592a0 = fps*ms/1000`. Video is slaved to audio, and audio byte 0 = frame 0 time 0 (no offset).
- `FUN_004256f0`: walks video records (`FUN_004259c0` = next record, crossing chunks) up to the current frame. When it is behind
  by more than 4 frames it skips ahead to the next keyframe. It decompresses with `ICM_DECOMPRESSEX` (0x403E; flags
  0x80000000 HURRYUP for frames it won't show, 0x08000000 NOTKEYFRAME), after `ICM_DECOMPRESSEX_BEGIN` (0x403C)
  from `FUN_00425a40`. `ICM_DECOMPRESSEX_END` (0x403F) runs in `FUN_00424a10`. Surface callbacks: `LAB_00425190`
  (lock), `LAB_004252f0` (unlock/blit), `LAB_00425330`.
- `FUN_00425a40`: starts playback (first frame and DSound Play). `FUN_00425d20` and `FUN_00426080` handle the per-tick update and end test (not fully traced).

## Usage by the game

Intro sequence table at `0x455A40` (0x14-byte entries: handler, filename, param, fade-in ms, fade-out ms):

| # | handler | file | |
|---|---|---|---|
| 0 | 0x4378E0 (still image) | TITLE\emi.rfa | EMI logo |
| 1 | 0x437A70 (movie) | TITLE\TWI.stm | "williams.avi", Williams Entertainment logo, 10.6 s |
| 2 | 0x437A70 | TITLE\Prolific.stm | Prolific Publishing lightbulb logo, 14.3 s |
| 3 | 0x4378E0 | TITLE\Silent.rfa (2500) | Silent Software logo |
| 4 | 0x437A70 | TITLE\RF.stm | Return Fire title intro (greyscale), 37.5 s |
| 5 | 0x437BE0 | - | end / go to menu |

The victory movie handler is `FUN_004375d0` (sequence entry at `0x455B00`). It plays
`PTR_004559D0[DAT_00459e20]` = {Win1, Win2, Win3, Win}. The index is set by `FUN_004370e0(side, level)`, which is called from
`FUN_0040f050` @0x40F1FA with `(DAT_00457100, DAT_00443868)`. `DAT_00443868` is the 0-based level number (the `-l` cmdline switch sets it).
The map table at `0x4559E0` is `{0,1,1,1,1,2,2,2,3,3}` for level 0..9:

| level (0-based) | movie | content | length |
|---|---|---|---|
| 0 | WIN1.STM | archival B&W celebration footage | 11.8 s |
| 1-4 | WIN2.STM | archival B&W footage | 22.7 s |
| 5-7 | WIN3.STM | archival B&W footage | 22.4 s |
| 8-9 | WIN.STM | long victory parade (stock footage with burned-in timecode) | 69.5 s |

`DAT_00459970` = first arg (probably the winning side/player). It offsets into the banner BMP table
`0x455A08` (BanBL/BanGL/BanBH/BanGH, blue/green, low/high res) drawn with the win movie.

## Per-file summary

| file | chunk | n | frames@15fps | video s | audio s |
|---|---|---|---|---|---|
| PROLIFIC.STM | 0x10000 | 43 | 215 | 14.333 | 14.333 |
| RF.STM | 0x10000 | 136 | 562 | 37.467 | 37.466 |
| TWI.STM | 0xE800 | 34 | 159 | 10.600 | 10.600 |
| WIN.STM | 0x10000 | 313 | 1043 | 69.533 | 69.533 |
| WIN1.STM | 0x10000 | 46 | 177 | 11.800 | 11.800 |
| WIN2.STM | 0x10000 | 103 | 340 | 22.667 | 22.666 |
| WIN3.STM | 0x10000 | 94 | 336 | 22.400 | 22.400 |

## Open questions

- The in-game image is Cinepak dithered to the 8-bit palette at hdr+0x594 (RF.STM has a different palette from the
  others). Our conversion decodes to true colour, which looks better. To match the original exactly, quantise to that
  palette. It is unclear whether this palette matches the DirectDraw palette active during playback. Check `LAB_00425190`/`LAB_004252f0`
  and the palette code in the sequence handlers.
- How the 320x240 frame is placed or scaled on screen (640x480 mode?) is not traced (`LAB_004252f0`).
- Exact meaning of `DAT_00457100` (first arg of FUN_004370e0) and of the 0xB4 header dword (always 0).
- Sequence-entry fields +8/+0xC/+0x10 (values like 0x3E8, 0x1F4, 0x9C4) are assumed to be hold, fade-in and fade-out in ms. Not verified.
