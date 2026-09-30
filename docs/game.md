# Game simulation (objects, collision, vehicles) — port notes

Port: `src/game/` (C11, headless). Tests: `tests/sim_test.c` (driving), `tests/combat_test.c` (weapons/damage),
`tests/twoplayer_test.c` (2-player game, §11).
Table layout generated from the exe: `tools/gen_game_tables.py > src/game/game_tables.c` (addresses and
counts only; the contents are read from RFIRE.BIN at start-up by `exe_load()`, `src/exe.c`).

```
clang -Isrc src/game/*.c src/world.c src/world_tables.c src/assets.c src/exe.c src/render/models_data.c tests/sim_test.c -o /tmp/sim_test   # or: cmake --build build (sim_test, combat_test)
/tmp/sim_test            # run from the project root (data in ./cd); --map prints a larger map
```

| file | contents (original addresses) |
|---|---|
| `fixmath.[ch]` | FixMul 0x438300, Cos/Sin/Atan2Fixed 0x438360/0x438520/0x438310, AngleBetweenPoints 0x407c00, the rotation/heading tables of InitProjectionAndRotTables 0x4050f0, TurnTowardsAngle 0x423460, ApproachValue 0x41ed80/0x41edb0, MSVC `rand`, RandRange 0x4335f0 |
| `object.[ch]` | pool/lists/cell chains/delete queue/update (0x41dff0..0x41ecb0), timer queue 0x401000/0x401090, callback list 0x4011a0 |
| `shape.[ch]` | ShapesOverlap 0x41c800 and the part tests 0x41b8d0..0x41c3a0, FUN_0041c930 |
| `collide.c` | ObjCheckCollision 0x41dcf0, CollideWithCell 0x41dee0, CollideWithCellContents 0x41dac0, BNO touch handlers 0x41f260..0x41f6a0, BnoDestroy 0x417a00 / FUN_00417c20, SetCellStaticObject 0x417850, GetObjWaterState 0x4185e0 / FUN_00418770 |
| `vehicle.[ch]` | controllers 0x41edf0/0x41eee0, Vehicle class 0x428210/0x4283c0/0x428490, movement 0x428ca0/0x42a670/0x42abe0, per-type hooks 0x429f20..0x42b8e0, Storage (lift) 0x417f50..0x418470, wreck 0x4292d0.. (partial), bunker select view 0x404570..0x405440 |
| `game.[ch]` | cell build of LoadLevelMap 0x4322f0, pad/flag handlers 0x431fb0/0x431f60, FUN_0041f190, FUN_004320c0, GameSetup1P 0x41a480 / GameSetup2P 0x41a750, GameFrame1P 0x41a660 / GameFrame2P 0x41aa70 |
| `weapon.[ch]` | FireProjectile 0x431760, Missle/Death Missle/TRACER 0x430bb0..0x431700, Grenade 0x4319c0..0x431de0, Mine 0x434e40..0x434fd0, FUN_00418910 |
| `effect.[ch]` | Expl 0x4204c0..0x420a40 + ops 0x41fc50..0x4203b0, FWall/debris 0x4338c0..0x4347a0, Stay 0x435da0..0x435eb0, Shadow 0x434cf0/0x434d70 |
| `collide.c` (damage) | BnoDamage 0x417c20, BnoDestroy 0x417a00, FUN_00417960, TowerDestroyed 0x423510, FlagBuildingDestroyed 0x424170, HideFlagInRandomBuilding 0x424060, bridge 0x41f9a0 |
| `vehicle.c` (weapons) | TankFireCannon 0x429d30 (+FUN_0042f030 muzzle), Msv_Fire 0x42a010, Msv_Mine 0x42a310, Jeep_Grenade 0x42aa00, Heli_Fire 0x42b100, Heli_Missile 0x42b2a0, WreckUpdate 0x4293e0 + thinks 0x429740..0x4297f0 |
| `ai.h`, `turret.c`, `drone.c`, `sub.c` | enemies (§9): ActivateTurret 0x423c10 + Turret Gun 0x4235a0..0x423ac0, FUN_00417b50; Drone 0x435040..0x435c30; SUB 0x407320..0x4076f0 + the SUB branch of Mus_Director |
| `rules.h`, `man.c`, `gate.c`, `flag.c`, `mine.c`, `rules_tables.c` | soldiers, gates, the flag, random mines, EndGame (§10); tables from `tools/gen_rules_tables.py` |
| `game_tables.c` | generated: shape parts, graphic descriptors (+ `gfx_by_addr` table), BNO fields, shore shapes, vehicle descriptors, tunables, bunker menu/scripts, projectile types, explosion tables, debris programs (layout + loader; the explosion descriptors / scripts are read in place from the RFIRE.BIN image by `rf_u32` / `rf_ptr`) |

Ghidra: all hook functions that were not yet functions were created and ~100 functions/globals renamed
(`Tank_LaunchHook`, `Heli_Move`, `Storage_Rise`, `ShapePartOverlap`, `g_VehState`, `g_Teams`, ...).

## 1. Frame (GameFrame1P 0x41a660, sim part)
`g_dt` (DAT_0045f3a8, 0..12 ticks) and `g_tick` (DAT_0045f390, += dt) are produced by FrameTimingUpdate at the
end of the previous frame. Then: PollAllPlayerInputs (prev = cur) → Mus_Service → BunkerDoorReset 0x427fe0 →
TimerQueueAdvance(dt) → ObjUpdateAll → (radar, palette) → `view->mode(view)` (bunker select / zoom / play) →
RunCallbackList → HUD, Mus_Director, sound. All motion is `rate * dt` in 16.16.
GameFrame2P 0x41aa70 (`game_frame_2p`) differs in order: Mus_Service → BunkerDoorReset → TimerQueueAdvance →
ObjUpdateAll → (radar, palette) → **PollAllPlayerInputs** → view 1 mode → view 2 mode → RunCallbackList, and it
has no "game running" test between the objects and the views. So in 2 players the vehicles see the previous
frame's input words (one frame of lag) while the bunker screens see the fresh ones.

## 2. Object system
Pool `0x47261c`, 512 × 0x74; slot 0 unused (FUN_0041dff0 links slots 1..511 into the free list in order).
Lists (Amiga MinList): free 0x443dd0, active 0x443de0, inactive 0x443df0, "useless" 0x443e00 (recycled by
ObjCreate when the pool is full; classes with `cflags & 2` go there via FUN_0041e200).

| off | field |
|---|---|
| +00/+04 | list node |
| +08 | id: slot (bits 0-8) + serial (`+= 0x200` per ObjCreate) |
| +0c | flags: 0x20 on inactive list, 0x40 alive, 0x80 delete queued, 0x100 no collision, 0x200 crushed by lift, 0x1000000 engine sound / wreck-out-of-fuel, 0x2000000 first update (vehicle) / explode (wreck), 0x4000000 last move blocked, 0x8000000 |
| +10 | team (0,1; 2 neutral) |
| +14 | class |
| +18 | think (vehicles: control decoder; lift: state function; wreck: state function) |
| +1c | cell word pointer (or the outside pseudo-cell `*0x452d40` = 0x46f3c0) |
| +20/+24 | next/prev in the cell chain; chain head = slot in cell bits 16-24 |
| +2c/+30/+34 | parent / first child / next sibling (ObjSetParent 0x41ec00, class hooks +0x20..+0x2c) |
| +38 | delete-queue link (LIFO, DAT_00480e34) |
| +3c | graphic descriptor (+8 shape, +0xc radius, +0x28 position adjust) |
| +40/+44/+48 | x, y, z (16.16; x,y ∈ [0, 0x10000000)) |
| +4c | heading (0..0x3fffff, 0 = north, clockwise) |
| +50 | zeroed by ObjCreate |
| +54 | speed (signed px/tick) |
| +58.. | class specific: vehicle +5c team struct, +60 VehState*, +68 launch counter, +70 heli bob; lift +58/59 pixel offset, +5c team, +60 display model, +68 timer, +6c vehicle type, +70 hold flag; wreck +64 birth tick, +6c hit points |

ObjCreate(class, team, x, y, z, arg) 0x41e240: take free (or recycle), set id/flags/team/class/think=class+0x1c/
gfx=class+0x14/pos, AddTail(active), `init(class,obj,arg)`: 0 → back to free list, >0 → link to cell, <0 → keep
unlinked. ObjUpdateAll 0x41e970: reap delete queue; for each active object call `update`, advance to the
next node captured *before* the call, reap again (skipping a reaped next node). Destroy = class `destroy`
(0 vetoes), detach parent/children, sound detach, unlink cell, AddHead(free). ObjMove(obj,d) 0x41e7e0 saves the
old position (DAT_00480e38, used by swept shapes and FUN_0041e920), relinks the cell when the cell changes (or the
outside cell when off-map), runs ObjCheckCollision and restores everything on a hit (returns 1).

Timer queue (0x4568b0): delta-sorted list of `{rem, fn, a, b, period}`; advance(dt) subtracts dt from the head,
fires every expired entry with the carried-over remainder, callback result `<0` delete, `0` repeat same period,
`>0` new period.

### Classes (descriptor 0x48 bytes: id, name, init, destroy, update, gfx, ?, think, 4 attach hooks, prio,
hit_static +0x34, hit_object +0x38, damage +0x3c, +0x40, cflags +0x44)
| id | name | descriptor | prio | ported |
|---|---|---|---|---|
| 0 | Missle | 0x450950 | 0xc8 | yes (weapon.c) |
| 1 | Vehicle | 0x44b128 | 0x64 | yes |
| 2 | Turret Gun | 0x44ad40/0x44ad90 | 0x32 | yes (turret.c, §9) |
| 3 | FWall (debris) | 0x454f48 | 0 | yes (effect.c) |
| 4 | Shadow | 0x454fb8 | 0 | yes (projectiles/grenades; the heli's is synthesised by play.c) |
| 5 | Storage (lift up / down) | 0x443288 / 0x4432d8 | 0x6a | yes |
| 6 | Destroyed Vehicle | 0x44b0d8 | 0x68 | yes (crew bail-out = `spawn_man` hook) |
| 7 | TRACER | 0x4509a0 | 0x100 | yes (line-of-fire probe, for turrets) |
| 9 | Drone | 0x455778 | 0x6e | yes (drone.c, §9) |
| 10 | Mine | 0x455008 | 0x96 | yes (random placement FUN_0041c9b0: mine.c, §10) |
| 11 | Expl | 0x44aa50 | 0xc8 | yes |
| 12 | Flag | 0x44ae30 | 0xc9 | yes (flag.c, §10) |
| 13 | Gate | 0x44ade0 | 0x32 | yes (gate.c, §10) |
| 14 | MAN | 0x43fb10 | 0x7d | yes (man.c, §10) |
| 15 | SUB | 0x43fc10 | 0x7d | yes (sub.c, §9) |
| 16 | Death Missle | 0x4509f8 | 0xc8 | yes |
| 17 | Stay | 0x455820 | 0 | yes |
| 18 | Grenade | 0x450d48 | 0xc8 | yes |
Class +0x40 is the shadow graphic a new object gets (ProjectileInit writes the type's +0x30 there around
ObjCreate(Shadow)). Obj fields per class: see the unions in `object.h`.

## 3. Map cells for the simulation
`uint32 cell[128*128]` (0x45f3c0): bits 0-6 terrain, 7-13 BNO, 14-15 team, 16-24 object-chain head, 25-27 BNO hit
points, 31 radar flag. The port rebuilds these words from the RFM exactly like LoadLevelMap (tile table, 
SetCellStaticObject with hp clamp 1..15 stored in 3 bits, team bits only when a BNO is placed, pad/flag handlers,
bridge decks) — `World.cell` is only used for metadata. Quirk: the "fill with deep water" loop in the original
never advances its pointer, so only cell 0 gets terrain 2 (irrelevant for 128×128 maps).
The sum of all tile bytes seeds FUN_0041f190: a 16×16 table of per-cell offsets (x,y ∈ −12..12 px, +2 0..10,
+3 0..255) used by graphic adjust 0x41f210 to jitter bushes/palms/rocks (both for drawing and for collision);
the global `rand` state is saved/restored around it.

## 4. Collision
**ObjCheckCollision(obj)**: nothing if the gfx has no shape or flag 0x100. Tests the object's cell and its
N/S neighbours (each via CollideWithCell = the cell and its W/E neighbours), i.e. the 3×3 block; neighbour
rows/cols are visited when `(frac ∓ radius)` crosses the half-cell *or* `radius > 0` (y) / `radius ≥ 0` (x), so in
practice always. Off-map neighbours set a flag and the outside pseudo-cell's chain is tested last.
**CollideWithCellContents(cell, cell_centre, obj)**:
1. Static object: `BNO.gfx.shape` at the cell centre (+jitter). If overlapping: BNO touch handler (bit0 = block,
   bit3 = ignore), then the class `hit_static` (NULL = block). Vehicle `hit_static` 0x428c40 always blocks.
2. Every object in the chain: overlapping shapes → the lower-priority side's `hit_object` decides
   (`&1` block when called as self, `== 4` block when called as "other"; both NULL = block). Vehicle vs vehicle
   0x428c60 returns 5 → block. Lift 0x4182e0 damages (0x40000) or deletes whatever touches it and flags
   "lift blocked".

**Shapes**: a graphic's +8 is a list of parts ordered top-down: `{type, next, b8 pickup flag, b9 kind,
solid bits, mask bits, zlo, zhi, dx, dy, bbox[4], nverts, verts, nedges, edges}`. ShapesOverlap walks both lists by
height (`a.zlo+za <= b.zhi+zb` and vice versa) and returns `solid_a|solid_b` of the first pair with
`b.solid & a.mask` whose 2-D test passes (types: 1 point, 2 AABB, 3 convex polygon rotated by `heading>>16`
with the 64 matrices, 4 swept segment; segment tests in whole pixels). The pair is kept in DAT_00480ea4/ea8.
Vehicle hull = one polygon part, solid 2, mask 0x27, z 0..10 px (MSV 0..12): Tank/MSV 16×24 px, Jeep 10×16 px,
Heli pentagon 22×45 px. Static parts are solid 1, mask 0xff (walls z 0..19 px, towers 0..24, bushes 0..7);
bridge decks are solid 0x40 / z −50..0.5 px so vehicles never hit them; the heli at z = 50 px clears
everything (the tallest static part, the lift, reaches 32 px).

**Passability** (BNO touch handler → result for vehicles):
| BNO | handler | Tank / MSV / Heli-on-ground | Jeep |
|---|---|---|---|
| rocks 7-10 | 0x41f260 | drive over | blocked |
| bushes, cactus, planters (1,2,6,11-13) | 0x41f290 | crushed if `speed > 0.5 px/t` (damage 0x640000 → destroyed BNO, BNO+0x28 explosion), else blocked; parts with mask 4 ignored | blocked |
| tents 47/48 | 0x41f6a0 | crushed if `speed > 0.5` else blocked | same |
| fuel 39-42, ammo 14, gates 43/44 | 0x41f340 | parts with `b8 & 2` are pads: recorded in `state+0x68/+0x6c`, no block; other parts block | same |
| destroyed flag 63 | DestFlagOnTouch 0x4247e0 | a jeep picks up the flag lying in the cell, blocks | |
| palms, trees, walls, towers, buildings, bunkers, prisons, ... | none | blocked | blocked |
| bridge decks 74/75 | none (solid 0x40) | never collide | |
Terrain itself never blocks. Water is handled per frame by the water hook:
**GetObjWaterState** 0x4185e0: z > 1 px → 0; terrain > 0x33 or bridge BNO → 0; 1 → 1 (shallow); 2 → 2 (deep);
3 → 0; 4..23 → shore: overlap of the object's first shape part with the shore polygon (table 0x443558:
invert flag, rotation in 90° steps, 4 polygons) → 1 or 0; 24..39 → 1; 40..51 → inside the land box (0x443618) → 1,
outside → 2.

## 5. Vehicles
Descriptor pointer table 0x44afc8 → Tank 0x44b3a8, JEEP 0x44b690, MSV 0x44b978, HELI 0x44bc60 (0xb8 dwords;
the port's `VehicleDef` names every field, `[n]` = dword index). VehicleInit copies dwords 2..0x51 into the
**per-team** state 0x459390 + team*0x140 (only one live vehicle per team).

| field [idx] | Tank | Jeep | MSV | Heli |
|---|---|---|---|---|
| launch hook [5] | 0x429f20 | 0x42a410 | 0x42a250 | 0x42b3c0 |
| movement [6] | 0x428ca0 (default) | 0x42a670 | default | 0x42abe0 |
| armour [9] / hp [0xa] | 0.3 / 22 | 0 / 1 | 0.5 / 26 | 0.2 / 15 |
| water hook [0x13] | 0x4299e0 | 0x4299e0 | 0x4299e0 | none |
| max fwd [0x5a] px/tick | 1.05 (65.6 px/s) | 1.40 | 0.72 | 1.80 |
| max rev [0x5b] | −0.40 | −0.80 | −0.30 | −0.60 |
| accel [0x5c] /tick | 0.050 | 0.050 | 0.040 | 0.020 |
| friction [0x5d] /tick | 0.025 | 0.010 | 0.025 | 0.020 |
| turn [0x5e] /tick | 0x4000 = 1.41° | 0x8000 = 2.81° | 0x3333 = 1.13° | 0xc000 = 4.22° (with inertia) |
| fire A/B/C [0x5f..0x61] | cannon / cannon (lob) / – | grenade / TireOut-TireIn / drop flag | missile / missile / mine | gun / gun / missile |
| slot 0: proj, reload, ammo | 0, 20, 150 | 0 (grenade), 30, 16 | 8, 30, 100 | 7, 15, 100 |
| slot 1 | – | – | mines: reload 140, 10 (≤ team mines) | 6, 30, 50 |
| fuel [0x84] | 400 | 500 | 320 | 400 |
| dock radius [0x95] | 4 px | 9 px | 4 px | 32 px |
| dock hook [0x96] | – (dock) | 0x42ab90 | – | 0x42b600 (landing) |
| engine sound [0x90] | "tread" | "JeepStart" | "tread" | "Servo" |
| respawn delay [0x98] | 120 ticks | 120 | 120 | 200 |
| music [0xaf] | track 0, prio 0x80 | 4 | 6 | 8 |
| enemy arrow [0xa6] | yes | no (flag beacon) | yes | yes |
| models [0x52/53/55/58] | normal / splash / sinking / wreck | | | |
| sink depth [0x56] | 14 px | 13 | 14 | 14 |
Camera fields `[0x31]`, `[0x3a..0x51]`, `[0xb0..0xb7]` are dumped raw (renderer). Unknowns: `[0x57]` 0x1555,
`[0x91],[0x92]` (sound pitch?), `[0x99]`, `[0xa3..0xa5]`, `[0xa7..0xaa]`.

**VehState** (+0x140): +0 def, +4 wreck, +8 control word, +0xc pre-update hook (non-zero return skips the rest),
+0x10 movement fn, +0x14 fuel 16.16, +0x1c armour, +0x20 hp, +0x24+i*0x10 weapon slot (+8 next-fire tick,
+0xc ammo), +0x44 water hook, +0x48 splash/sink animation, +0x4c hit-flash tick, +0x50/+0x54 cannon elevation
and target (tank: 0 or 0x3b8e39), +0x58 turret yaw / MSV launcher / heli rotor spin, +0x5c turret target / jeep
beacon / MSV pending shot / heli dock cell, +0x60 pending shot / jeep timers, +0x68/+0x6c touched pickup cell/part,
+0x70 water state, +0x74 resupply-sound tick, +0x78 fuel-warning tick, +0x80/+0x84 jeep boat blend/target or
heli rotor angle/speed, +0x88 heli bank, +0x8c heli yaw rate, +0x90/+0x94 hover drift, +0x98/+0x9c heli
velocity, +0xa4..+0xac last thing bumped, +0xb0 analog target heading, +0xb4.. camera tracker node, +0xc8 object,
+0xe0..+0x13c camera params (+0xec lagged "pitch" = `approach(fixmul(speed, [0x3c]))` at 0x51e/0x20c per tick).

**Control word** (ControllerDecodeDigital 0x41edf0; scheme per type in team loadout +0xb0, 1 = analog 0x41eee0):
1 idle, 2 fwd, 4 back, 8 right, 0x10 left, 0x18 = steer to `state+0xb0`; A 0x20/0x40/0x80 press/hold/release,
B 0x100.., C 0x800.., 0x4000/0x8000 extra buttons; bits 16-23 = left/right magnitude, 24-31 = up/down
magnitude (raw input low 16 bits; the keyboard reader sets 0xff / 0xff00). Without the magnitude byte
the speed limit is 1/256 — scripted input must include it.

### VehicleUpdate 0x428490 (per frame)
1. First frame (flag 0x2000000): engine sound, ammo/fuel from the descriptor (MSV mines taken from team+0xbc),
   music request, idle-cell tick.
2. `ctrl = think(obj)`; run `state+0xc` hook.
3. `moved = move(d, ...)`; if moved: burn fuel `|speed|*dt >> 5` (0 → wreck), ObjMove(d). Blocked while
   turning → retry with the old heading (no bounce if the spot is free); blocked straight → `speed = -speed>>2`
   and flag 0x4000000. Not moved but turned into something → heading reverted.
4. Same cell for > 360 ticks → SpawnDrone, unless ≥ 3 enemy turrets were in view last frame (DAT_00471458, §9). Fuel < 1/8 → "FuelWarn" every 120 ticks.
5. Resupply (FUN_00429040) when standing still on a pickup part: fuel +0.5/tick, ammo +dt/2 per slot.
6. All three buttons (raw 0xe000000) = self-destruct (wreck flagged 0xa000000).
7. Standing still (`speed == 0` and heading unchanged) on own pad terrain within `dock_radius` of the centre:
   no button → door animation; A/B/C press → dock (tank/jeep/MSV: DockVehicleInBunker, heli: landing hook).
   Otherwise the fire functions get `ctrl`, `ctrl>>3`, `ctrl>>6`.
8. Water hook with `GetObjWaterState`; engine-sound update; enemy arrow every 60 ticks.

### Movement (per tick unless noted; `f` = terrain factor × (magnitude+1)/256)
Terrain factor FUN_00428e90: airborne 1.0; in water 0.75 (jeep in boat mode 0.25); jeep in boat mode on land 0.01;
road terrain 0x49..0x59 and bridge decks 1.2 (also sets road direction bits from 0x44af2c); else 1.0.
* **Ground (tank/MSV) 0x428ca0**: fwd `v = min(v + accel*dt, max_fwd*f)`, back `v = max(v − accel*dt, max_rev*f)`,
  turn `h ± turn*dt` (or towards the analog target), idle: friction towards 0; `d = dir[h>>16] * (v*dt)`
  (dir = (sin, −cos)). Tanks turn in place.
* **Jeep 0x42a670**: as above plus: turning without fwd/back counts as fwd; weak stick (<0x80) cancels turning;
  friction is applied whenever it is not turning (so the top speed is `max − friction*dt`); when not steering on a
  road/bridge it auto-steers along the road towards the lane centre (`(pos & 0x1ffffc − 0x100000)>>2`).
  TireOut (B in water) sets `boat_target = 1.0`; JeepFlagCheck morphs `boat` at 0x444/tick with control forced idle.
* **Heli 0x42abe0**: speed as ground without terrain factor; yaw rate `turn_vel` approaches ±turn×mag with
  0x7ae/0x1eb8 (not dt-scaled), strafe with the extra buttons (0.8 px/t sideways), bank ±3 units,
  velocity `vel` approaches the commanded vector at 0x7ae*dt, random hover drift ±0.03 when idle,
  climbs at 0.5 px/tick to z = 50 px, `SpawnSub` when > 1 cell off the map.

### Launch, docking, death, selection
* **Bunker select** (ViewEnterBunkerSelect 0x404d30): first type with stock; menu grid 0x43f1d8 (nav bytes
  up/down/left/right, skipping empty stock; repeats every frame while held); fade-in 0x11eb/tick; A = SpawnLiftVehicle.
  The per-type zoom script (menu +0x20: steps 0x4042b0 sound, 0x404530/0x404490 zoom, 0x4044e0 tilt, 0x4043a0 HUD,
  0x404430 fade-out) ends by clearing `lift+0x70`. ~190 ticks.
* **Lift (Storage)**: created at the pad at z = −32 px, heading south (heli 135°); waits for the release and for
  nothing on the lift, sets terrain 92, rises 0x4ccc/tick (~107 ticks), then restores the pad terrain,
  SpawnVehicle and destroys itself. Mines on the lift are removed; objects touching it take 0x40000 damage.
* **Leaving the lift**: tank/MSV forced full forward for 15 ticks (0x44b178); jeep waits 98 ticks then starts at
  half speed; heli spins up (0x49b/tick), rotor to 0x40000, takes off to 50 px drifting east.
* **Stock**: loadout 0x4438b8 → team+0xac; FUN_004320c0 sets T3 J8 A3 H3 (VHCL/filename overrides);
  VehicleInit decrements, DockVehicleInBunker increments (0xff = infinite). MSV returns unused mines.
* **Docking**: lowering lift (0x4432d8) at the cell centre, "Raise" sound, view fades to select once z < −16 px.
  Heli: brake → descend onto the pad centre (heading → 135°) → rotor spin-down → dock.
* **Death**: fuel 0, self-destruct, hp ≤ 0 (class damage 0x428f60: `hp −= dmg − armour`), or sinking below
  `sink_depth` in deep water. Wreck (class 6) schedules the return: out of fuel → 0x4280d0 (fade to select unless the
  last vehicle was a jeep), otherwise 0x4280b0 fly-back (camera → 0x4052d0 → 0x405360: no jeeps left → EndGame(−1)).
  Sinking deletes the vehicle and schedules 0x4280d0 directly.
* **Win**: jeep carrying the enemy flag on own pad terrain → EndGame(team) (JeepFlagCheck 0x42a480).

## 6. Weapons, projectiles, explosions, debris, damage

### Firing (per vehicle, VehicleUpdate step 7; `arg` = descriptor `fire_arg`, bits = ctrl, ctrl>>3, ctrl>>6)
Keys: A = H (0x08000000), B = J, C = K, extra Q/E (0x200000/0x400000 -> CW_X4/X5). Every shot: `next_fire` =
tick + reload, ammo-1, HudMarkDirty (hook `hud_dirty`, 0x40 slot 0 / 0x80 slot 1); empty -> "OutAmmo" 0x43edc8.
* **Tank** 0x429d30: A flat, B (arg −1) raises the barrel (elev 0 <-> 0x3b8e39 at 0x4ccc/tick; a shot pressed
  while it moves is kept in `fire_pending` and fired by the turret hook 0x429f50). Muzzle FUN_0042f030:
  `Rx(pitch*4)·(0,−6.75,0) + (0,−5.25,7)`; heading = hull + turret; pitch 0 or 0x38e38f (0x44b174). Muzzle
  flash = Expl 0x454550 attached to the tank (follows hull+turret, FUN_00420410), pitched by FUN_00420a40.
  Turret: Q/E ±0x4ccc/tick, C recentres.
* **Jeep** 0x42aa00: FUN_00431de0 picks the target: enemy vehicle on the ground within 61.2 px, else the
  damageable BNO last bumped (state+0xa4 = cell from `hit_static`) or an enemy object bumped (+0xa8/+0xac),
  else 48..62 px ahead ±4 heading steps; ThrowGrenade 0x431c80 (offset weapon arg = 5 px up).
* **MSV** 0x42a010: 3 launcher tubes (state+0x58 = tube, −6.0 after the 3rd / empty, the hook 0x42a280 reloads
  at 0x2666/tick while ammo left, sound 0x43ea98); pitch 0x4f5c flat (type 8) / 0x38e38f raised (type 9);
  2 points by FUN_0042f030 from 0x44d0b0 + tube offsets 0x44d088 (overlapping table): rocket start and the
  back-blast Expl 0x4544f0. C = Msv_Mine 0x42a310 (2-player only): SpawnMine 5 px ahead, not in deep water.
* **Heli** 0x42b100: obj flag 0x10000000 = selected weapon (C toggles, Heli_Missile 0x42b2a0, "0x43edb0"),
  0x8000000 = left/right muzzle (0x44f218/0x44f230) alternating; A (arg 0x3fffff) spreads the heading
  ±0x20000 and pitches guns to 0x71c71 (39° down), missiles get 0x3fffff; B fires level. Forward speed is added
  (FUN_00431880). Missiles get the flash 0x454580.

### Projectile types (0x450a78, 12 × 0x3c; `proj_types[]`)
`+0 class, +8 flags (1: offset rotated in x/y only, 2: pitch by FUN_00408400 and gravity *adds* pitch),
+0xc speed, +0x18 launch sound, +0x1c impact (0x431100 = SpawnExplosion(+0x34[hit kind])), +0x24 damage,
+0x28 gravity (pitch rate), +0x2c model, +0x30 shadow, +0x34 explosion table (0x450a48 / 0x450a60), +0x38 life
ticks, +0x39/+0x3a animation frames/period, +0x3b shallow-water radius`.
| type | used by | class | speed | damage | gravity | life |
|---|---|---|---|---|---|---|
| 0 | tank cannon | Missle | 3.0 | 1.0 | 0x2666 | 80 |
| 6 | heli missile | Missle (flag 2) | 3.0 | 4.0 | 0x6666 | 100 |
| 7 | heli gun | Missle | 3.0 | 1.0 | 0 | 80 |
| 8 / 9 | MSV rocket flat / raised | Missle | 2.3 | 2.0 | 0 / 0x1c28 | 90 |
| 10 | SUB homing | Death Missle | 3.0 | 25.0 | 0x1c28 | 90 |
| 3 | turret line-of-fire | TRACER | 6.0 | | | 90 |
ProjectileInit: vel = (0,−speed,0)·(P[pitch]·Y[heading]) (P = 0x48a7b0; positive pitch points *down*);
pitch ≠ 0 and gravity ≠ 0 -> ballistic flag 0x2000000. Update: life−dt (bytes), ballistic: pitch turned towards
0 (flag 2: pitch += gravity·dt) and vel rebuilt; ceiling z = 55 px; ObjMove (swept shape type 4, part
0x4494c0, solid 4 mask 0x43); z < 0 -> impact flag 0x200 and hit kind 0 ground / 2 road (terrain 0x49..0x53) /
1 water (deep, or shallow when a box of ±radius is wet, FUN_00418910). hit_static 0x4310b0: BnoDamage(damage),
kind 4; hit_object 0x431140: not the owner (parent), class damage, kind 3; never blocks. Life out = silent.
Quirks kept: the animation countdown +0x72 is never stored (frames advance only when dt ≥ period); a raised tank
shell (−39°) only levels out (turned towards 0) and flies on at the 55 px ceiling; the heli missile fired with A
first flies straight down (FUN_00408400 angle 0x3fffff = 90°) for one tick.
Explosions by hit kind: 0x450a48 = {0x453b58 dust, 0x4539d0 splash, 0x453d80 road, 0x453f80 object,
0x453ee0 static}; 0x450a60 = {0x453c58, 0x453a00, 0x453e48, 0x453f80, 0x453ee0}.
**Grenade** 0x431a10: closed-form arc from the throw point: speed = FixSqrt(FixDiv(3276·d, 0xb504)), per tick
`z = (speed·0.924 − 0.025·t)·t`, range `speed·0.383·t`, tumble 12 frames; damage 1.5 on hits; explosion by kind
from 0x450a60 (GrenadeDestroy). **Mine**: arms after 0x19ffff/0x2222 ticks (blinking team 0/2, "0x43e9c0"), then
graphic 0x449478, inactive list, team 1; vehicles set it off (0x434f70), damage > 1.5 too; explosion 0x454470
(damage box). Cell bit 31 = mine on radar.

### Explosions (class Expl; descriptors are interpreted in place from the RFIRE.BIN image, `rf_u32` / `rf_ptr`)
Descriptor dwords: `[0] script, [1] labels, [2] graphic flags (0x10 on the ground), [3] frames, [4] z-bias,
[5] frame rate/tick, [6] scale, [7] nverts, [8] verts, [9] nfaces, [10] faces`. ExplUpdate: frame = 1.0 on the
first update, then += rate·dt; destroyed at `frames`. Script bytes: 0 end, 1 yield, 2 wait-frame n, then the
table 0x44a9c0 `(obj, args, skip)`:
| op | fn | args | effect |
|---|---|---|---|
| 3 | 0x41fc80 | snd | sound 0x43ede0[snd] |
| 4 | 0x41fcc0 | | apply the cell's destroyed state (FUN_00417960) |
| 5 | 0x41fd10 | dx dy bno new | neighbour with `bno` -> `new` (wall damage states: WALL_H 46 -> R/LDMG 55/54 -> BDMG 56) |
| 6 | 0x41fd90 | i | graphic = explosion part 0x452dc8 + i·0x44 (ground flash, then the sprites) |
| 7 | 0x41fe00 | | graphic = 0x44aa20 (sprites only) |
| 8 | 0x41fe30 | f | frame = f |
| 9 | 0x41fe60 | n thr rng | skip the next n ops when RandRange(rng) < thr |
| 10 | 0x41fee0 | l | goto label l for even object ids |
| 11 | 0x41ff10 | | delete |
| 12 | 0x41ff30 | l | graphic = label l |
| 13 | 0x41ff60 | zlo zhi w h mask dmg | damage box (heap copy of 0x44aa20 + AABB part, solid 0x20); dmg < 0 = per tick; ExplUpdate then collides it (hit 0x4205f0/0x420640) |
| 14 | 0x420030 | a | box = ±a |
| 15 | 0x420070 | 2 bytes | nop |
| 16 | 0x420080 | | detach from the parent |
| 17 | 0x4200b0 | k | SpawnDebrisBurst of every part of the cell's BNO model with debris set k (k < 0: part/set from labels) |
| 18 / 19 | 0x420170 / 0x4201f0 | dx dy v | BnoDamage(255) the neighbour whose BNO / terrain is v |
| 20 | 0x420270 | n | run one of the next n ops at random |
| 21 | 0x420310 | t | remove the BNO now, put its destroyed state after t ticks (timer 0x420370) |
| 22 | 0x4203b0 | bno team | set the cell's side bits (destroyed fuel orientation) |
Drawing (render/view.c `draw_expl`, graphic 0x44aa20: hook 0x420680, draw 0x4206b0): the descriptor's quads,
`M = [scale]·[P[obj+0x57]]·[Y[heading]]·C`; per face (6 dwords: sprite, {first, last, fade, variant}, 4 vertex
indices; variant 1/4 = id bit 0/1 picks the 2nd face, 2/3 = id&3 / id&7 of 4/8) a cel `sprite + frame − first`
while first ≤ frame ≤ last; from `fade` on FUN_00407df0: hidden below 1/16, else drawn twice (the PIXC blend is
not used by the PC cel engine). Sprites are the ART.CAR cels 1080..1790.

### Debris (class FWall) and remains (Stay)
SpawnDebrisBurst(pos, team, set, part, heading, pitch, cb): every face of the model part whose flags bits 8-9
(class k) are set becomes a piece: ObjCreate(FWall, set[k−1]), Obj_SetCel = the face's 4 corners centred on
their mean, direction = centroid · recip(|centroid|). Debris programs (`DebrisDef` = rate, frames, fade frame,
steps of 8 dwords `{op, from, to, ...}`, table 0x454ee8): 0 end, 1 PLUT colour fade (not needed on PC: no-op),
2 speed along the direction, 3/4 yaw/pitch spin, 5 sprite animation, 6 gravity (0x4546b0/0x4546b4), 7 floor
(settle, inactive, z-bias −400), 8 damp, 9 gravity + squash on the ground, 10 splash/dust explosion at z ≤ 0.
Sets 0x454e18 (by explosion op 17 / face classes): 1 = vehicle wreck high, 5 = low, 6 = bushes, ...; tower turret
0x454730; bridge planks 0x44a0a0 (sprite 0xa6). The draw 0x433a60 removes settled pieces that come back into
view after 60 idle ticks (the renderer flags it, `effects_reap_drawn`). Stay (SpawnStay 0x435eb0): a burnt hulk
at rest; draw 0x435e40 wraps the hulk graphic, moves it to the recycle list tail, removes it when it is drawn
again after `life` and `linger` idle ticks.

### Damage
* **BnoDamage 0x417c20**: `v = (amount − armour)·mult >> 16` (min 1) off the 3-bit cell HP; nearly destroyed
  (h > 1, h − v < 2) with men flags (BNO +0x0c & 0xff8000) -> `spawn_men` hook (SpawnMenFromBuilding 0x4071f0);
  HP 0 -> BNO +0x1c hook or BnoDestroy. **BnoDestroy 0x417a00**: SpawnExplosion(BNO +0x2c, cell) at the cell
  centre (+jitter, + model offset for flag 0x10) — the script applies the destroyed variant (op 4 / 21), else
  FUN_00417960 at once: BNO +0x31 (+ terrain +0x30 + random variant by flags 2/4). Bushes crushed by a
  vehicle use BNO +0x28 (0x4545d0) and side bits 1.
* TowerDestroyed 0x423510: the turret part flies off as debris (pose of the live turret, DAT_0044ad30, or the
  jitter rest pose), then BnoDestroy -> BNO_DMG_TOWER. FlagBuildingDestroyed 0x424170: counts `flag_left`
  (nflags/2); if the destroyed building hid the flag (or the count ran out) the flag moves
  (HideFlagInRandomBuilding) or appears: `flag_spawn` hook. Bridge 0x41f9a0: 2..8 planks as debris (the
  original also writes the source's heading), then BnoDestroy -> BNO 76.
* **Vehicle 0x428f60**: `hp −= amount − armour` (if > 0), hit flash state+0x4c; hp ≤ 0 (or the damage hook) ->
  wreck (death hook if any); hp < −40 explodes the wreck at once. **Wreck** 0x4293e0: falls (FUN_004337d0),
  coasts with friction, sinks in deep water (explosion 0x454300), on OF_NEW explodes (0x44b0c8[type]) into
  debris (sets 1/5 by height) and keeps the def[0x59] graphic; blocked -> Stay (burnt hulk) or crew bail-out.

## 7. Test output (tests/sim_test.c, level 1 "The Cakewalk", pad (67,67))
Tank: lift 294 ticks; rolls 16 px off the lift; 1.26 px/t on the road, 1.05 on grass; crushes the bush at (67,71);
blocked by a palm at x=2076; 0.79 px/t in shallow water; sinks at row 80 and is back in the select screen
146 ticks later. Docking on the pad returns the stock. Jeep: 1.38 px/t (1.62 on road), blocked by palms, TireOut in
shallow water, crosses deep water at 0.32 px/t; self-destruct → fly-back → select. Heli: rotor spin-up ~215 ticks,
climbs to 50 px, 1.8 px/t over trees and sea, yaw with inertia.

**tests/combat_test.c** (`build/combat_test`; test range east of the pad cleared, vehicles spawned directly):
bush: 1 shell -> BNO_DEST_BUSH_1 (2 debris); wall column WALL_V: 5 shells, neighbours -> RDMG/LDMG_WALL_V,
cell -> terrain 0x6a; building BLDG_S: 5 shells -> BNO_STD_DEST; tower: 5 shells -> 5 turret debris,
BNO_DMG_TOWER; raised tank shell levels out at 55 px; jeep blocked by a bush, 1 grenade destroys it; MSV
rockets fly over bushes, 3 destroy a factory; heli guns hit the ground 68 px ahead, 3 dropped missiles destroy
a building, guns break a bridge deck (5 planks, BNO 76); flag building -> `flag_spawn` hook; enemy tank: 32
shells (0.7 each) -> wreck -> Stay. In-app: `OPENRF_DEMO=fire|jeep|msv|heli` places a bush, building, wall and
tower west of the pad and fires.

## 8. Open questions / not ported
* Radar blips of drones (FUN_00422c70(drone, 0x4557e0, 1000)) are not drawn (HUD movers); of Mus_Director only the SUB
  (0x1000) and flag (0x100/0x200, play_rules.c) branches are ported; 0x400 / vehicle-death states are not.
* Debris colour fade (step 1, a 3DO PLUT operation) is a no-op; the projectile height recorder
  DAT_00459680 (debug, never read) is not ported.
* Likely original race: a second lift launched while the previous (lowering) lift still exists is deleted by
  the old lift's `hit_object` (both are class 5 with equal priority), leaving the view without a vehicle.
  The test waits for the old lift to disappear.
* Swept part vs polygon (type 4 vs 3) compares the *relative* sweep vector with absolute edges (as in the
  original); only matters for projectiles.
* x87 `fsin/fcos/fpatan` vs libm may differ in the last ulp → rare off-by-one in the tables.
* Meaning of VehicleDef `[0x57]`, `[0x91..0x93]`, `[0x99]`, `[0xa3..0xaa]`, weapon `arg[]`; team+0x10 usage;
  who sets team+0x28 (door cell).

## 9. Enemies: tower turrets, the Drone, the submarine (`turret.c`, `drone.c`, `sub.c`, `ai.h`)
Setup: `ai_level_start()` after `game_load` (FUN_00435040 drone pool, counters, `game_hooks.drone/.sub`);
`ai_frame_end()` after `game_frame` (Mus_Director's SUB branch). `ai_hooks`: drone hum play/stereo/kill,
listener positions. Test: `build/ai_test`. In-app: `OPENRF_DEMO=turret|drone|sub` (towers NW of the pad /
3 drones per team / the heli is put near the west edge once airborne and flies off the map).

### Turrets (class 2 "Turret Gun", 0x44ad40 normal / 0x44ad90 large, prio 0x32)
* **Trigger** — Model_TowerHook 0x4174e0 is the depth hook of the tower models 0x441840 / 0x4418a0 (BNO 49
  BNO_TOWER / 50 BNO_LARGE_TOWER): while a tower cell with HP > 0 is queued for drawing, if the view's vehicle
  (view +0x6c) exists, is of the other side and is class 1 (Vehicle — not a wreck or a lift) it calls
  ActivateTurret(cell, vehicle); on success the static turret part is skipped (DAT_0045f398 = −1) and the new
  object (linked at the head of the cell chain) is drawn in the same pass. So a turret wakes exactly when its
  tower is inside the rendered cell area of the enemy player's view. HP 0 → the turret part is not drawn.
  Port: `RenderMap.tower_hook` → play.c `on_tower` → `turret_tower_hook`.
* **ActivateTurret 0x423c10**: ObjCreate(class by BNO 50, cell team, cell centre, z 0, arg = vehicle); obj +0x60
  byte = BNO, +0x61 = HP, cell BNO := 90 BNO_ACTIVE_TOWER (graphic 0x441538 hidden by hook 0x4174a0, no shape —
  shots hit the object); yaw/pitch from the cell jitter (FUN_004174b0: yaw = (j[3]&0x3f)<<16, pitch = j[0]<<14),
  +0x6c = rest yaw. TurretInit 0x4235a0: parent = the vehicle (target), +0x62 unseen = 0, +0x64 wake =
  tick + (RandRange(300) & 0xff), +0x68 next shot 0, +0x70 muzzle 0x441308 (0, −15, 24.5 px) / large 0x441580
  (z 29.5 px), `DAT_00472048[team]++`.
* **TurretUpdate 0x423620**: runs think while think ≠ 0, HP byte > 0 and `(short)(+0x62 += dt) < 301`; else
  ObjDestroyNow. The draw (Model_TowerHook with the object) zeroes +0x62 and uses obj yaw/+0x5c pitch/team.
* Thinks: 0x423760 wait for the wake tick → **aim 0x423780**: no target → rest. Yaw → AngleBetweenPoints at
  0x3333/tick (1.13°); only once aligned and dist2d < 192 px: pitch target = Atan2(dist, muzzle z − target z) >> 2,
  values in (0x60000, 0x3a0000) snapped to ±33.75° (≤ 0x200000 → 0x60000 down, else 0x3a0000 up); otherwise
  pitch target 0. Pitch at 0xa3d/tick. In range and `+0x68 < tick` → **fire 0x4239e0**: TRACER (type 3) from the
  muzzle; `DAT_0046f3c4 == 0` (nothing of the turret's side — own-team static cell or own view vehicle — in
  the line) → type-4 shell (4 px/t, damage 1.5, 150 ticks, "Missle" sound), next shot +60; blocked → +30.
  **Rest 0x423ac0** (target gone, e.g. the vehicle died/docked): yaw back to +0x6c, pitch to 0, think = 0.
  Both aim and fire keep a per-tick "nearest turret per target side" (DAT_0044ad38 / DAT_00471820; never read).
* **Damage 0x423710**: FUN_00417b50 = BnoDamage on the cell HP without the destroy step ((amount − armour)·mult
  >> 16, min 1); 1 at HP 0 → ObjMarkForDelete. **TurretDestroy 0x423660**: count−−, cell BNO restored from +0x60;
  HP left → it is simply the static tower again (the next draw uses the jitter pose; after a rest the
  yaw matches, the pitch snaps to the jitter pitch); HP 0 → DAT_0044ad30/DAT_004588c8 := live yaw/pitch, HP := 1,
  BnoDamage(0xff0000) → TowerDestroyed 0x423510 sends the turret part flying in the live pose, then BnoDestroy
  (explosion 0x453228 → BNO_DMG_TOWER).
* Tank vs large tower (ai_test, level 2 "Cornered", tower (74,31)): wakes with the tank 165 px away, first shell
  after the reaction delay + turn, −1.2 hp per hit on the tank; 5 tank shells destroy it (5 debris, BNO 60).
  Target gone → rest in 175 ticks; not drawn → static after 300 ticks.

### Drone (class 9, 0x455778, prio 0x6e, graphic 0x455680, shadow 0x455730)
* **Pool** FUN_00435040(n ≤ 5): records (0x20 bytes, 0x4597b8) per team: next, obj, +8 last draw tick, +0xc team,
  +0xd muzzle, +0xe shot down, +0x10 killer heading, +0x14 next shot, +0x18 bumping drone, +0x1c gun pitch.
  n = VHCL pool (G.ai_pool) or the level table 0x443880 {0,0,0,1,1,2,2,3,3} — level 1–3 have none.
* **SpawnDrone 0x435950**(target) (idle rule, VehicleUpdate): side = RandRange(4), box = ±def[0x9f]/[0xa0] half
  cells (tank ±512 px, jeep ±256) around the target; FUN_00435c30 picks a random point on that side, rejected
  inside another team vehicle's box; up to 4 sides. Team = target team ^ 1, z = 50 px, parent = target,
  Shadow object (class +0x40 = 0x455730), radar blip (not ported). The first live drone starts the looping
  "Drone" event 0x43e9a8 unowned with per-listener sources DAT_004598f8 / DAT_00459904 (1P: both the first);
  DroneUpdate moves them to the drone nearest each listener every tick; killed (cmd 9) with the last drone.
* **Flight 0x435620**: velocity approaches speed·dir(heading) by 0x3d7·dt; speed +0x4ccc·dt up to 1.5 px/t;
  z → 50 px at ≤ 0x4ccc·dt (dropped when blocked); roll/pitch turned (0x8000) towards |v|·(−6 | −3.5)·dir(heading −
  angle(v)); a drone that touched it (hit_object 0x435900 records it; block 5 unless one of the parts is the
  64-px proximity part 0x455600) pushes it apart by `0x20000 − dist/16`.
* **Attack 0x435420** (default think): turn rate 0x6666 when the target is within ±90°, else 0x3333 (≤ 75 px) /
  0x10000; gun pitch (relative to the body, limit 0x50000 down only — the "up" limit compares with `0x40 − 0x50000`,
  a typo in the original, so it never clamps) at 0x4ccc; fires when aimed, 32 < dist < 80 px and heading within
  ±5°: airborne target (z > 2 px) → TRACER probe first (blocked → wait 10); type 11 (3.5 px/t, 0.95 damage) from
  alternating muzzles (∓8, −8, 0) px, reload 10. No target → **0x435840** flies on, deleted 60 ticks after its
  last draw. **Damage 0x435880**: < 1.0 ignored; a Missle knocks it by half its velocity; marks it shot down →
  DroneDestroy: explosion 0x454098 + debris of the drone model (set 0x454e28, velocity of the drone added,
  FUN_004352c0). Draw 0x435090: verts 0..7 (guns, 0x4552f8) by P[gun pitch], verts 42..47 (rotor, 0x455358) by
  Y[(id + tick) & 15], Model_DrawTurret with pitch obj+0x68, roll obj+0x58; shadow draw 0x435120 (team 0).

### Submarine (class 15 "SUB", 0x43fc10, prio 0x7d, graphic 0x43fbb0)
* SpawnSub 0x4076f0 (Heli_Move, heli > 1 cell off the map; one at a time, DAT_0048bb1c) → team 0 at (0,0,0).
* Update 0x4073a0: sets 0x1000 in the music flags (Mus_Director: state < 6 → Mus_Request(16 MUS_SUB, 0x8a),
  gone → state 1 → Mus_Request(−1, 100)); runs the think chain; frame +0x5c by 0x3333·dt: state 1 rises to
  0x14 and loops 0x14..0x19, states 0/2 fall back to 0 (from the loop it wraps below 0x14 first).
* Thinks: **0x407470** idle: the team vehicle on the off-map cell that is farthest out; +0x60 += dt, after 240
  ticks jump to it (+0x20, z 0) and parent = it; nobody out → frame 0 → think 0 (removed). **0x407580** surface
  (state 1) while the target is still off the map and `drawn ≤ tick + 60` (always true — the draw stores the
  tick); at frame ≥ 0x14 → **0x407620**: 180 ticks later FireProjectile(pos + (−32, 0, 0), heading 0, pitch
  0x355555, type 10 Death Missle, team 0, owner = the target → the missile homes on its parent), parent = the
  missile → **0x4076b0** stays up while it flies → **0x4075f0** dive (state 2) → idle again.
* Draw 0x407320: obj +0x64 = tick, sprite index = frame − 1 (hidden at 0), Model_DrawStatic.
* ai_test: heli leaves the west edge, SpawnSub at once, surfaces under it 341 ticks later, missile 181 ticks
  after that; 25 damage destroys the heli; the sub dives and is removed.

## 10. Soldiers, gates, the flag, random mines, end of game (`man.c`, `gate.c`, `flag.c`, `mine.c`, `rules.h`)
Setup: `game_load` calls `rules_level_start(G.random_mines)` where StartNewGame runs FUN_0041c9b0 (after the map,
before GameSetup1P); it installs `game_hooks.spawn_men/spawn_man/flag_spawn` when unset. Tables:
`python3 tools/gen_rules_tables.py > src/game/rules_tables.c`. App glue: `src/play_rules.c`, `src/endgame.c`,
`src/highscore.c`. Test: `build/rules_test` (all checks must pass). In-app: `OPENRF_DEMO=rules` (gate across the
tank's path, the enemy flag, a building and a prison letting men out), `OPENRF_DEMO=win` (EndGame(0) at once).

**RandRange 0x4335f0** is `(uint32)(2·rand15·n) >> 16` (it wraps for n ≥ 0x8000; `rand_range` now uses unsigned
arithmetic). **Flag site pick FUN_0042fdd0**: DAT_0044f2dc = 0, so the value is FUN_0042bbe0 = three calls of the
global CRT `rand()`: `(r3 & 3) + (r1·0x8000 + r2)·4`, scaled by `((u·2 & 0xffff)·n >> 16) + (u & 0x7fffffff) >> 15)·n) >> 16`;
team 1 is picked first, after the jitter table (whose rand() save/restore leaves the state = that rand() value).

### MAN (class 14, prio 0x7d, graphic 0x43f900 walk / 0x43f998 swim, shape 4×1.5 px solid 1)
* **SpawnMenFromBuilding 0x4071f0** (BnoDamage when the hit points drop to 1 from >1 and BNO +0x0c & 0xff8000):
  d = bits 20-23 − bits 16-19 (buildings/factories 2, bunkers 4, outpost 3, hospitals 5, prisons 4, tents and
  watchtowers never reach it); d + RandRange(d) men of the cell's side — prisons (flag 0x8000) give the *other*
  side (freed prisoners); x = shape centre + x0 + RandRange((x1 − x0) >> 17) px (west half), y = shape centre −
  (RandRange(−y0) & ~0xffff): the 16.16 extent wraps RandRange to < 1 px, so y is always the centre line. Returns d.
* **ManInit 0x4062c0**: frame 6, drawn-tick = now, grenades `0x43fa88[RandRange(16)]` (0..4), heading
  `((RandRange(8) − 4) & 63) << 16` (north ±22°). Class think **0x406cd0**: walk out of the building in 4 px steps
  along the heading with flag 0x1000000 (statics / men are recorded, not blocking); stuck in something other than
  a man → removed; in a man → try the 8 spots of 0x43fab0 round it (else removed). ManUpdate 0x406320 runs the think
  until it returns 0 (NULL think = destroy), **removes men not drawn for 120 ticks** (the draw 0x4061c0/0x406280
  stamps obj+0x5c; rules_test "draws" them every frame), then animates obj+0x64 by 0x3333·dt per state +0x72:
  0 walk frames 0..5, 1 stand → 9, 2 throw → 6, 3 swim 10..17, 4 tread water 18..19 (graphic 0x43f998 for 3/4).
* **0x406bc0 pick**: nearest of each side's current vehicle (or its latest wreck, FUN_00406180 / DAT_00456a18,
  id-checked) by dist2d → ObjSetParent(man, it); within 256 px → **0x4065f0**, else **0x4065b0** stand; timer
  now + 30 + RandRange(90); ±2 heading steps offset (used once).
* **0x4065f0** runs *away* from that vehicle (own or enemy) at 0.2 px/tick: blocked by an object → the three
  headings 180°/270°/90° from it that stay within 90° of the flight; by a static part → north/south if outside the
  part's x range, then east/west if outside its y range. Standing still at frame 9 next to an enemy (< 96 px) with
  grenades → **0x406a90**: frame down to 6, ThrowGrenade at the vehicle ± RandRange(d/4 px) twice per axis, wait
  120 + RandRange(60) (**0x406b90**). Timer out: 1/3 chance of a grenade at 64..96 px, else pick again.
* hit_static 0x4064b0 records the cell; hit_object 0x4064d0: a Vehicle (class 1) crushes the man ("ManCrush",
  a dead-man Stay 0x43fa30 sprite 806 + RandRange(3)·2 + team, 60/120 ticks, unless in water), anything else is
  recorded and blocks; damage 0x406550 kills the same way. Vehicles (prio 0x64) drive through men (the man's
  hook answers ≠ 4). SpawnMan 0x4071c0 = wreck crew bail-out.
* Draw 0x4061c0: Model_DrawStatic with the face pair of the heading octant (0x43f480 team 0 / 0x43f680 team 1:
  shadow 755, figure 655/665 + 20·{0,1,2,3,4,3,2,1}[octant]) and the frame as item +8 (face flag 8 offset);
  0x406280: Model_DrawYaw, sprite 756 + 25·team + frame.

### Gate (class 13, prio 0x32)
* The vehicle resupply check FUN_00429040 on a pickup part of kind 3 (the gate zone 0x4424f8, ±32 px) of an own
  gate (BNO 43 H / 44 V) → **SpawnGate 0x423fb0** (needs hit points): Gate object at the cell centre, parent = the
  vehicle, obj+0x60 = BNO, the BNO is cleared from the cell (the object draws it). GateInit 0x423cd0 copies the
  graphic with its two door parts (0x4428a8+0x88 / 0x442d70+0x88; flag +0x100 = V), target 15 px, "GateMove".
* GateUpdate 0x423d50: removed when the cell's hit points are 0; opening approaches the target at 0.5 px/tick;
  the door parts sit at ∓(16 + opening) px on x (H) / y (V); fully open and the parent no longer overlaps the zone
  (or gone) → target 0 + "GateMove"; closing and ObjCheckCollision(gate) blocked → reopen (vehicles never block
  it: their hit_object answers 5 ≠ 4); shut for 60 ticks → deleted ("GateClose" when it reaches 0).
  GateDestroy 0x423ec0 puts the BNO back; with 0 hit points it sets 1 and BnoDamage(0xff0000) destroys it.
  damage 0x423f50 = FUN_00417b50 on the cell (delete when it reaches 0); hit_static 0x423fa0 = 0.
* Draw wrappers 0x4175a0/0x417610/0x417680/0x4176f0 (view.c `draw_gate`): verts 9,10,13,14 x (H) / y (V) =
  ±(16 px − whole-px opening), top face (6 H / 7 V) cel 0x35a while open, 0x359 closed / for map cells.

### Flag (class 12, prio 0xc9, graphic 0x44e740 lying / 0x44e610 carried)
* **FlagBuildingDestroyed 0x424170** (collide.c): each destroyed own flag building decrements `flag_left`
  (nflags/2); while ≥ 0 destroying the building that hides the flag moves it to another intact one
  (HideFlagInRandomBuilding 0x424060); when the count runs out (or no building is left) the **Flag** appears in
  that cell (`flag_spawn`): radar blip 0x44e7a8 pri 10000 (8 px flag, colours 69/103, blinking with the empty
  0x44e7b8 every 15 ticks), home position DAT_004588b0, music bit 0x100 (Flag Discovery, prio 0xfe), and a
  120-tick camera tracker (zoff 10 px, pitch 0x180000, height −150 px) for the other side if its vehicle is within
  320 px. The building becomes BNO_DMG_FLAG (hp 6); shot down to BNO_DEST_FLAG (63) its touch handler
  **DestFlagOnTouch 0x4247e0** lets a jeep take the flag lying in the cell (the rubble still blocks).
* **FlagOnTouch 0x424760** (hit_object): any side's jeep takes a free flag unless it just dropped it (+0x70) or
  already carries one: "Ding", ObjSetParent, moved behind the jeep in the update list (FUN_0040b5a0).
* FlagUpdate 0x424380: lying: falls to z 0, drifts back to the last dry position at up to 0.1 px/tick when in
  water, heading 0; carried: at jeep + Y[heading]·(3.75, 6.75, −2) px, heading turned (0x4ccc/tick) towards the
  jeep's clamped to 0x471c7..0x1b8e38 / 0x2471c7..0x3b8e39, music bit 0x200 (Flag Pickup, 0x8a) when the carrier is
  of the other side; cloth frame +0x5c (0..3 up, 4..9 loop, 10..12 down) while lying / moving. Draw 0x42f740 /
  0x42f6e0: sprite + 13·team + frame; the lying flag pitched by (view pitch − 0x190000) >> 3.
* Jeep button C (0x42ab70 → **FUN_004248a0**): drop the carried flag (own side's first), or take one whose own
  collision test reaches the jeep (a flag lying in the rubble answers the static part first: back into it).
  A jeep dying drops it (children detached). Jeep docking (0x42ab90) with its own flag, or a lift (StorageUpdate
  0x417fb0, 3×3 cells) next to its own flag → "Ding" + re-hidden; an enemy flag next to a lift → TeamWins.
  Remains (Stay) next to a lift are blown away (explosion 0x453b58 / 0x453c58 by radius).
* **Win**: JeepFlagCheck 0x42a480 (jeep carrying the enemy flag on its own pad terrain) → EndGame(team).
  **Loss**: fly-back 0x405360 with FUN_00427d70 = 0 (no jeep in stock or driven) → EndGame(−1).

### Random mines FUN_0041c9b0 (VHCL +5, or level·4 on 1-player levels 6-9)
Two passes over the cells (row-major): sites with no BNO, not strictly inside team 0's pad ±2 cells, terrain
bit 3 clear (so the roads 0x48..0x4f never qualify), terrain ≤ 0x53 and either a road 0x49..0x53 or terrain
0/3/≥ 0x34 — first pass only next to a road (0x49..0x59 N/S/W/E). Each mine: RandRange(sites left) → the r-th
unused site (r = 0 and 1 both the first), SpawnMine at (cell·32 + 6 + RandRange(25)) px on both axes (y drawn
first). The second pass places the remainder − 1 (the original's loop decrements before its exit test) and may
reuse cells. "Campgrounds Of America" (VHCL 200) finds 6 sites: 12 mines.

### End of game (src/endgame.c, play_rules.c, main.c)
EndGame 0x40f380: game clock stop (the high-score time), fade out 1 s; State_EndOfGameSequence 0x40f050 with a
winner: Mus_StartWinMusic (SFX stopped, 13 Win at 0xff), SetWinnerAndWinVideo 0x4370e0 = movie
`0x4559e0[level]` {0,1,1,1,1,2,2,2,3,3} → TITLE/WIN1, WIN2, WIN3, WIN.STM; banner 0x455a08 `Ban{B,G}{L,H}`
(winner, +2 in 640 modes) on black with its palette, colour key 0, at x centred / y = 180 (·2 hi-res,
0x455a18), fade in 500 ms, wait for the win music to end, music off, the movie with the banner over every frame
(pixel-doubled to 640×480), fade out 500 ms, back screen (Drums). No winner: music off, fade 1 s, back screen.
State_BackScreen then calls **RecordHighScore 0x42d8b0**: 1 player, winner 0, a player name (here `$USER`):
`~/Library/Application Support/Return Fire/RFire_HS` (OPENRF_HS overrides), each byte stored as
`(b ^ 0x5a) + "retufire"[offset & 7]`; header `{0x1c, "rfhs", n1P, 0x48, n2P, 0x50}`, 1P records
`{u16 level+1 (0x7f custom map), map[33] (file base name), player[33], u32 ms}` sorted by level; an existing
(level, map) record is replaced only by a faster time; 2P records follow unchanged.
Level flow (the Win32 menus pick maps in dialog 0x3f4): after a 1P win the next map RFMAPnnn+1; on the title
screen F2 starts the current map, keys 1-9 the first map of difficulty level 1-9; `--level <1..100 | path>`.


## 11. Two players (GameSetup2P 0x41a750, GameFrame2P 0x41aa70; `game.c`, `vehicle.c`, `src/play.c`)
The player count comes from the map header (+0x16, LoadLevelMap sets DAT_00443864): the 104 maps under
`WORLDS/2PLAYER/LEVEL1..9` (RFMAP101..204) are 2-player maps; each has one home pad and flag sites per side.
* **Setup**: team 1 (0x481020) gets index 1, view 2 (0x48bfdc), input words `DAT_0046f5dc+4` / `DAT_0046f5d4+4`
  and its own loadout copy (0x4438b8 + 0x14, the same defaults; FUN_004320c0 writes the VHCL stock into both
  halves), `loadout[0] |= 1`; HudInit(2); ViewEnterBunkerSelect for view 1 then view 2. The level's random mine
  count defaults to 0 in 2 players (FUN_004320c0 with param 2), VHCL may still set it. FUN_00427ea0 (Car_Load)
  zeroes vehicle def +0x294 (the enemy-arrow cel, def[0xa5]) only in 1 player: in 2 players the dashboard radar
  shows the arrow towards the other player's vehicle (team +0x80, VehicleUpdate).
* **Both sides are human**: turrets wake up for the vehicle of the view that draws the tower
  (Model_TowerHook: `get_team_vehicle(view team)` of the other side), SpawnDrone sends a drone of the other side
  at an idle vehicle of either team, the drone hum is positioned per listener (SFXF_OWN_GAINS), Msv_Mine
  0x42a310 lays mines only when `nplayers > 1`, the weapon-count widget shows the MSV mine stock in 2P.
* **Win**: JeepFlagCheck for either jeep (the enemy flag carried onto the own pad), or the enemy flag next to
  the own lift (StorageUpdate) → EndGame(team): banner BanB (side 0, blue) / BanG (side 1, green).
* **FUN_00427d70** (end of the fly-back, 0x405360) for a side without vehicle: 3 = it still has (or drives) a
  jeep → back to the bunker; 1 player: 0 → EndGame(−1). 2 players: 0 = neither side has a jeep → EndGame(−1)
  (draw); 2 = the other side has jeeps and this one other vehicles → bunker; **1 = nothing left at all while the
  other side still has jeeps → spectate** (`BV_SPECTATE`): +0xd8 = 0x3bffff, fade 1.0, +0xcc = −1.0, mode
  FUN_004054f0 (+0xcc += 0x51e·dt to 0: the lost type's icons fade out; then +0x3a = 0, +0x3b = 2, fade 0) →
  LAB_00405590 (fade += 0x51e·dt to 1.0) → 0x405710 (draws FUN_004055f0 for +0x3b more frames, then nothing: the
  picture stays). FUN_004055f0: fade cel, the other side's skull (FUN_00404fb0 with +0xd8 = 0) and the four
  vehicle types (cels 0x838 / 0x842 / 0x848 / 0x83d at (8,0) (36,0) (8,16) (36,16) from (cx − 35,
  cy + skull h/2 + 10)) each crossed out with cel 0x84d at +(4,7). The game goes on until the other player wins.
* **Fly-back skull** FUN_00404fb0 (also 1 player; `ui_flyback_draw`): cel `0x84d + frame + (side 0 ? 7 : 0)`,
  frame = table 0x43f288[+0xd8 >> 16] (the laugh of 0x405360), corners (±32 px) through diag(+0xd4) × yaw[+0xd0]
  round the view centre; +0xd4 grows 0x28f → 0x13333 by 0x666·dt, +0xd0 spins by 0x28000·dt until the zoom is
  full and the turn wraps. While +0xd8 > 0 the lost type's starting stock (min 8, rows of 4, table 0x43f320 +
  type·0x1c {cel, -, dy, x step, row step, cross dx, dy}) is drawn under it, the ones no longer in stock crossed
  out, fading in with +0xcc (0x51e·dt, FUN_00407df0 skips the cels below level 2). 0x405360 / 0x405440 draw the
  fade cel (Cel_SetupFullscreenFade, level 0xffff − fade) instead of the world.
* Not ported: the 2P status screen of B/C in the bunker (FUN_00404b70), pause (F3).
* **Scores**: RecordHighScore with 2 players (both names set; here `OPENRF_P1` / `OPENRF_P2`, default `$USER` /
  "Player 2"): record `{name1[33], name2[33] (strcmp order; the winner index flips with them), u32 wins1 +0x44,
  wins2 +0x48, draws +0x4c}`, the pair's counter is incremented, a new pair inserted in name order.
* Test (`build/twoplayer_test`, "Driving School"): both launch on the same frame (tank / jeep), the one-frame
  input lag, a side-0 tower waking for player 2's view, drones for either side, 4 MSV mines, player 2's jeep
  bringing side 0's flag home → EndGame(1), side 0 spectating while side 1 plays on, the draw, the score records.
