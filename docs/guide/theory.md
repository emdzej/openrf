# How Return Fire works

A theory of operation of Return Fire (Silent Software, 3DO 1995, Windows 95 edition 1996): how the game is
built, from the frame loop to the capture of the flag. It comes from porting the Windows executable function by
function. It is meant for anyone who wants to learn how an action game of its time is put together. The details,
with the executable's addresses and layouts, are on the internals pages of this site:
[engine architecture](/architecture), [game simulation](/game), [renderer](/render), and the formats of
[ART.CAR](/car), [the level maps](/rfm) and [the movies](/stm).

## The shape of the program

Return Fire for Windows is one executable of about 420 KB, written in C, plus a small language DLL with
the menus, dialogs and strings. About two thirds of its code is the game and its engine, and the rest is
the C runtime and the Windows glue (DirectDraw, DirectSound, Video for Windows, the registry, the dialogs).

The program is a port of the 3DO original, and you can see it in the design. The 3DO had a hardware
sprite engine, so the 3DO game drew everything as *cels*: textured quadrilaterals described by a small
control block. The Windows version keeps that model and adds a software rasteriser that plays the part of
the 3DO hardware, drawing cels into an 8-bit DirectDraw surface. The game code still builds cel lists
and hands them to a "draw cels" call, just as it did on the 3DO.

| Part | What it does |
|---|---|
| App shell | Windows set-up, the main loop, the app states (intro, title, game, end sequence), menus and dialogs |
| Timers and callbacks | A queue of delayed calls, sorted by time, and a list of per-frame tasks |
| Object system | A fixed pool of game objects, each with a class that supplies its behaviour |
| Map | The 128 × 128 grid of cells: terrain, static objects, the objects standing in each cell |
| Collision | Shapes made of parts, tested against the cell's static object and the other objects in the 3 × 3 cells around |
| Vehicles | The four vehicle types, the bunker lift, fuel, ammunition, docking, wrecks |
| Weapons and effects | Projectiles, grenades, mines, scripted explosions, debris, remains |
| Enemies and rules | Gun towers, drones, the submarine, soldiers, gates, the flag, the end of the game |
| Camera and world view | Camera follow, the perspective floor, 3D models made of cels, the depth sort |
| Cel engine | The software 3DO: cel lists, draw modes, blend and shade tables |
| HUD and radar | The status bar, gauges and the 128 × 128 radar image |
| Sound and music | A priority sound mixer with two listeners, and a music director over one long orchestral score |
| Movies | Streaming of the intro and victory films |

Start-up sets up the window, DirectDraw, sound and input, loads the art, and starts the intro. Then the
main loop pumps Windows messages and calls the *current app state* once per pass, and presents the frame.
The states are the intro, the title (back) screen, the one- and two-player game, a fade into a new game,
and the end-of-game sequence. A state never blocks; it does one step and returns, and the next state is
set by assigning a function pointer. This is the same shape the port uses (see [Portable
core](/internals/portable-core)), which made it possible to run the game in a host that calls it once per
frame, such as gasm and the browser.

The front end is plain Windows: the menu bar and the dialogs for player names, level select and high
scores. There is no in-game menu system. The port replaces the dialogs with keys on the title screen.

## Time

Time is counted in ticks of 16 ms (62.5 a second), taken from the system clock. At the end of each game
frame the game works out how many whole ticks have passed since the last one, caps that at 12, and that
count is the next frame's *delta*. Every speed, turn rate, timer and animation in the game is multiplied by
the delta. So the game has a variable timestep with an integer step: a fast machine runs frames with a
delta of 1 (or 0, in which case nothing moves), a slow one runs fewer frames with larger deltas. There is no
frame limiter beyond the monitor's vertical sync in full-screen mode.

A game frame does, in order:

```
read the input words (current and previous, per player)
music service; reset the bunker-door flags
advance the timer queue by the delta
update every active object
update the radar movers; cycle the water colours
run each player's view (bunker select, zoom-in, or the world view)
draw the overlay tasks and the HUD
music director; sound service
measure the time: the next delta
```

Note that the view comes *after* the objects but is part of the simulation's step, which matters below:
drawing changes the game state.

Randomness comes from the C runtime's `rand()` (the Microsoft one), through a helper that scales it to a
range. Nothing in the simulation reads the wall clock, and the map's decorative jitter is seeded from the
map itself. Given the same input words and the same sequence of deltas, a game plays the same: the original
had a record and replay option built on this. The port reproduces the same generator, and its tests run
whole games unattended on a fixed 16 ms step.

## Units

Positions, speeds and almost every tunable are 16.16 fixed point. A map cell is 32 pixels, so a world is
4096 pixels square. Headings use a full circle of 64 steps in the integer part, and the rotation matrices for
those 64 headings are precomputed. A second angle unit (256 steps) is used by the sine and cosine helpers.
Fixed point was the natural choice on the 3DO's ARM processor, which had no floating point unit; the PC port
kept it, and that is also what makes the game deterministic across machines.

## The data

Return Fire splits its content in an unusual way. The *assets* are on the CD:

- `ART/ART.CAR`: every sprite, floor tile, texture and HUD element, as one relocatable memory image of
  3DO cel control blocks with 8-bit pixels and the palettes ([format](/car)). Sprites have no names; the code
  addresses them by index.
- `ART/TRANS.TBL`: precomputed colour tables for blending and shading in the 8-bit palette. The game
  regenerates it if it is missing.
- `WORLDS/1PLAYER` and `WORLDS/2PLAYER`: 204 level maps, one byte per tile ([format](/rfm)).
- `SOUND`: the sound effects as WAV files, and the whole score as one long WAV.
- `TITLE`: the movies and the banners, and `ART` also holds full-screen pictures (title and pause screens,
  status bars).

But the *design data* is in the executable: the 3D models of every vehicle and building, the vehicle
descriptors (speeds, armour, fuel, weapons, camera), the table of static object types, the projectile types,
the explosion scripts, the debris programs, the sound event table and the music track table. These are C
structures in the program's data section, filled with function pointers next to numbers. A building type,
for example, names its own graphic, its hit points, what it becomes when destroyed, which explosion it
plays, and the function to call when something touches it.

This is typical of a small team's game of the time: the level designers had an editor for maps (the map
files carry an author name and editor dates), while everything else was tuned in the code. It also explains
why OpenRF has to read those tables from the user's `RFIRE.BIN` at start-up instead of shipping them.

A map is small: a header, a few tagged chunks (the name, an optional override of the vehicle stock and enemy
numbers, the difficulty level) and a 128 × 128 grid of tile bytes. Each tile byte goes through a table that
turns it into a terrain id, an optional static object, a side, and an optional special action (the home pad,
the flag sites). So the map editor only placed tiles; the meaning of a tile lives in the program.

## The world

The level in memory is one array of 128 × 128 cell words. A cell word packs:

- the terrain (grass, road, water, the shore pieces, the pad, ...), which is also the floor tile to draw;
- the static object on the cell, if any (a bush, a palm, a wall, a building, a gun tower, a fuel dump ...);
- the side that owns it;
- the static object's hit points;
- the head of the list of moving objects standing in the cell;
- a radar flag.

So the map *is* the spatial index. Every moving object is linked into the list of the cell it stands in,
and is relinked when it crosses into another. Collision and drawing both work cell by cell, and a static
object is not an object at all until it needs to be (a gun tower becomes one when it wakes, a gate when it
opens). This keeps the cost of a level with thousands of trees and walls close to zero: they are bits in
a word until something touches them.

Static objects get a small per-cell offset from a 16 × 16 jitter table, used both for drawing and for
collision, so a field of palms does not look like a grid.

Water is not a wall. Terrain never blocks anything; instead, each frame a vehicle asks what kind of water it
is in (none, shallow, deep), and for the shore tiles that is decided by testing the vehicle's shape against
a polygon of the shoreline. Shallow water slows vehicles down; in deep water the ground vehicles sink.

## The gameplay

### The rules

Each side has a home bunker under a lift pad, a stock of vehicles, and a flag hidden in one of its flag
buildings. The player chooses a vehicle in the bunker, rides the lift up, and goes out to find the enemy
flag: destroy enemy flag buildings until the flag is uncovered, pick it up with the jeep (the only vehicle
that can carry it) and bring it back onto your own pad, or next to your own lift. A side that runs out of
jeeps loses. In one-player games the other side has no vehicles: its defence is the towers, drones,
soldiers, mines and the submarine. In two-player games both sides are human, on a split screen.

When a building that might hide the flag is destroyed, the flag may move to another intact flag building
instead of appearing; only when the count runs out does the flag show itself. So the search cannot be
cut short by guessing.

### Objects

Everything that moves is an *object* from one fixed pool of 512. An object has a position, a heading, a
speed, a side, a graphic, a parent and children (a carried flag is a child of the jeep), and a *class*. The
class is a table of functions and settings: create, destroy, update, think, the hooks for when it hits a
static object or another object, the damage handler, and a priority. About twenty classes exist:
projectile, vehicle, gun turret, debris, shadow, the bunker lift, wreck, tracer, drone, mine, explosion, flag,
gate, soldier, submarine, homing missile, remains and grenade.

Most classes also have a *think* function, a pointer the object swaps to change what it is doing: a soldier
switches between walking out of a building, running away, standing, and throwing a grenade by replacing its
think function. These are state machines without a state variable.

Removal is deferred: an object marks itself for deletion and the update loop reaps it between updates, so no
object disappears while another is looking at it. When the pool is full, objects of the "can be recycled"
classes (remains, debris) are reused first.

### Collision

A graphic carries a *shape*, a list of parts stacked by height. A part is a point, a box, a convex polygon
that rotates with the heading, or a swept segment for fast projectiles, and it has a height range, a
"solid" bit set and a mask of what it collides with. Two shapes overlap when two of their parts overlap in
height, their bits match, and the 2-D test passes. A move is tried by moving the object, testing the 3 × 3
cells around it, and undoing the move if it hit something. The helicopter flies above the tallest static
part, so in the air it hits nothing on the ground.

What happens on a touch is decided by the *lower-priority side's* hook. A tank drives over rocks and crushes
bushes when it is going fast enough; a jeep is stopped by both. A fuel dump or an ammo dump has a pad part
that blocks nothing and records the vehicle standing on it, which is how resupply works. A vehicle runs a
soldier over by answering the soldier's touch.

### Vehicles and their physics

The four vehicles are one class with a descriptor each: tank, jeep, MSV (the armoured missile vehicle)
and helicopter. Only one vehicle per side is out at a time, and its state lives in a per-side record.

The input is one 32-bit word per player per frame: direction bits, three fire buttons and two extra buttons,
plus *magnitude* bytes for the two axes, so a joystick could give a partial deflection. The keyboard sets
full magnitude. Each frame a controller function turns the word into a control word (forward, back, left,
right, and press / hold / release edges for each button).

The movement is simple and per tick: accelerate towards the top speed (forwards or in reverse), let friction
bring it back to zero when there is no input, turn at a fixed rate, and move along the heading. The top speed
is multiplied by a terrain factor: faster on roads and bridges, slower in water. The jeep is different: it
always carries friction, so its real top speed is a little under the nominal one; it steers itself along a
road when the player does not steer; and in water it can turn into a boat. The helicopter has momentum: its
yaw rate and its velocity approach the commanded ones instead of jumping to them, it banks, it drifts a
little when idle, and it can strafe.

Fuel burns in proportion to the distance driven; an empty tank turns the vehicle into a wreck. Standing still
on a fuel or ammo pad refills it. Standing still on your own pad and pressing fire docks the vehicle: it goes
back into the stock and the player chooses again.

The lift is an object of its own: it rises from under the pad carrying the chosen vehicle, and only when it
reaches the top does the vehicle object get created. Objects caught on a rising lift are damaged, mines on it
removed.

### Weapons and damage

Each vehicle has its own weapons: the tank's cannon with a raised (lobbing) mode and a turret that turns
independently, the jeep's grenades, the MSV's missiles and (in two-player games) mines, and the helicopter's
guns and missiles. A shot creates a projectile object of a projectile type that gives its speed, damage,
gravity, life time, model and the explosions for what it hits (ground, road, water, an object, a building).
Gravity is applied by turning the projectile's pitch, not by adding a vertical velocity. Grenades follow a
closed-form arc instead.

Damage to a vehicle is the hit minus the vehicle's armour, off its hit points. Damage to a static object
comes off the three-bit hit points stored in its cell; at zero the object is destroyed, which plays its
explosion and swaps in its "destroyed" type. A building on its last hit points lets its soldiers out (a
prison lets out the *other* side's prisoners).

Explosions are small scripts: a byte code with operations to play a sound, change the graphic, damage a box
around the explosion, change the neighbouring cells (a wall that shows cracks on both sides), throw debris,
pick a random branch, and wait for a frame. Debris pieces are the faces of the destroyed model, each flying
off under a little program of its own (speed, spin, gravity, bounce, settle). A destroyed building is
literally taken apart into the quads it was drawn from.

### The enemies

**Gun towers** are static objects until an enemy vehicle comes into view. The tower wakes up *during
drawing*: when the renderer queues a tower in the view of a player whose vehicle belongs to the other side,
the tower becomes a turret object. The turret waits a short random reaction time, turns towards the target,
picks one of two elevations, and fires; before each shot it sends an invisible tracer to check that the line
of fire is clear of its own side. A turret that is not drawn for a while goes back to being a static tower.

**Drones** punish standing still. A vehicle that stays in the same cell too long calls a drone, unless
several enemy towers were already in view. The drone appears at a random point on one side of a box around
the target, flies at a fixed height, turns to face its target, and fires when it is aimed and in range. The
number of drones a level allows grows with the difficulty level.

**The submarine** guards the map edge: a helicopter that flies off the map is followed by a submarine that
surfaces under it and fires a homing missile.

**Soldiers** come out of destroyed buildings. They pick the nearest vehicle, run away from it if it is close
or stand if it is not, and throw grenades at enemy vehicles that come near. Vehicles crush them.

**Mines** are placed at random on the later levels, near roads first. A mine arms after a moment and goes
off when a vehicle touches it.

None of these enemies uses the player's input word. They are separate classes, each a short state machine
over a target, a timer and a few angles. The difficulty comes from their number and placement in the map,
not from planning.

### Two players

The two-player game is the same game with both sides human: two views side by side, two HUD blocks, two
input words. Towers wake for whichever player's view draws them, drones hunt either side. A player who has
nothing left watches the screen until the other one wins. One detail differs: in the two-player frame the
input is read after the objects are updated, so the vehicles act on the previous frame's input.

## Saving

There are no saved games. The game keeps high scores (for one player, the fastest time per map; for two,
the wins and draws of each pair of names) in one small binary record, lightly obfuscated, plus the level
progress per player. The original keeps them in the registry; the port uses the host's storage.

## Presentation

### Rendering

The world is drawn as a tilted top-down view, in painter's order. The renderer walks the rows of map cells
from the far edge of the screen to the near one. For each row it computes a perspective scale from a 1/z
table, draws the visible floor tiles as trapezoids, and queues the contents of each cell: the static object's
model and every object in the cell's list. The queue is depth-sorted by insertion, and after the floor the
models are drawn back to front.

Vehicles and buildings are real 3D models, not pre-rendered sprites: each is a set of vertices and textured
quads. A model is rotated by one of the 64 yaw matrices (and a pitch matrix for turrets and tilted parts),
projected, and every visible face becomes one cel with four explicit corners. Faces are textured with affine
mapping, without perspective correction. Shadows are separate faces drawn in a darkening mode.

All colour effects work in the 8-bit palette through lookup tables: a 256 × 256 table for averaging two
colours (translucency), 32 darkening and 32 brightening tables, and per-cel remap tables (team colours).
The water's shimmer is palette cycling. Screen fades lower the palette by subtraction.

The logical screen is always 320 × 240. The 640 × 480 mode draws the same cels at twice the scale, with only
the full-screen pictures in separate high-resolution versions.

The camera follows a target with springs: position, height and pitch each approach their targets with a
velocity limited by acceleration. The game changes what the camera does by pushing *trackers* (the bunker
view, straight down over the rising lift, the driving view, the view of a dying vehicle) that set the
targets.

### HUD and radar

The status bar is a full-screen picture blitted under the HUD. The gauges are cels redrawn only when a dirty
bit says they changed. The radar is a 128 × 128 image with one pixel per map cell, updated as cells change,
with blips for the moving objects.

### Sound

Game code never talks to the sound hardware. It queues commands (play this event for this object, stop,
update), and the sound service runs them once a frame. An *event* names a sample and gives a priority that
decays after a while, a mix share and flags. Fifteen logical sounds can exist at once, sorted by priority
and loudness, and the loudest take the mix budget first, so a big explosion pushes the small sounds out.

Loudness falls off linearly with distance from a *listener*. There are two listeners, and the left channel
is computed from the first, the right from the second. In one player both are the same player (with a
small offset for a little stereo spread); in two players, player 1 hears the left speaker and player 2 the
right. This is a cheap and clever way to give a split screen split sound.

### Music

The whole score is one long recording on the CD. Each music *track* is a byte range of it, with a loop
point. A track can move to another with a fade, a hard cut, or *seamlessly*, continuing the same stream
position with new loop points: the three tank themes and the two helicopter themes are entry points into
one long piece each, so changing between them never restarts the music.

A music *director* chooses the track each frame from what is happening: the vehicle being driven, a vehicle's
death, the flag being uncovered or carried, the submarine, the bunker, the win. Requests carry a priority,
and the current priority decays over time, so a short sting holds the music for a moment and then lets
the game's state take it back.

### Movies

The intro and victory films are AVI files that Silent Software re-packed into fixed-size blocks, so that a
reader thread could stream them from the CD without parsing AVI at run time ([format](/stm)). The video is
Cinepak, slaved to the audio clock: when the picture falls behind, it skips to the next key frame.

## Quirks kept

OpenRF reproduces the original's bugs, because they are part of how the game plays:

- The raised tank shell only levels out and never comes down: it flies on at the projectile ceiling.
- Every sound event has a frequency, but the value is never sent to the sound hardware: samples play at their
  own rate, and only the jeep and helicopter engines change pitch.
- The drone's gun has a limit on how far up it can point, but the test is written wrongly and never applies.
- Soldiers that are not drawn for two seconds are removed, wrecks and debris time out from when they were last
  drawn, and towers wake from the draw pass. The renderer is part of the simulation; a headless run must
  still draw the view.
- The random mine placement places one mine fewer in its second pass than it means to.

## What is not fully understood

The game is fully ported, but some details are still open in the notes: the meaning of a few fields in the
vehicle descriptors and the map header, a two-player status screen in the bunker, the radar blips of the
drones, some branches of the music director, the exact rounding of a few cel draw modes, and how the movie
frames are matched to the screen palette. See the open questions at the end of each
[internals](/internals/) page.

## Lessons for building such a game

- **Make the map the spatial index.** One word per cell holding the terrain, the static object, its hit points
  and the list of objects in it makes collision, drawing and destruction all local and cheap.
- **Static until touched.** Thousands of trees, walls and towers cost nothing until one is shot at or comes into
  view; only then does it become a full object.
- **One input word for everyone.** Keyboard, joystick and scripted demos produce the same word, and with
  integer maths and a fixed random generator that is all you need for replays and lockstep networking.
- **Behaviour as data plus hooks.** Vehicle and object types are tables of numbers and function pointers, so a
  new building or weapon is mostly a new table entry.
- **Script the effects.** A tiny byte code for explosions and debris gives each destruction its own character
  without new code.
- **Let the music follow the game.** A priority-based director over one continuous score, with seamless
  transitions, sounds composed rather than switched.
- **Split the stereo for a split screen.** Two listeners, one per channel, give two players their own sound
  with no extra mixing cost.
- **Watch what drawing does.** Here the renderer wakes enemies and expires objects. It works, but it ties the
  game's behaviour to the view; keep that kind of coupling deliberate.

## Further reading

- [Engine architecture](/architecture): the program flow, timing, input, sound, music and the renderer, with
  addresses.
- [Game simulation](/game): objects, collision, vehicles, weapons, enemies, rules, two players.
- [Renderer](/render): the camera, the floor walk, the model queue and the model format.
- [ART.CAR](/car), [RFM level maps](/rfm), [STM movies](/stm): the file formats.
- [Portable core](/internals/portable-core): how the port is structured to run on gasm and in the browser.
- [Credits](/credits): the published and community sources the port relied on.
