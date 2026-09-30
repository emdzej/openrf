# RFM level maps (`cd/WORLDS/{1PLAYER,2PLAYER}/LEVELn/RFMAPnnn.RFM`)

Status: **decoded**. The file is not compressed. Parser/renderer: `tools/rfm.py` (writes `out/maps/*.png` + `out/maps/summary.json`).

- There are **204 maps**, not 18: 100 one-player (`RFMAP001..100`) and 104 two-player (`RFMAP101..204`), spread over LEVEL1..9 in each folder.
- File sizes are 16756 bytes (no `VHCL` chunk, 154 files) or 16772 bytes (with `VHCL`, 50 files).
- Every map is 128x128 tiles, one byte per tile. A tile is 32x32 px, so a world is 4096x4096 px. In game coordinates (16.16 fixed point) a cell is 0x200000 wide and cell centres are at `n*0x200000 + 0x100000`.

## File layout

| off | type | meaning | used by |
|---|---|---|---|
| 0x00 | char[4] | `"WRL\0"` magic (lstrcmpiA against `0x441294`) | all readers |
| 0x04 | u8[4] | `54 4D 00 05`, constant; the game never reads it | – |
| 0x08 | u16 | width in tiles (always 0x80) | FUN_004322f0 |
| 0x0A | u16 | height in tiles (always 0x80) | FUN_004322f0 |
| 0x0C | u8[2] | `01 01`, constant | – |
| 0x0E | u16,u16 | DOS date, DOS time: created (e.g. 1995-07-21) | – (editor metadata) |
| 0x12 | u16,u16 | DOS date, DOS time: last modified | – |
| 0x16 | u8 | number of players, 1 or 2. Maps are filtered by this value (FUN_00416e10, FUN_00417190), and the loader takes it as the player count when its argument is < 0 | FUN_004322f0 |
| 0x17 | char[41] | author, NUL-terminated (`Unknown`, `MichaelAngelo`, ...) | – |
| 0x40 | u8 | must be non-zero or the loader refuses the map | FUN_004322f0 |
| 0x41 | u8[3] | `01 01 00` (202 maps) or `01 00 00` (Wolfsbane, The Whole). Not read by the game as far as I can see | ? |
| 0x44 | u32 | tile data size = w*h = 0x4000 | size check: `[0x44]+[0x48]==filesize` |
| 0x48 | u32 | tile data offset = end of the chunk area (0x174 or 0x184) | |
| 0x4C | u32 | 0 | |
| 0x50 | chunks | `{char tag[4]; u32 len (includes the 8-byte header); u8 data[len-8]}`, repeated while `off < [0x48]` | |
| [0x48] | u8[w*h] | tile bytes, row-major, row 0 = north/top | |

### Chunks (the order seen is always NAME, [VHCL], LEVL, EDTN)
- `NAME` (len 0x10C): the map title as a NUL-terminated string in a 260-byte buffer. The bytes after the NUL are left-over editor memory (fragments of the editor's own path, e.g. `...IRE\Image\Worlds\1Player\Level1\The Cakewalk.rfm`) and should be ignored. The title is copied to `DAT_00451320` and used as the registry key for progress (FUN_004146b0).
- `VHCL` (len 0x10), read by FUN_004320c0. A value of 0xFF means "use the default":
  - +0 ASV count (default 3)
  - +1 helicopter count (3)
  - +2 jeep count (8; must not be 0)
  - +3 tank count (3)
  - +4: goes to `DAT_0043fe1c` and then to FUN_00435040, which builds a per-team pool of up to 5 objects. When 0xFF, the value comes from the per-level table `0x443880` = `[0,0,0,1,1,2,2,3,3]`. **Meaning unknown**, probably the number of enemy AI units.
  - +5: number of **random mines** (must be < 201). The mines are placed by FUN_0041c9b0 on cells with no object that are ground or next to a road, and not within 2 cells of the home pad. With 0xFF: none in 2P; in 1P, `level*4` mines when level index > 5. The two maps with value 0 get no random mines.
  - +6..7: garbage.
- The same six values can also be forced from `[A#H#J#T#M#]` tokens in the map *path* (the `[` bracket part of the filename), also parsed in FUN_004320c0. No stock map uses this.
- `LEVL` (len 0xC): u32 difficulty level 1..9, which matches the LEVELn folder. It is stored as `DAT_00443868 = v-1` (clamped to 8).
- `EDTN` (len 0xC): u32. It is always `0x03270327` in the stock maps. FUN_00416e10 and FUN_00417190 skip maps with this value when counting or registering user maps, so it marks "shipped/stock edition".
- The game also keeps a CRC-16 (poly 0x8408, init 0xFFFF) in the registry for progress data (FUN_00414990 and others). That CRC covers registry records, not the RFM file.

## Tile byte → cell (loader FUN_004322f0 @ 0x004322f0)

The loader is called from FUN_0040b710. The default path table is at `0x451428` (`Worlds\1Player\Level1\The Cakewalk.rfm`). Its steps:
1. Load the file (FUN_00433620) and check the magic and `[0x40]`. Run FUN_004320c0 twice: once with defaults and once with the VHCL chunk. Read LEVL and NAME.
2. Clear the global `u32 cell[128*128]` at `0x45F3C0`. Every cell becomes terrain 2 (deep water).
3. The map is centred in 128x128 using `(128-w)/2, (128-h)/2`. For each tile byte `b`:
   - If `b >= 0xF0`, set `b = 0`.
   - Look up `TILE_TABLE[b]` at `0x452858`: 240 entries of `{u8 terrain, u8 object, u8 team, u8 handler}`.
   - If `terrain == 0xFF`, the cell becomes terrain 0 and nothing else happens (unused slot).
   - Otherwise set `cell.terrain = terrain`.
   - If `object != 0`, call `FUN_00417850(object, cell, 0, team)`. This sets `cell.object = object` and `cell.team = team`. If `OBJ[object].terrain_override != 0xFF`, it also replaces `cell.terrain` with that value. It then sets the strength bits and updates the radar pixel (FUN_00422930).
   - If `handler != 0`, call `(*0x452c18[handler])(cell, team, x_fixed, y_fixed)`:
     - handler 1 = `0x431FB0`, **home pad**. It is added to the per-team base struct at `0x480F50 + team*0xD0`, at most 4 pads per team. The count is at `0x480F88` (team 0) or `0x481058` (team 1). This is where vehicles launch.
     - handler 2 = `0x431F60`, **flag site**. Team 0 sites go to list `0x471C40` (count `0x472050`) and team 1 sites to list `0x471840` (count `0x472054`), at most 254 per team. After loading, FUN_0042fdd0 picks **one site at random** for each team's real flag (`DAT_0047181c` / `DAT_00471818`). All other sites are decoys.
   - The sum of all tile bytes goes to FUN_0041f190. It is probably an RNG seed.
4. The map is valid only if: team-0 pads > 0, team-1 flags > 0, and in 2P also team-1 pads > 0 and team-0 flags > 0. In 1P maps, team 0 is the player (pad, no flag) and team 1 is the CPU (flags, no pad).
5. Bridge fill:
   - Horizontal: from every cell with terrain 0x54 (84), walk east to the first 0x55 (85). Each cell in between gets object 74 `BNO_BRIGE_H`, with the team taken from the 0x54 cell.
   - Vertical: from every 0x56 (86), walk south to the first 0x57 (87). Each cell in between gets object 75 `BNO_BRIGE_V`.
6. Draw 3x3 radar markers around the pads: 0xCF for team 0 and 0x6C for team 1.

### In-memory cell word (`0x45F3C0[y*128+x]`)
| bits | meaning |
|---|---|
| 0-6 | terrain / ground-tile id (0..127). This id also indexes the 0x44-byte sprite descriptor table `DAT_00440c54 + id*0x44`, which is most likely the first frames of ART.CAR; see line ~16121 of all.c |
| 7-13 | object id (BNO_*), 0 = none |
| 14-15 | team: 0 = player 1 / human, 1 = player 2 / CPU, 2 = neutral |
| 25-27 | strength/hits from `OBJ[obj].strength`, clamped 1..15 but stored in 3 bits |
| 31 | radar shows 0xC9 when set (probably a mine or damage flag; not set by the loader) |

### Terrain id classes (from FUN_00418a30 "water depth", FUN_0041cd80/FUN_0041cdb0 "road", and the object overrides)
| id | class |
|---|---|
| 0 | plain land (grass). Tile byte 0 |
| 1 | shallow water (depth 1). Tile byte 20 |
| 2 | deep water (depth 2). Tile byte 1. Also the fill outside the map |
| 3 | land variant (dry). Tile byte 21 |
| 4..51 | shoreline/transition tiles. FUN_00418a30 counts them as shallow water. In the renders they form the beach ring |
| 52..72 | land-side transition tiles (dry) |
| 73..83 (0x49..0x53) | road pieces |
| 84/85 | horizontal bridge west/east abutment |
| 86/87 | vertical bridge north/south abutment |
| 88/89 | gate floor H/V (override from GATE_H/V) |
| 90/91 | home pad team 0/1 (tile bytes 57/77, handler 1) |
| 92..111 | ground under objects, set by overrides: 93 tree base, 94-96 planters, 97 turret/tower, 98/99 wall V/H, 100 hospital, 101 fuel, 103 prison, 104 ammo, 109/110/111 flag / damaged flag / destroyed flag |

I have not seen the art (ART.CAR is being decoded separately). The exact look of each of the 4..72 transition ids should be read from sprites 0..127 once `tools/car.py` exists.

### Tile byte table (`0x452858`)
The byte values look like an editor palette laid out in rows of 20:

| bytes | content |
|---|---|
| 0-19 | terrain 0,2,4,5,6,7,8,9,16,17,18,19,24,25,28,29,40,41,44,45 |
| 20-39 | terrain 1,3,10..15,20..23,26,27,30,31,42,43,46,47 |
| 40-47 | terrain 32,33,52,53,56,57,61,62 |
| 48 | BRIGE_H piece on terrain 0 |
| 49/50 | terrain 84/85 (H-bridge ends) |
| 52-55 | road 73,80,75,81 |
| 57 | **team-0 home pad** (terrain 90) |
| 60-67 | terrain 34,35,54,55,58,59,63,64 |
| 68 | BRIGE_V piece |
| 69/70 | terrain 86/87 (V-bridge ends) |
| 72-75 | road 79,74,78,77 |
| 77 | **team-1 home pad** (terrain 91) |
| 80-88 | terrain 36,37,48,49,65,66,67,68,60 |
| 93-95 | road 82,76,83 |
| 100-107 | terrain 38,39,50,51,69..72 |
| 120-137 | neutral objects (team 2): 120 BUSH_1S, 121 PALM_1S, 122 BUSH_2S, 123 PALM_2S, 124 TREE_S, 125 ROCK_1S, 126 ROCK_2S, 127/128 ROCK_1T/2T (on shallow water), 129-132 bush/palm variants on terrain 60, 133 CACTUS, 134 TREE_S on terrain 93, 135-137 PLANTER_BUSH/BERRY/STONE |
| 160-197 | **team 0** buildings. Same list as 200-237 |
| 200-237 | **team 1** buildings. The object id is the same as in 160-197 and only the team differs |
| unused | 51, 56, 58, 59, 71, 76, 78, 79, 89-92, 96-99, 108-119, 138-159, 178/179, 198/199, 218/219, 238/239 → terrain 0xFF (cleared) |

Team building list (byte offset from 160 or 200):

| offset | object | offset | object |
|---|---|---|---|
| +0 | PRISON_S | +20 | **FLAG** (handler 2, flag site) |
| +1 | BLDG_S | +21 | BLDG_E |
| +2 | BLDG_W | +22 | BLDG_N |
| +3 | FACT_S | +23 | FACT_E |
| +4 | FACT_W | +24 | FACT_N |
| +5 | BNKR_S | +25 | BNKR_E |
| +6 | BNKR_W | +26 | BNKR_N |
| +7 | HOSP_S | +27 | HOSP_E |
| +8 | HOSP_W | +28 | (object 0: nothing, team terrain 0) |
| +9 | FUEL_S | +29 | FUEL_E |
| +10 | FUEL_W | +30 | FUEL_N |
| +11 | GATE_V | +31 | GATE_H |
| +12 | WALL_V | +32 | WALL_H |
| +13 | TENT_V | +33 | TENT_H |
| +14 | TOWER | +34 | AMMO |
| +15 | WTOWER_NE | +35 | LARGE_TOWER |
| +16 | WTOWER_NW | +36 | WTOWER_SE |
| +17 | OUTPOST | +37 | WTOWER_SW |

In the maps, TOWER usually sits on both sides of gates, and LARGE_TOWER sits on wall corners and junctions.

## Object table (`0x451438`, 91 records of 0x38 bytes, max id in `0x451430` = 91)
Record fields:
- +0x00 u32 id
- +0x04 char* name (`BNO_*`)
- +0x08 ptr: graphics/animation data. Must be non-zero for FUN_00417850 to place the object
- +0x0C u32 flags/class
- +0x10 u8 terrain override (0xFF = keep)
- +0x11 u8 strength
- +0x14 ptr: per-frame function
- +0x1C ptr: on-destroy handler
- +0x2C ptr: sound/effect table
- +0x30 u32
- +0x34 u32 radar colour: low word for team 0, high word for team 1/neutral

`OBJ_TABLE` in `tools/rfm.py` lists all ids. The names are:

| ids | objects |
|---|---|
| 1-10 | bushes, palms, tree, cactus, rocks (`_S`/`_T` variants) |
| 11-13 | planters |
| 14 | AMMO |
| 15-17 | PRISON_S/E/W |
| 18-21 | WATCHTOWER_* (unused in maps) |
| 22 | FLAG |
| 23-26 | BLDG_N/S/E/W |
| 27-30 | FACT_* |
| 31-34 | BNKR_* |
| 35 | OUTPOST |
| 36-38 | HOSP_S/E/W |
| 39-42 | FUEL_N/S/E/W |
| 43/44 | GATE_H/V |
| 45/46 | WALL_V/H |
| 47/48 | TENT_V/H |
| 49 | TOWER |
| 50 | LARGE_TOWER |
| 51-73 | damaged/destroyed states (not placed by maps) |
| 74/75 | BRIGE_H/V |
| 76-89 | destroyed/decoration states |
| 90 | ACTIVE_TOWER |

## Observations
- Every map has exactly 1 home pad per participating team. The handler allows up to 4.
- 1P maps have 1..37 team-1 flag sites. 2P maps are symmetric, with equal flag-site counts per team (1..55).
- Object usage across all maps is dominated by palms, towers, walls and bushes.

## Open questions
- Header bytes 0x04..0x07 (`TM\0\5`, maybe an editor signature or version), 0x0C..0x0D and 0x41..0x43. The game never appears to read them.
- VHCL +4 (`DAT_0043fe1c`, the pool built by FUN_00435040): what kind of unit.
- The exact visual meaning of terrain ids 3 and 4..72. The 20-column palette suggests shore pieces by corner/edge type. This needs ART.CAR frames 0..127.
- The `(cell & 8)` test in FUN_0041cdb0 (random mine placement), which looks like a decompiler artefact or a terrain sub-flag.
- Header flags 0x42 = 0 in two maps (Wolfsbane, The Whole): no observed effect.
