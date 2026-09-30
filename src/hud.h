/* In-game HUD widgets in the status bar (HudInit 0x422640, HudUpdateAll 0x422750, widget table
   0x44acc0) and the 128x128 radar image (RadarUpdateMovers 0x422fb0). */
#pragma once
#include "platform.h"
#include <stdint.h>

typedef struct Obj Obj;

/* Events (GHUD_* in game/game.h, raised through game_hooks.hud):
   GHUD_SELECT  ViewEnterBunkerSelect 0x404d30 (dashboard slides away)
   GHUD_DASH    bunker script step FUN_004043a0 (vehicle dashboard slides in)
   GHUD_VEH_INIT VehicleInit 0x428210 (radar registration FUN_00422c70)
   GHUD_VEH_START VehicleUpdate 0x428490 first frame (gauges, radar widget)
   GHUD_VEH_GONE VehicleDestroy 0x4283c0 */

void hud_init(int nplayers);                              /* HudInit */
void hud_event(int ev, int team, Obj *o);
void hud_mark_dirty(int player, uint32_t mask);           /* HudMarkDirty 0x422820 */
/* Per-frame: RadarUpdateMovers (call after the sim, before the view). */
void hud_radar_update(void);
/* HudUpdateAll: draws the dirty widgets into fb (which must keep the previous frame's status bar). */
void hud_update_all(Framebuffer *fb, int scale, int nplayers);
