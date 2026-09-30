# World view rendering (RenderWorldView 0x419dd0) — exact maths, models, sort rules

Reference implementation: `tools/view.py` (integer, bit-level port) and `tools/models.py`
(extracts all 188 model parts to `out/models/models.json`). Sample frames: `out/view/*.png`
(`RFMAP001_bunker.png`/`_drive.png` = level 1 home pad, `RFMAP051_town_{bunker,drive,heli,lift,intro}.png`
= building-dense compound of "No Gas Up").

All values are 16.16 fixed point unless stated. `FixMul(a,b) = (a*b)>>16` (64-bit, floor; 0x438300,
**__fastcall** ECX,EDX). `FixDiv(a,b) = trunc((a/65536)/(b/65536)*65536)` (x87 + `_ftol`, 0x438390).
`CosFixed/SinFixed(a)` (0x438360/0x438520) = `trunc(cos(a/65536 * 2π/256) * 65536)`: full circle 0x1000000.
`ISqrt` 0x438550, `FixSqrt` 0x438580. `/` below is C division (truncate toward 0).

## 1. Tables (InitProjectionAndRotTables 0x408440, arg F = DAT_004438a8 = 0x12c0000 = 300.0)

| table | address | contents |
|---|---|---|
| projection `T[i]`, i = -1024..511 | 0x483780 (= PTR_DAT_00440c30; first entry at -0x1000) | `T[i] = F / (300 - i)` (int div; 0 at i=300) = 300/(300-i) in 16.16 |
| yaw `Y[k]`, k=0..63 | 0x481780 (9 ints, stride 0x24) | a=k*0x40000: `[c, s, 0, -s, c, 0, 0, 0, 1]` (rotation about z) |
| pitch `P[k]` | 0x48a7b0 | `[1, 0, 0, 0, c, s, 0, -s, c]` (rotation about x) |
| heading dir | 0x482480 (3 ints) | `(0,-1,0)*Y[k] = (sin, -cos, 0)`; heading 0 = north (-y) |
| reciprocals | 0x482080 | `FixDiv(1.0, n)` n=1..255 |
| triangular | 0x48b0f0 | `sum(0..n)` |

Vectors are **row vectors**: `v' = v*M` (`VecMulMat3` 0x42bb00, `Mat_TransformVerts` 0x4383c0,
each term `FixMul`ed separately); `Mat_Mul3x3(out,A,B)` 0x438460 = A·B (so `v*(A*B)` applies A first).
World axes: x east, y south (row 0 = north), z up; 1 cell = 32 px = 0x200000.

## 2. Camera / view struct (0x48bd90 + i*0x24c, dword index [n] or byte offset +0x..)

| field | meaning |
|---|---|
| [2],[3] | view w,h (1P lo-res 320x152, clip rect (0,0)-(319,151)) |
| [4],[5] | screen centre = (w/2)<<16, (h/2)<<16 → (160,76) |
| [6],[7] | camera world x,y (the point that projects to the centre at z=0) |
| [8] +0x20 | camera *height offset* `H` (smoothed value +0x114, target +0x134) |
| [9] +0x24 | pitch `p` (target +0x144), cache key +0x104 |
| [10] +0x28 | `-cos p`;  [11] +0x2c = `sin p` |
| [12],[13] | `left` = world X of the top row's left end, `top` = world Y of the top screen row (integer px, relative to camera) |
| +0x3c..+0x5c | camera matrix `C = [1,0,0, 0,s,c, 0,-c,s]` (c=cos p, s=sin p) |
| +0x10c/+0x110 | smoothed target, +0x12c/+0x130 target, +0x128 target z, +0x138/+0x13c velocity |
| +0x14c/+0x150/+0x154 | per-frame pointers to the winning tracker's {accel, maxaccel, gain} triples for pos / height / pitch |
| +0x158 | tracker list (nodes of 0x60 bytes at 0x4568c8 + view*0x60, update fn CamTracker_Update 0x402d90) |
| +0x1c4/+0x1c8 | screen-window offset (Camera_SetScreenWindow 0x403550; 1P: (0,12), window 288x140) |
| [0x2f],[0x30] | zoom-in ramp / view callback (ViewModeZoomIn → RenderWorldView) |

View space of a world point (X,Y,Z relative to the camera): `x' = X`, `y' = Y*s - Z*c`,
`z' = Y*c + Z*s` (z' grows toward the viewer). Projection uses `zr = z' - H`:
`scale = T[zr>>16] = 300/(300 - zr)`; the eye is at zr = 300.

**Float equivalent:** `sx = 160 + X*k`, `sy = 76 + (Y*sin p - Z*cos p)*k`, `k = 300/(300 + H - Y*cos p - Z*sin p)`.

`top`/`left` (ViewInit 0x403750, recomputed by Camera_Update when p or H change):
```
hh = -h>>1                                   // -76
top  = FixDiv(H + F, FixMul(F, s)/hh - [10]) // Y where screen y = -h/2 (16.16)
inv  = FixDiv(H + FixMul([10], top) + F, F)  // 1/scale at that row
[13] = top >> 16 ;  [12] = (inv * (-w>>1)) >> 16
```

### Camera modes (trackers created by the game; values = ViewAddTracker args)

| situation | creator | pitch +0x144 | H +0x134 | target z offset | centre scale |
|---|---|---|---|---|---|
| intro zoom start | ViewInit | 0x200000 (45°) | 270 | – | 0.53 |
| bunker vehicle select | View_EnterBunkerCam 0x404ee0 (pad pos) | 0x180000 (33.75°) | 0 | 0 | 1.00 |
| lift rising | Lift_SetTopDownCam 0x4181b0 | 0x400000 (90°, straight down) | 250 | 10 | 0.55 |
| driving tank/jeep/MSV | SpawnVehicle 0x427cd0 / 0x418200 | 0x180000 | g_VehCamHeight 0x44afb0[type] = -170 | 10 | 2.31 |
| driving heli | same | 0x180000 | -100 | 10 | 1.50 |
| vehicle destroyed | VehicleDestroy (call at 0x42840e) | 0x3e0000 | -120 (0xff880000) | 10 | – |

Default node (CamTracker_Default 0x402bf0, template 0x43eea8): H target 0, pitch 0x200000,
params {0.1, 0.05, 4.0}, {0.1, 0.05, 3.0}, {0.04, 0.01, 1.0}.

### Camera_Update 0x403a40 (follow)
1. Walk tracker list; first node whose update returns 1 wins. A node sets (if still 0) `+0x14c/150/154`
   to its param triples and writes targets: `+0x12c,+0x130 = obj.x,y`, `+0x128 = obj.z + zoff`,
   `+0x134 = H`, `+0x144 = pitch`.
2. Pitch: `Camera_StepAxis(+0x24 → +0x144)`.  StepAxis(cur,target,vel,{a0,a1,gain}): if the
   integer parts match stop; else desired speed `min(FixSqrt(FixMul(|d|, a1)), gain)` (signed),
   `vel = ApproachValue(vel, desired, a0*dt)`, `cur += vel*dt`, clamp at target.
3. Target y correction: `+0x130 += (+0x128>>16) * [10]` (pull north by (z+zoff)·cos p).
4. Position: `dx=(tx-sx)>>16, dy=(ty-sy)>>16, d=ISqrt(dx²+dy²)`; per axis
   `want = FixMul((dx<<16)/d, gain)`, capped to `±FixSqrt(|dx|*a1)` (cap uses accel a1, else a0);
   `vel = ApproachValue(vel, want, accel*dt)`, `pos += vel*dt`, no overshoot.
5. Height: `StepAxis(+0x114 → +0x134)`, `[8] = +0x114`.
6. `[6] = +0x10c - +0x1c4`, `[7] = +0x110 - +0x1c8`; recompute matrix/top/left if pitch or H changed.
Steady state (used by view.py): `cam = (tx, ty - (tz+zoff)·cos p - 12)`.

## 3. Floor walk (RenderWorldView 0x419dd0)
```
X0 = (camx>>16) + left;  Y0 = (camy>>16) - 32 + top
col = X0>>5;  xrel = left - (X0&31)          // tile-aligned left edge, px rel. camera
row = Y0>>5;  yrel = top - (Y0&31) - 32      // one tile margin above the screen
i  = [10]*yrel + H                           // = H - Y*cos p
xs = FixMul(xrel<<16, T[-i>>16])             // screen x of the row edge (rel. centre)
ys = FixDiv(300*[11], F + i) * yrel          // screen y (rel. centre)
tw = FixDiv(F, F + i) << 5                   // tile width on screen
on = 0
loop per row (far → near):
  ytop = ys
  if ys >= cy: if !on: break  else on = 0    // one extra row below the screen, no floor
  if on: while xs+tw < -0x20 - cx: col++, xrel += 32, xs += tw   // skip columns left of screen
  queue(cell[row][col-1])                    // one margin column (models only)
  yrel += 32; recompute xs', ys', tw' for the row's bottom edge
  xt = xs + cx; xb = cx + xs'
  while xt < w<<16:                          // cells left→right
     queue(cell)
     xt += tw_top;  if on: emit trapezoid cel
        c0=(prev xt & ~0x7fff, ytop+cy) c1=(xt & ~0x7fff, ytop+cy)
        c2=(xb+tw' & ~0x7fff, ys'+cy)   c3=(prev xb & ~0x7fff, ys'+cy)
  queue(one more cell on the right)
  row++;  if ys' < cy: on = 1               // the first row (above the screen) is models-only
after the loop: objects on the off-map pseudo cell (0x452d40) inside the visited box, then
View_DrawQueuedModels, flush.
```
Floor cel = copy of ART.CAR CCB `cell&0x7f` (off-map cells use CCB 2 = deep water), flags
`&~LAST | 0x1000` (explicit corners), mode **0xc** (`Cel_DrawTrapezoid`), or **0x13** (`Cel_DrawQuad`)
when terrain ∈ {1,2,4..0x33} (water/shore). x corners are rounded down to 0.5 px (only visible in
hi-res, where corners are `>>15`); y corners are exact. Screen pixel = `corner>>16 + clipX/Y`.

**Cel_DrawTrapezoid 0x40fe4f:** rows `c0.y .. c3.y` inclusive (H=c3.y-c0.y); left edge c0→c3, right c1→c2,
`dL=((c3x-c0x)<<16)/H`, `dR=((c2x-c1x)<<16)/H`, `dv=((h<<16)-1)/H`; per row `n=(R>>16)-(L>>16)` pixels
(right end exclusive), `du=(w<<16)/n`, texel `src[(v>>16)*w + (u>>16)]`, colour 0 skipped unless BGND.
Adjacent tiles share the bottom/top row and the right/left x, so there are no gaps.

**Cel_DrawQuad 0x410b9c** (models, mode 0x13): scanline edge list. Edges (Cel_ScanEdge 0x410120):
c0→c1 (u 0→(w-1)), c1→c2 (v 0→(h-1)), c3→c2, c0→c3; per-edge `dx = Δx<<16/Δy`, `du=(Δu+1)/Δy`, sorted by
(top y, x, dx); horizontal edges kept with dx=0x7fffffff. The first two edges are left/right; when a row
reaches the next edge's top y it replaces the edge whose bottom equals it (left) or else the right one.
Spans are inclusive `[L>>16, R>>16]`, `du=(uR-uL)/(n)`, affine (no perspective correction).

**Cel_DrawShadowSpans 0x411ba5** (mode 0xd): same trapezoid walk (`H = c3.y-c0.y+1`), each source row
holds `u8 x0,x1` pairs in 1/256 of the row width: `x = ((b*((R-L)>>4))>>4 + L)>>16`, `dst = darken[4][dst]`.

## 4. Model queue and depth sort (View_QueueModel 0x4085a0)
View_QueueCellContents 0x419cf0: for a cell with BNO t≠0, queue `BNO[t].model` at the cell centre
`((x+16)<<16, (y+16)<<16, 0)`; then every live object in the cell chain with `obj+0x3c` at `obj+0x40`.
Each **part** of the chain (`+4`) gets its own item (pool 0x483f80, 0x34 bytes, max 512):
```
pos = copy; if def+0x28: def+0x28(def,&pos)            // Model_PosJitter: pos.xy += jit[(x>>21&15)+(y>>21&15)*16].dx,dy
item.def = def
item.x = def.dx - camx + pos.x ; item.y = pos.y - camy + def.dy
if def+0x24 == 0:
   static cell:  zb = H - def.zbias ; item.team = (cell>>14)&3 ; yaw = pitch = 0
   object:       zb = H - obj+0x50 - def.zbias ; yaw = obj+0x4c ; team = obj+0x10
else zb = H - hook(item, cell, obj)                    // hook may set yaw/pitch/team/cell and DAT_0045f398
item.z = (def.flags & 0x10) ? 0 : pos.z + def.dz
(x',y',z') = item.xyz * C
key = (y'>>8) - z' + zb + (|x'|>>9)
item.z = z' - H
DAT_0045f398: 0 → sorted insert; >0 draw immediately; <0 drop.
```
Sorted insert: walk from the tail backwards while `new.key > node.key`; insert after the stopping node.
The list is in **descending key order** and View_DrawQueuedModels 0x408e50 draws from the head, so
**larger key = farther = drawn first**; ties keep queue order (row far→near, left→right, parts in chain
order). All floor cels are drawn before any model. Float: `key ≈ -depth + y'/256 + |x'|/512 - zbias`.

## 5. Model draw functions
Common tail `Model_EmitFaces(tv, proj, item, def, view)` 0x4088a0:
`ProjectVertices(proj, tv, &item.xyz, view, nverts)` 0x401290:
```
iz = (v.z + item.z) >> 16 ; if iz > 0x1ff: reject the whole part
s  = T[iz]
X  = ((((v.x + item.x) >> 14) * s) & 0xfffe0003) >> 2  + cx     // int32 product, 0.5 px rounding
Y  = ((((v.y + item.y) >> 14) * s) & 0xfffe0003) >> 2  + cy
```
Faces (0x20 bytes): `{sprite, flags, a, b, c0, c1, c2, c3}`. nfaces>0: draw face if
`(!(flags&1) || X[a] <= X[b]) && (!(flags&2) || Y[a] <= Y[b])` (back-face test on a vertex pair).
nfaces≤0: draw faces listed in `def+0x3c[(yaw & 0xfff9ffff)>>19]` (8 yaw octants; byte list ends <0),
no test. CCB = copy of ART.CAR `sprite + ((flags&8) ? item.team : 0)` (team colour variants), explicit
corners c0..c3 = projected verts (`Cel_SetQuadCorners` 0x426c10), draw mode = the sprite's PRE0
(0 normal, 1/2 shadow `darken[4]/[2]`, 3/4 blend, 5 brighten, 0xd shadow spans, 0x10 key-blend for CCB
452-456/524-578, 0x11 PLUT remap). Face flag bits 8-9 = debris class used by SpawnDebrisBurst when the building is destroyed; 0x10/0x80 not read by the renderer.

| fn | matrix applied to the vertices |
|---|---|
| Model_DrawStatic 0x408840 | `C` (cached in def+0x40 while `def+0x3c == pitch`) |
| Model_DrawYaw 0x408a20 | `Y[yaw>>16]*C` if yaw&0xffff0000 else C |
| Model_DrawYawPitch 0x408aa0 | pitch=|item.pitch|; pitch? `P[pitch>>16]*Y[yaw>>16]*C` : yaw? `Y*C` : C |
| Model_DrawTurret 0x408b80 | `Rx(item+0x20·4) · Rx'(item+0x30·4)` [· Y[(item+0x24)>>16]] · Y[yaw] · C (CosFixed built on the fly) |
| Model_DrawTilted 0x408d40 | `(P[pitch]*Y[yaw] or Y[yaw]) * C * P[DAT_00440c4c]` |

Wrappers (patch the shared def, then call one of the above):
- **Model_DrawBuilding 0x41f550** (BLDG/FACT): `r = jit(cell).b3` (CellJitterEntry 0x417820 =
  entry `(idx&15) + (idx&0x1e0)/2`); table T = g_BldgWallSprites 0x4478d8 `[909,931,915,927,919,923]`
  or g_FactWallSprites 0x4478f0 `[938,960,944,956,948,952]` (chosen by def+8 == 0x447890); door side
  `d=(def.flags>>16)&0xff` (0 N,1 W,2 E,3 S); roof face4 = T[0]+s8[0x4478d0+(r&7)] (0,0,2,2,2,4,4,4);
  wall(face k, neighbour n) = T[1] if n is BNO 51 (rubble) else (d==k ? ((r&0x18)==0x18 ? T[3]:T[2])
  : (((r&0x60)>>5)==k ? T[5] : T[4])); face1 (W) uses +2; faces 1/2/3 use neighbours W/E/S.
- **Model_TowerHook 0x4174e0** (TOWER/LARGE_TOWER turret part): no object → hidden if cell HP bits
  (0xe000000) are 0; if the viewing player's vehicle is an enemy Vehicle, `ActivateTurret` 0x423c10 may
  replace it by a live Turret object (then hidden); else `team=cell side, yaw=(r&0x3f)<<16`,
  `pitch=(jit.b0 as u8)<<14` (random rest pose), returns zbias.
- Model_CellHook 0x41f380: team from cell, remembers the cell (for the building wrapper).
- Gates (Model_DrawGateH_W/E, GateV_N/S): leaf width = 1.0 - (gate obj+0x64 hi16), sprite 857/858 → static
  map = closed (no Gate object until opened; SpawnGate then clears the BNO).
- **Combat objects** (render/view.c): projectiles hook 0x41f800 (yaw, pitch obj+0x60, frame obj+0x71 as team),
  grenade hook 0x41f830 (team = tumble frame + 12 for team 1); explosions: graphic 0x44aa20 has no model —
  hook 0x420680 + draw 0x4206b0 draw the descriptor's own quads (`RenderExpl`, see docs/game.md §6), the parts
  0x452dc8/0x452e0c (ground flash, DrawStatic) continue with it; debris 0x433a60 (`RenderDebris`, one cel on
  4 corners, fade by FUN_00407df0 = hidden below 1/16 else drawn twice); Stay 0x435e40/0x435e10 draws only
  the first part of the wrapped graphic with pitch obj+0x5c. The draw-time removals (idle debris / expired
  Stay) are reported through `RenderObj.draw_flags` and applied by `effects_reap_drawn`.
- Dest wrappers (DestBush/Tree/WTower/Fuel) pick a rubble face/variant from the jitter byte (not needed
  for fresh maps; view.py draws them with their stored faces).

## 6. Map / per-cell data used by the renderer
Cells built as LoadLevelMap 0x4322f0 (+SetCellStaticObject 0x417850): terrain from tile table 0x452858,
BNO terrain override, HP = clamp(strength,1,15) in bits 25-27 (3 bits!), side bits 14-15.
Jitter table g_CellJitter256 0x472290 (InitCellJitterTable 0x41f190): `srand(sum of raw tile bytes)`,
256 × `{RandRange(25)-12, RandRange(25)-12, RandRange(11), RandRange(256)}` with MSVC rand
(`s=s*214013+2531011; (s>>16)&0x7fff`), `RandRange(n) = ((rand()&0x7fff)*2*n)>>16`; the previous
seed is restored afterwards.

## 7. Model format (models.json)
188 model parts in .data (187 with geometry, 1 special: BNO 76 `0x449fd0`, hook 0x41f950 destroyed
bridge), 598 faces, 1782 vertices. Part struct: see `tools/models.py` docstring. Vertex = 3×s32 16.16
(x east, y south, z up) relative to the part origin (+ offset +0x14..+0x1c). JSON per part: `addr, draw_fn,
next, flags, offset, zbias, depth_hook, pos_hook, verts, faces[{sprite, flags, test[a,b], corners[4],
vis, team_offset, debris_class, sprite_mode, sprite_size}], orders_by_yaw8, used_by`.
Unreferenced (dead or reached via pointers not found): 0x446570 0x446738 0x449ae8 0x44a610 0x44c6b0
0x44ce90 0x44d7d0 0x44e240 0x44e3c0 0x452e0c.

### BNO → model parts
| BNO | name | parts (addr: draw fn, faces, z-bias, hook) |
|---|---|---|
| 0 | BNO_NOTHING | none (not placeable) |
| 1 | BNO_BUSH_1S | 0x4440a8 DrawStatic 3f zb1.5 +jitter |
| 2 | BNO_BUSH_2S | 0x4440f0 DrawStatic 3f zb1.5 +jitter |
| 3 | BNO_PALM_1S | 0x444490 DrawStatic 2f zb21 +jitter; 0x4443c8 DrawStatic 2f zb9 +jitter |
| 4 | BNO_PALM_2S | 0x4448e8 DrawStatic 2f zb18 +jitter; 0x444780 DrawStatic 2f zb7.5 +jitter |
| 5 | BNO_TREE_S | 0x444b38 DrawStatic 2f zb18; 0x4449d0 DrawStatic 2f zb7.5 |
| 6 | BNO_CACTUS | 0x444d18 DrawStatic 3f zb15 +jitter; 0x444cd0 DrawStatic 1f zb7.5 +jitter |
| 7 | BNO_ROCK_1S | 0x444ed0 DrawStatic 1f zb-10 +jitter |
| 8 | BNO_ROCK_2S | 0x444f18 DrawStatic 1f zb-10 +jitter; 0x444ed0 DrawStatic 1f zb-10 +jitter |
| 9 | BNO_ROCK_1T | 0x444f60 DrawStatic 1f zb-10 +jitter |
| 10 | BNO_ROCK_2T | 0x444fa8 DrawStatic 1f zb-10 +jitter; 0x444f60 DrawStatic 1f zb-10 +jitter |
| 11 | BNO_PLANTER_BUSH | 0x4470d0 DrawStatic 1f zb-40 |
| 12 | BNO_PLANTER_BERRY | 0x447118 DrawStatic 1f zb-40 |
| 13 | BNO_PLANTER_STONE | 0x447160 DrawStatic 1f zb-40 |
| 14 | BNO_AMMO | 0x44a360 DrawAmmo 5f zb12; 0x44a2d8 DrawStatic 1f zb5 |
| 15 | BNO_PRISON_S | 0x446258 DrawStatic 4f zb12; 0x4461d0 DrawStatic 1f zb5; 0x446070 DrawStatic 5f zb12 |
| 16 | BNO_PRISON_E | 0x446258 DrawStatic 4f zb12; 0x4461d0 DrawStatic 1f zb5; 0x446070 DrawStatic 5f zb12 |
| 17 | BNO_PRISON_W | 0x446258 DrawStatic 4f zb12; 0x4461d0 DrawStatic 1f zb5; 0x446070 DrawStatic 5f zb12 |
| 18 | BNO_WATCHTOWER_NE | none (not placeable) |
| 19 | BNO_WATCHTOWER_NW | none (not placeable) |
| 20 | BNO_WATCHTOWER_SE | none (not placeable) |
| 21 | BNO_WATCHTOWER_SW | none (not placeable) |
| 22 | BNO_FLAG | 0x446998 DrawStatic 8f zb12; 0x4467a0 DrawStatic 1f zb5 |
| 23 | BNO_BLDG_N | 0x447908 DrawBuilding 5f zb12 CellHook; 0x447230 DrawStatic 1f zb5; 0x446e60 DrawStatic 1f zb-5 |
| 24 | BNO_BLDG_S | 0x44794c DrawBuilding 5f zb12 CellHook; 0x447230 DrawStatic 1f zb5; 0x446e60 DrawStatic 1f zb-5 |
| 25 | BNO_BLDG_E | 0x447990 DrawBuilding 5f zb12 CellHook; 0x447230 DrawStatic 1f zb5; 0x446e60 DrawStatic 1f zb-5 |
| 26 | BNO_BLDG_W | 0x4479d4 DrawBuilding 5f zb12 CellHook; 0x447230 DrawStatic 1f zb5; 0x446e60 DrawStatic 1f zb-5 |
| 27 | BNO_FACT_N | 0x447a18 DrawBuilding 5f zb12 CellHook; 0x447278 DrawStatic 1f zb5; 0x446ea8 DrawStatic 1f zb-5 |
| 28 | BNO_FACT_S | 0x447a5c DrawBuilding 5f zb12 CellHook; 0x447278 DrawStatic 1f zb5; 0x446ea8 DrawStatic 1f zb-5 |
| 29 | BNO_FACT_E | 0x447aa0 DrawBuilding 5f zb12 CellHook; 0x447278 DrawStatic 1f zb5; 0x446ea8 DrawStatic 1f zb-5 |
| 30 | BNO_FACT_W | 0x447ae4 DrawBuilding 5f zb12 CellHook; 0x447278 DrawStatic 1f zb5; 0x446ea8 DrawStatic 1f zb-5 |
| 31 | BNO_BNKR_N | 0x448268 DrawStatic 11f zb12; 0x447dd0 DrawStatic 1f zb5 |
| 32 | BNO_BNKR_S | 0x4482b0 DrawStatic 11f zb12; 0x447dd0 DrawStatic 1f zb5 |
| 33 | BNO_BNKR_E | 0x448708 DrawStatic 11f zb12; 0x447dd0 DrawStatic 1f zb5 |
| 34 | BNO_BNKR_W | 0x448750 DrawStatic 11f zb12; 0x447dd0 DrawStatic 1f zb5 |
| 35 | BNO_OUTPOST | 0x447d00 DrawStatic 5f zb12 CellHook; 0x447cb8 DrawStatic 1f zb5 |
| 36 | BNO_HOSP_S | 0x448df8 DrawStatic 5f zb12; 0x4488e0 DrawStatic 1f zb2 |
| 37 | BNO_HOSP_E | 0x448e40 DrawStatic 5f zb12; 0x448924 DrawStatic 1f zb2; 0x44881c DrawStatic 1f zb-50 |
| 38 | BNO_HOSP_W | 0x448e88 DrawStatic 5f zb12; 0x448968 DrawStatic 1f zb2; 0x4487d8 DrawStatic 1f zb-50 |
| 39 | BNO_FUEL_N | 0x445ddc DrawStatic 10f zb12; 0x445cc8 DrawStatic 1f zb-40; 0x445bfc DrawStatic 1f zb5 |
| 40 | BNO_FUEL_S | 0x445d98 DrawStatic 10f zb12; 0x445bb8 DrawStatic 1f zb5 |
| 41 | BNO_FUEL_E | 0x445e20 DrawStatic 10f zb12; 0x445d0c DrawStatic 1f zb-40; 0x445c40 DrawStatic 1f zb5 |
| 42 | BNO_FUEL_W | 0x445e64 DrawStatic 10f zb12; 0x445d50 DrawStatic 1f zb-40; 0x445c84 DrawStatic 1f zb5 |
| 43 | BNO_GATE_H | 0x4428a8 DrawGateH_W 7f zb14.1; 0x4428ec DrawGateH_E 7f zb14.1 |
| 44 | BNO_GATE_V | 0x442d70 DrawGateV_N 8f zb14.1; 0x442db4 DrawGateV_S 8f zb14.1 |
| 45 | BNO_WALL_V | 0x442278 DrawStatic 3f zb15 |
| 46 | BNO_WALL_H | 0x441d68 DrawStatic 3f zb15 |
| 47 | BNO_TENT_V | 0x4491c0 DrawStatic 4f zb10 |
| 48 | BNO_TENT_H | 0x449208 DrawStatic 4f zb10 |
| 49 | BNO_TOWER | 0x441840 DrawYawPitch 5f zb24.5 TowerHook; 0x4414f0 DrawStatic 5f zb14; 0x441338 DrawStatic 1f zb5 |
| 50 | BNO_LARGE_TOWER | 0x4418a0 DrawYawPitch 5f zb29.5 TowerHook; 0x441698 DrawStatic 5f zb14; 0x4415b0 DrawStatic 1f zb5 |
| 51 | BNO_STD_DEST | 0x449318 DrawStatic 1f zb-40 HookStdDest |
| 52 | BNO_DEST_JAIL | none (not placeable) |
| 53 | BNO_DEST_HOSP | 0x448ef0 DrawStatic 1f zb-40 |
| 54 | BNO_LDMG_WALL_H | 0x441df8 DrawStatic 4f zb15 |
| 55 | BNO_RDMG_WALL_H | 0x441db0 DrawStatic 4f zb15 |
| 56 | BNO_BDMG_WALL_H | 0x441fa0 DrawStatic 5f zb15 |
| 57 | BNO_LDMG_WALL_V | 0x442308 DrawStatic 4f zb15 |
| 58 | BNO_RDMG_WALL_V | 0x4422c0 DrawStatic 4f zb15 |
| 59 | BNO_BDMG_WALL_V | 0x4424b0 DrawStatic 5f zb15 |
| 60 | BNO_DMG_TOWER | 0x441a78 DrawStatic 1f zb-40; 0x4419a0 DrawStatic 5f zb14 |
| 61 | BNO_DESTROYED_TOWER | 0x441ac0 DrawStatic 1f zb-40 |
| 62 | BNO_DMG_FLAG | 0x446b08 DrawStatic 4f zb12; 0x446a00 DrawStatic 1f zb5 |
| 63 | BNO_DEST_FLAG | 0x446d28 DrawStatic 4f zb12; 0x446c60 DrawStatic 1f zb5 |
| 64 | BNO_DEST_TENT | 0x449270 DrawStatic 1f zb-50 HookDestTent |
| 65 | BNO_DEST_AMMO | 0x44a3e8 DrawStatic 2f zb-50 |
| 66 | BNO_DEST_GATE_H | 0x442e98 DrawStatic 1f zb-50 |
| 67 | BNO_DEST_GATE_V | 0x442e98 DrawStatic 1f zb-50 |
| 68 | BNO_READY_TOWER | 0x4414f0 DrawStatic 5f zb14; 0x441338 DrawStatic 1f zb5 |
| 69 | BNO_WTOWER_NE | 0x445450 DrawStatic 5f zb12; 0x4450f8 DrawStatic 1f zb5 |
| 70 | BNO_WTOWER_NW | 0x445494 DrawStatic 5f zb12; 0x44513c DrawStatic 1f zb5 |
| 71 | BNO_WTOWER_SE | 0x4454d8 DrawStatic 5f zb12; 0x445180 DrawStatic 1f zb5 |
| 72 | BNO_WTOWER_SW | 0x44551c DrawStatic 5f zb12; 0x4451c4 DrawStatic 1f zb5 |
| 73 | BNO_DEST_PRISON | 0x4462c0 DrawStatic 1f zb-40 |
| 74 | BNO_BRIGE_H | 0x449d90 DrawStatic 1f zb-40 |
| 75 | BNO_BRIGE_V | 0x449dd8 DrawStatic 1f zb-40 |
| 76 | BNO_BRIGE_V | 0x449fd0 special (hook Model_HookBridgeDest) |
| 77 | BNO_DEST_FUEL | 0x445fa8 DrawDestFuel 2f zb-50 CellHook |
| 78 | BNO_DEST_WTOWER_NE | 0x4455e0 DrawDestWTower 1f zb-50 |
| 79 | BNO_DEST_WTOWER_NW | 0x445624 DrawDestWTower 1f zb-50 |
| 80 | BNO_DEST_WTOWER_SE | 0x445668 DrawDestWTower 1f zb-50 |
| 81 | BNO_DEST_WTOWER_SW | 0x4456ac DrawDestWTower 1f zb-50 |
| 82 | BNO_DEST_OUTPOST | 0x447d68 DrawStatic 1f zb-50 |
| 83 | BNO_DEST_PLANTER | 0x4471a8 DrawStatic 1f zb-40 HookDestPlanter |
| 84 | BNO_DEST_TREE_S | 0x444588 DrawDestTree 1f zb-40 CellHook +jitter |
| 85 | BNO_DEST_TREE_D | 0x444698 DrawDestTree 1f zb-40 CellHook +jitter |
| 86 | BNO_DEST_TREE | 0x4445d0 DrawDestTree 1f zb-40 CellHook |
| 87 | BNO_DEST_BUSH_1 | 0x444178 DrawDestBush 1f zb-40 CellHook +jitter |
| 88 | BNO_DEST_BUSH_2 | 0x444200 DrawDestBush 1f zb-40 CellHook +jitter |
| 89 | BNO_STORAGE_LIGHT | 0x44b000 DrawStatic 1f zb-50 |
| 90 | BNO_ACTIVE_TOWER | 0x441538 DrawStatic 5f zb14 HookHide |

| user | model |
|---|---|
| class MAN (0x43fb10+0x14) | 0x43f900 Model_DrawMan (8 verts, 2 faces) |
| code: MAN walk (ManUpdate) | 0x43f900 Model_DrawMan (8 verts, 2 faces) |
| code: MAN swim (ManUpdate) | 0x43f998 Model_DrawManSwim (4 verts, 1 faces) |
| code: MAN misc (0x4064d0/0x406550) | 0x43fa30 Model_DrawStatic (4 verts, 1 faces) |
| class SUB (0x43fc10+0x14) | 0x43fbb0 Model_DrawSub (4 verts, 1 faces) |
| class Turret Gun (0x44ad40+0x14) | 0x441840 Model_DrawYawPitch (12 verts, 5 faces) |
| class Turret Gun (large) (0x44ad90+0x14) | 0x4418a0 Model_DrawYawPitch (12 verts, 5 faces) |
| code: large turret (TurretInit) | 0x4418a0 Model_DrawYawPitch (12 verts, 5 faces) |
| code: gate H (SpawnGate) | 0x4428a8 Model_DrawGateH_W (15 verts, 7 faces) |
| code: gate V (SpawnGate) | 0x442d70 Model_DrawGateV_N (16 verts, 8 faces) |
| class Storage (rising) (0x443288+0x14) | 0x4431f8 Model_DrawStoragePad (12 verts, 5 faces) |
| class Storage (lowering) (0x4432d8+0x14) | 0x4431f8 Model_DrawStoragePad (12 verts, 5 faces) |
| class Mine (0x455008+0x14) | 0x4493b0 Model_DrawStatic (4 verts, 1 faces) |
| code: mine (MineUpdate) | 0x449478 Model_DrawStatic (4 verts, 1 faces) |
| class Missle (0x450950+0x14) | 0x4495c0 Model_DrawYawPitch (8 verts, 3 faces) |
| class Death Missle (0x4509f8+0x14) | 0x4495c0 Model_DrawYawPitch (8 verts, 3 faces) |
| class TRACER (0x4509a0+0x14) | 0x4495c0 Model_DrawYawPitch (8 verts, 3 faces) |
| projectile type 1 model (+0x2c) | 0x4495c0 Model_DrawYawPitch (8 verts, 3 faces) |
| projectile type 3 model (+0x2c) | 0x4495c0 Model_DrawYawPitch (8 verts, 3 faces) |
| projectile type 4 model (+0x2c) | 0x4495c0 Model_DrawYawPitch (8 verts, 3 faces) |
| projectile type 5 model (+0x2c) | 0x4495c0 Model_DrawYawPitch (8 verts, 3 faces) |
| projectile type 6 model (+0x2c) | 0x4495c0 Model_DrawYawPitch (8 verts, 3 faces) |
| projectile type 1 shadow (+0x30) | 0x449628 Model_DrawYaw (4 verts, 1 faces) |
| projectile type 3 shadow (+0x30) | 0x449628 Model_DrawYaw (4 verts, 1 faces) |
| projectile type 4 shadow (+0x30) | 0x449628 Model_DrawYaw (4 verts, 1 faces) |
| projectile type 5 shadow (+0x30) | 0x449628 Model_DrawYaw (4 verts, 1 faces) |
| projectile type 6 shadow (+0x30) | 0x449628 Model_DrawYaw (4 verts, 1 faces) |
| projectile type 2 model (+0x2c) | 0x449730 Model_DrawYawPitch (8 verts, 3 faces) |
| projectile type 8 model (+0x2c) | 0x449730 Model_DrawYawPitch (8 verts, 3 faces) |
| projectile type 9 model (+0x2c) | 0x449730 Model_DrawYawPitch (8 verts, 3 faces) |
| projectile type 2 shadow (+0x30) | 0x449798 Model_DrawYaw (4 verts, 1 faces) |
| projectile type 8 shadow (+0x30) | 0x449798 Model_DrawYaw (4 verts, 1 faces) |
| projectile type 9 shadow (+0x30) | 0x449798 Model_DrawYaw (4 verts, 1 faces) |
| projectile type 10 model (+0x2c) | 0x4498a0 Model_DrawYawPitch (8 verts, 3 faces) |
| projectile type 10 shadow (+0x30) | 0x449908 Model_DrawYaw (4 verts, 1 faces) |
| projectile type 0 model (+0x2c) | 0x4499a0 Model_DrawTilted (4 verts, 1 faces) |
| projectile type 7 model (+0x2c) | 0x4499a0 Model_DrawTilted (4 verts, 1 faces) |
| projectile type 11 model (+0x2c) | 0x4499a0 Model_DrawTilted (4 verts, 1 faces) |
| projectile type 0 shadow (+0x30) | 0x449a08 Model_DrawYaw (4 verts, 1 faces) |
| projectile type 7 shadow (+0x30) | 0x449a08 Model_DrawYaw (4 verts, 1 faces) |
| projectile type 11 shadow (+0x30) | 0x449a08 Model_DrawYaw (4 verts, 1 faces) |
| class Grenade (0x450d48+0x14) | 0x449aa0 Model_DrawTilted (4 verts, 1 faces) |
| code: bridge debris (0x41f870) | 0x449e20 Model_DrawStatic (4 verts, 1 faces) |
| code: bridge debris (0x41f870) | 0x449e68 Model_DrawStatic (4 verts, 1 faces) |
| code: bridge debris (0x41f870) | 0x449eb0 Model_DrawStatic (4 verts, 1 faces) |
| code: bridge debris (0x41f870) | 0x449ef8 Model_DrawStatic (4 verts, 1 faces) |
| code: bridge debris (0x41f870) | 0x449f40 Model_DrawStatic (4 verts, 1 faces) |
| code: bridge debris (0x41f870) | 0x449f88 Model_DrawStatic (4 verts, 1 faces) |
| code: tank turret (Model_DrawTank) | 0x44ccb0 Model_DrawYaw (22 verts, 8 faces) |
| class Vehicle (0x44b128+0x14) | 0x44cd10 Model_DrawTank (24 verts, 6 faces) |
| class Destroyed Vehicle (0x44b0d8+0x14) | 0x44cd10 Model_DrawTank (24 verts, 6 faces) |
| vehicle Tank def[0x52] body | 0x44cd10 Model_DrawTank (24 verts, 6 faces) |
| vehicle Tank def[0x53] shadow | 0x44cdc0 Model_DrawYaw (4 verts, 1 faces) |
| vehicle Heli def[0x53] shadow | 0x44cdc0 Model_DrawYaw (4 verts, 1 faces) |
| vehicle Tank def[0x55] part 0x55 | 0x44ce48 Model_DrawYaw (8 verts, 2 faces) |
| vehicle Heli def[0x55] part 0x55 | 0x44ce48 Model_DrawYaw (8 verts, 2 faces) |
| vehicle Tank def[0x58] wreck A | 0x44cfe0 Model_DrawYaw (12 verts, 3 faces) |
| vehicle Tank def[0x59] wreck B | 0x44cfe0 Model_DrawYaw (12 verts, 3 faces) |
| vehicle MSV def[0x52] body | 0x44d650 Model_DrawMSV (52 verts, 14 faces) |
| vehicle MSV def[0x53] shadow | 0x44d700 Model_DrawYaw (4 verts, 1 faces) |
| vehicle MSV def[0x55] part 0x55 | 0x44d788 Model_DrawYaw (8 verts, 2 faces) |
| vehicle MSV def[0x58] wreck A | 0x44d920 Model_DrawYaw (12 verts, 3 faces) |
| vehicle MSV def[0x59] wreck B | 0x44d920 Model_DrawYaw (12 verts, 3 faces) |
| code: jeep (Model_DrawJeep) | 0x44dfb0 Model_DrawYaw (48 verts, 12 faces) |
| vehicle Jeep def[0x52] body | 0x44e070 Model_DrawJeep (44 verts, 11 faces) |
| code: jeep part (0x42f5c0) | 0x44e110 Model_DrawYaw (6 verts, 2 faces) |
| vehicle Jeep def[0x53] shadow | 0x44e1a8 Model_DrawJeepShadow (4 verts, 1 faces) |
| vehicle Jeep def[0x55] part 0x55 | 0x44e328 Model_DrawYaw (8 verts, 2 faces) |
| vehicle Jeep def[0x58] wreck A | 0x44e510 Model_DrawYaw (12 verts, 3 faces) |
| vehicle Jeep def[0x59] wreck B | 0x44e510 Model_DrawYaw (12 verts, 3 faces) |
| code: carried flag (FlagUpdate) | 0x44e610 Model_DrawFlagCarried (8 verts, 2 faces) |
| class Flag (0x44ae30+0x14) | 0x44e740 Model_DrawFlag (8 verts, 2 faces) |
| code: flag (FlagUpdate) | 0x44e740 Model_DrawFlag (8 verts, 2 faces) |
| code: heli shadow (0x417e40/Model_DrawHeliShadow) | 0x44e9b8 Model_DrawYaw (4 verts, 1 faces) |
| vehicle Heli def[0x52] body | 0x44ee68 Model_DrawHeli (44 verts, 12 faces) |
| code: heli (lift/dock) | 0x44ee68 Model_DrawHeli (44 verts, 12 faces) |
| vehicle Heli def[0x58] wreck A | 0x44eec8 Model_DrawYawPitch (44 verts, 12 faces) |
| code: rotor A (0x42fb10) | 0x44f058 Model_DrawYaw (4 verts, 1 faces) |
| code: rotor B (0x42fba0) | 0x44f188 Model_DrawYaw (4 verts, 1 faces) |
| code: rotor (lift, 0x42b5e0) | 0x44f1d0 Model_DrawRotorB (4 verts, 1 faces) |
| vehicle Heli def[0x59] wreck B | 0x44f298 Model_DrawYaw (4 verts, 1 faces) |
| code: explosion part (0x41fd90) | 0x452dc8 Model_DrawStatic (4 verts, 1 faces) |
| class Drone (0x455778+0x14) | 0x455680 Model_DrawDrone (50 verts, 15 faces) |
| code: drone shadow (SpawnDrone) | 0x455730 Model_DrawDroneShadow (4 verts, 1 faces) |

## 8. Open questions / caveats
- The driving camera (H=-170 → 2.3× at the centre, ~4 tiles visible) comes straight from
  g_VehCamHeight + ViewAddTracker; it has not been compared with a real screenshot.
- `FixDiv` is x87 80-bit in the original; view.py uses doubles (possible 1-ulp differences).
- Cel_DrawQuad port follows the edge-pairing logic but the exact rounding of Span_* for every
  mode (and the `n==0` case using EDX garbage) is not proven pixel-exact; Cel_DrawTrapezoid and
  Cel_DrawShadowSpans are straight ports.
- Vehicle wrappers are ported in `src/render/view.c` (draw_tank/msv/jeep/heli/rotor_a/b/storage*, depth hooks
  in `obj_hook`): tank turret 0x44ccb0 = hull yaw + state+0x58, barrel verts 15..21 = 0x44c938 rotated about x by
  state+0x50·4 + 0x44c704; MSV launcher verts 44..51 (0x44d028/0x44d094), face 13 sprite 0x146 − frame; jeep tyre
  sprites 0x1c9 + (x>>16 & 3), boat morph table 0x44de70 (verts 36..43) and 0x44dfb0 / wake 0x44e110 (scale cache
  DAT_0044dc80); heli: Model_DrawTurret with pitch = obj+0x70, roll = state+0x88, tail fold 0x120000·(1−spin-up)
  on verts 0x44ee40, rotor disc 0x44ea00 by speed (0x44e850/898/8e0, 4 = two resting blades 0x44e9b8), ground
  shadow = class-4 child (0x44f1d0 + rotor shadow 0x44f0a0) at (x + 85·(z>>8), y − z/2) (render_heli_shadow);
  lift: pad saves the last cel, platform draws obj+0x60's model at once, doors slide u68·0x4ccc, the saved cel is
  re-added. Hook 0x417d20 draws the lift immediately while the cell terrain has bit 1 clear (92). Test sheets:
  `out/vehicles/*.png` (render_test). Model_DrawTilted uses P[DAT_00440c4c = 0x38] after C.
- Water/palette cycling (Pal_CycleWater) is not applied (static palette); HUD cels are not drawn,
  only the 1PBSCRL.RFA status-bar bitmap under the view.
- Model_DrawTurret: item+0x20 pitch (Rx), +0x30 roll (Ry), +0x24 extra yaw (mode 1); only the heli uses it.
- Face flag bits 0x10/0x80 and def+0x0c (size) meaning unconfirmed.
