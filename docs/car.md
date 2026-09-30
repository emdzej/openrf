# ART/ART.CAR and ART/TRANS.TBL

Decoder: `tools/car.py`. Output: `out/car/NNNN.png` (RGBA), `out/car/sheet.png` (labelled
contact sheet), `out/car/index.json` (per-sprite metadata), `out/car/palette.png`.

## Summary

ART.CAR is a relocatable memory image of **3DO Cel Control Blocks (CCBs)**, carried over from the
3DO original. The PC port kept the 3DO CCB struct but:
- converted the pixel data to **uncompressed 8-bit indexed pixels**, row-major, stride = width;
- appended `width`/`height` fields to the CCB (68 bytes instead of 3DO's 60);
- reused `PRE0` (3DO preamble word 0) as a **PC draw-mode selector**, and `PRE1` as a parameter.

All multi-byte values are little-endian. The whole file is loaded into one `GlobalAlloc` block
(size rounded up to 4, plus 0x4000 bytes of scratch space). Pointer fields hold file offsets and are
rebased by adding the block address.

There are 2165 CCBs (1949 unique source rectangles; the rest are aliases that reuse the same
pixels). There is no name table, so sprites are addressed by index (the code uses
`ccbBase + idx*0x44`).

## File layout

| Offset | Size | Contents |
|---|---|---|
| 0x00 | 16 | Header |
| 0x10 | count*0x44 = 0x23F14 | CCB array |
| 0x23F24 | count*8 = 0x43A8 | Scale-shift table, one entry per CCB |
| 0x282CC | 0x400 | Game palette, 256 x BGRx (same bytes as the `PS480.RFA` BMP palette) |
| 0x286CC | 0x404 | Win32 `LOGPALETTE` (ver 0x300, 256 entries): the DirectDraw palette |
| 0x28AD0 | 16 | PLUT A (4-bit -> 8-bit remap), used by CCB 1969/1971 |
| 0x28AE0 | 16 | PLUT B, used by CCB 1970/1972 |
| 0x28AF0 | .. EOF | Pixel data, referenced by `CCB.source` |

### Header (0x10 bytes)
| Off | Type | Value | Meaning |
|---|---|---|---|
| 0 | char[4] | `CCBA` | magic |
| 4 | u32 | 0x1D650F | file size. The loader rejects the file if this does not match; afterwards it is overwritten with the allocation size |
| 8 | u32 | 2165 | CCB count |
| 0xC | u32 | 0x23F24 | offset of the shift table (rebased; global `DAT_00440c58`) |

### CCB (0x44 bytes)
| Off | 3DO name | Type | Notes |
|---|---|---|---|
| 0x00 | ccb_Flags | u32 | 3DO flags. Only these are used on PC: bit31 `0x80000000` SKIP; bit30 `0x40000000` LAST (end of list); `0x20` BGND (colour 0 is drawn opaque rather than transparent); `0x1000` means "corners are explicit" (see below). File values: 0x7FE64400 (1847) and 0x7FE64420 (318, the opaque tiles) |
| 0x04 | ccb_NextPtr | ptr | 0 in the file; rebased if nonzero; draw-list link |
| 0x08 | ccb_SourcePtr | ptr | offset of the pixel data |
| 0x0C | ccb_PLUTPtr | ptr | 0x282CC (palette) for all but 4 CCBs; mode 0x11 remaps through a 16-byte PLUT |
| 0x10 | ccb_XPos | s32 16.16 | screen X (0 in the file). With flag `0x1000` set, 0x10..0x2C instead hold 4 corner points (x,y) in 16.16 |
| 0x14 | ccb_YPos | s32 16.16 | |
| 0x18 | ccb_HDX | s32 12.20 | 0x100000 = 1.0 |
| 0x1C | ccb_HDY | s32 12.20 | 0 |
| 0x20 | ccb_VDX | s32 16.16 | 0 |
| 0x24 | ccb_VDY | s32 16.16 | 0x10000 = 1.0 |
| 0x28 | ccb_HDDX | s32 | 0 |
| 0x2C | ccb_HDDY | s32 | 0 |
| 0x30 | ccb_PIXC | u32 | 3DO pixel-processor control word. **Not read by the PC renderer**; it is a leftover that correlates with PRE0 (0x1F001F00 normal, 0x1F009700 mode 1, 0x1F009B00 mode 2, 0x1F811F81 mode 3, 0x1F801F80 mode 5, 0xFFFFFFFF mode 0xD, 0x1F811F00 set to mode 0x10 at runtime) |
| 0x34 | ccb_PRE0 | u32 | **PC draw mode** (table below) |
| 0x38 | ccb_PRE1 | u32 | mode parameter (mode 0x10: 4 key colour bytes; mode 8/9: fill colour/level) |
| 0x3C | ccb_Width | u32 | pixels, also the source stride |
| 0x40 | ccb_Height | u32 | |

Screen corners (`Cel_ComputeCorners` 0x4260A0) follow 3DO semantics:
`P0=(X,Y)`, `P1=P0+HD*w`, `P2=P0+HD*w+VD*h (+HDD)`, `P3=P0+VD*h`. The HD values are 12.20 fixed
point, which is why the code computes `(HDX>>4)*w>>16`. `Cel_ComputeCorners2x` (0x426180) is the
pixel-doubled variant (`DAT_0043fcf0`, for 640x480 output). The textured-quad rasteriser is
`Cel_DrawQuad` (0x410B9C, edge walk) calling `Cel_TexSpanRows` (0x4104D5).

### Shift table (8 bytes per CCB)
`{s32 wshift, s32 hshift}` = log2(width), log2(height), or 0 if the dimension is not a power of 2
(verified for all 2165 CCBs). `Cel_SetScale` (0x4336C0) uses it to set HDX/VDY for a requested
on-screen size: shift if the value is nonzero, otherwise divide. `Obj_SetCel` (0x4338C0) stores
`&shift[idx]` next to its copied CCB.

### Palettes
- 0x282CC: 256 x BGRx. This is the *game* palette, identical to the palette in the `.RFA` BMPs.
- 0x286CC: `LOGPALETTE` passed to `CreatePalette` (`DAT_00481720`, `DAT_00481728`). Entries 0-9
  and 246-255 have flags=1 (PC_RESERVED) and RGB=0. They receive the Windows static colours at
  runtime. Entries 10-245 have flags=4 (PC_NOCOLLAPSE) and hold **BGRx game colours 0-235 shifted
  by +10**.
- **Sprite pixel values index the LOGPALETTE / DirectDraw palette** (the +10-shifted one), not the
  BGRx table. This was verified visually: water, sand, and grass tiles render correctly only with
  the LOGPALETTE. Normal, shadow, and blend sprites use only values 0 and 10-245. Values 1-24 occur
  only as levels/nibbles in mode-5 light masks and mode-0x11 4-bit PLUT cels. Values 246-255 never occur.
  In code, mode 10 does `GetNearestPaletteIndex(bgrxPalette, rgb) + 10`, which confirms the offset.
- Index 0 is transparent unless flag BGND (0x20) is set.

### Draw modes (CCB.PRE0), dispatcher `Cel_DrawList` (0x4262F0)
Tables (TRANS.TBL) are indexed in DirectDraw-palette space. `src` is the sprite pixel, `dst` the framebuffer pixel. Pixels with `src==0` are skipped unless noted.

| PRE0 | internal | Effect | # in file |
|---|---|---|---|
| 0 | 0/1 | copy; colour 0 transparent (internal 0) or opaque if BGND (internal 1) | 2072 |
| 1 | 2 | shadow: `dst = darken[4][dst]` (x27/32) | 11 |
| 2 | 2 | shadow: `dst = darken[2][dst]` (x29/32) | 21 |
| 3,4 | 3 | 50% translucent: `dst = blend[src][dst]` | 9 |
| 5 | 3 | additive light mask: `dst = brighten[src][dst]` (the blend path with the brighten base, so **src is a level 0-31**; sprites 1778/2077 use values 0-24) | 2 |
| 6 | - | untextured polygon fill (corners from +0x10), `FUN_0041124d` | 0 |
| 7 / 9 | - | flat fill `FUN_00411628` (colour 0 / PRE1 low byte) | 0 |
| 8 | - | `FUN_0041176c`, darken level from PRE1 | 0 |
| 10 | - | flat fill with 3DO RGB555 from `*(u16*)(PLUT+2)`, mapped to palette+10 | 0 |
| 0xB,0xF / 0xC / 0xE | - | other untextured primitives (`FUN_004118ba`, `FUN_0040fe4f`, `FUN_0041100f`) | 0 |
| 0xD | - | **span-list shadow mask** `Cel_DrawShadowSpans` (0x411BA5): `dst = darken[4][dst]` | 46 |
| 0x10 | 4 | key-blend: `src` in {PRE1 bytes 0..3, 0x0B} -> `blend[src][dst]`, else copy | 0 (set at runtime) |
| 0x11 | 5 | remap: `dst = PLUT[src]` (PLUT from CCB+0xC) | 4 |
| 0x12 | 6 | remap and blend: `dst = blend[PLUT[src]][dst]` | 0 |
| 0x13 | 0 | like 0 | 0 |

Internal mode 7 (`dst = table[src]` without a transparency test) also exists in the span loop.

### Mode 0xD shadow-span source encoding (46 sprites: 116,117,120,121,133,137,141,153,755,842,...)
This data is **not** w*h bytes:
```
s16 rowOffset[height]      // relative to SourcePtr; 0 = empty row
row data at SourcePtr+rowOffset:
  repeat { u8 x0; u8 x1; } until next x0 == 0   (the first pair is always read, so x0 may be 0 there)
  x0,x1 are in 1/256ths of the cel width: px = x*w/256, inclusive span.
```
Row sampling steps by `(height<<16 - 1)/screenRows`, so the mask is resolution independent. The
decoder renders it as 50%-alpha black.

## Runtime patches (Car_Load 0x4095F0)
- CCBs 524-578: PRE0=0x10, PRE1=0x7A7B7C7D (key colours 0x7D,0x7C,0x7B,0x7A plus 0x0B are translucent).
- CCBs 452-456: PRE0=0x10, PRE1=0x807C7A0A.
- CCBs 1981-1986: flags |= 0x70, SourcePtr = scratch buffer after the file (base+size rounded to 4),
  PRE0=6, PRE1=0, width=height=0x80. These are runtime-generated surfaces, so their on-disk pixels
  are meaningless.
- `Plut_BuildFadeRamp` (0x408F30) is called on (PLUT 1970, PLUT 1969) and (PLUT 1972, PLUT 1971),
  16 entries each. It builds 17 remap tables that lerp in RGB from A to B in 1/16 steps
  (`DAT_00440c5c`, `DAT_00440c60`), and is used to animate the 4-bit remapped cels.
- `DAT_00481730` = CCB 2141, `DAT_00481744` = shift[2141] (a special cel used by the HUD/map code).
- `DAT_00481620[256]` is set to the identity remap.

## ART/TRANS.TBL (81924 = 0x14004 bytes)
This file is a cache of colour-lookup tables, **generated by the game itself** (`Pal_InitTransTables`
0x4090C0) from the LOGPALETTE with `GetNearestPaletteIndex`. If the file is missing, has the wrong
size, or byte 0 is not 1, the game regenerates it and writes it back.
| Offset | Size | Global | Contents |
|---|---|---|---|
| 0 | 4 | - | byte0 = 1 (valid flag), rest 0 |
| 4 | 0x10000 | `DAT_0048172c` | `blend[a][b]` (index a*256+b) = nearest((pal[a]+pal[b])/2) |
| 0x10004 | 32x256 | `DAT_00481740` | `darken[k][c]` = nearest(pal[c]*(31-k)/32), k=0..31. `DAT_00481724` = k2, `DAT_00481748` = k4 |
| 0x12004 | 32x256 | `DAT_0048173c` | `brighten[k][c]` = nearest(min(255, pal[c]+3*(k+1))), k=0..31 |

These tables were verified bit-exact against a recomputation from the ART.CAR LOGPALETTE (nearest
by RGB squared distance over all 256 entries). A reimplementation can regenerate them instead of
shipping the file.

## Content (index ranges, approximate)
- 0-111: 32x32 opaque terrain tiles (sea/beach transitions, sand, grass, road, concrete, bridges).
- 112-460: trees and palms, rubble, fences, bunkers and turrets, tank/jeep/ASV bodies, tread strips,
  and shadow masks (mode 0xD).
- 461-660: wheels, soldiers, helicopter parts/rotors (translucent key-blend), ships/boats with wakes.
- 655-830: tiny infantry animation frames and debris.
- 830-1080: buildings and structures (flag towers, bunkers, houses, walls, domes) with destroyed
  variants.
- 1080-1790: explosions, fire, smoke, splashes, water rings, dust (effects animations).
- 1790-1900: team flags (animated, two colours), flag poles.
- 1940-1990: HUD/dashboard panels, radar, pulsing shots (1947-1963), 4-bit PLUT cels (1969-1972).
- 2000-2090: ripples/wake frames, dirt/grass/sky strips (2080-2089), map frame (2090).
- 2091-2124: vehicle side views and icons (selection screen: tank, APC, jeep, helicopter), small icons.
- 2125-2139: skull-with-helmet portraits (two team colours). 2140: digit table font. 2141-2164: small
  digits/icons.

## Ghidra names (renamed in project `returnfire`)
`Car_Load` 0x4095F0, `Car_Relocate` 0x409560, `Pal_InitTransTables` 0x4090C0,
`Pal_FreeTransTables` 0x409060, `Plut_BuildFadeRamp` 0x408F30, `Cel_DrawList` 0x4262F0,
`Cel_ComputeCorners` 0x4260A0, `Cel_ComputeCorners2x` 0x426180, `Cel_ExplicitCornersToScreen`
0x426260, `Cel_SetQuadCorners` 0x426C10, `Cel_DrawQuad` 0x410B9C, `Cel_TexSpanRows` 0x4104D5,
`Cel_DrawShadowSpans` 0x411BA5, `Cel_SetScale` 0x4336C0, `Cel_AddToList` 0x401220 (copies a CCB
into the per-frame list and clears LAST), `Cel_FlushList` 0x433730, `Obj_SetCel` 0x4338C0.
Filenames: `s_Art_art_CAR_00440c90`, `s_Art_Trans_tbl_00440c9c`. `DAT_00481738` overrides the CAR path.

## Open questions
- Semantic names for individual sprite indices. These are hard-coded in game code as `idx*0x44`
  offsets; xref-mining can map them.
- Exact behaviour of the unused untextured modes (6-0xC, 0xE, 0xF) was not traced in depth.
- Whether the `.RFA` BMPs are blitted with +10 index translation was not checked (see the RFA docs).
