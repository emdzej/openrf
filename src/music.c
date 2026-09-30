#include "music.h"
#include "assets.h"
#include "platform.h"
#include "exe.h"
#include <stdio.h>
#include <string.h>

enum { M_ONESHOT = 2 };
enum { T_FADE_RESTART, T_SEAMLESS, T_CUT, T_REVERT };

typedef struct {
    const char *name, *file;
    unsigned start, end, alt;   /* byte offsets in the WAV data chunk; alt = loop restart */
    unsigned char mode, gmin, gmax, flags, trans;
} Track;

/* Track table 0x443aa0 (18 x 0x2c, docs/architecture.md section 7) read from RFIRE.BIN, with the start / loop /
   end indices (negated) resolved through the byte-offset table 0x44bf78. Tracks 0..16 stream SCORE.WAV, 17 DRUMS.WAV. */
static Track tracks[MUS_COUNT];
static void music_tables_load(void)
{
    for (int i = 0; i < MUS_COUNT; i++) {
        uint32_t a = 0x443aa0 + 0x2c * (uint32_t)i;
        Track *t = &tracks[i];
        t->name = exe_str(exe_u32(a + 4));
        t->file = i == MUS_DRUMS ? "SOUND/DRUMS.WAV" : "SOUND/SCORE.WAV";
        t->start = exe_u32(0x44bf78 - 4 * (uint32_t)exe_s32(a + 0x18));
        t->alt = exe_u32(0x44bf78 - 4 * (uint32_t)exe_s32(a + 0x1c));
        t->end = exe_u32(0x44bf78 - 4 * (uint32_t)exe_s32(a + 0x20));
        t->mode = (unsigned char)exe_s32(a + 0x14);     /* bit 1 one-shot (M_ONESHOT), bit 2 restart */
        t->gmin = exe_u8(a + 1); t->gmax = exe_u8(a + 2); t->flags = exe_u8(a + 3);
        t->trans = exe_u8(a + 9);
    }
}
EXE_LOADER(music_tables_load)

/* Seamless pairs from the transition lists (Tank 1/2/3, Heli 1/2, Jeep->Jeep). */
static int transition(int from, int to)
{
    if (from < 0) return T_CUT;
    if (from <= MUS_TANK3 && to <= MUS_TANK3) return T_SEAMLESS;
    if ((from == MUS_HELI1 || from == MUS_HELI2) && (to == MUS_HELI1 || to == MUS_HELI2)) return T_SEAMLESS;
    if (from == MUS_JEEP && to == MUS_JEEP) return T_SEAMLESS;
    if (from == MUS_FLAG_PICKUP && to == MUS_JEEP_DEATH) return T_CUT;
    return tracks[to].trans;
}

static int cur = MUS_SILENCE, prev = MUS_SILENCE, pending = MUS_SILENCE - 1;
static int cur_prio, base_prio, cur_owner;
static bool enabled = true;
static float vol = 1, vol_target = 1;   /* 0x444 per 16 ms tick on a 0..0x7fff scale */

static void start_track(int t)
{
    const Track *k = &tracks[t];
    plat_music_play(assets_path(k->file), k->start, k->end, k->alt, !(k->mode & M_ONESHOT));
    vol = vol_target = 1;
    plat_music_set_volume(vol);
}

void music_init(void) { cur = prev = MUS_SILENCE; pending = MUS_SILENCE - 1; cur_prio = 0; }

bool music_request(int track, int prio, int owner)
{
    if (prio < cur_prio) return false;
    if (prio == cur_prio && cur >= 0 && track >= 0 &&
        !(track >= tracks[cur].gmin && track <= tracks[cur].gmax)) return false;
    if (track >= 0 && !enabled && !(tracks[track].flags & 1)) return false;
    cur_prio = base_prio = prio;
    cur_owner = owner;
    if (track == cur) return true;
    pending = track;
    return true;
}

void music_owner_changed(int owner)
{
    if (cur_owner && owner == cur_owner) { cur_owner = 0; music_request(MUS_SILENCE, 0x7fffffff, 0); }
}

void music_set_enabled(bool on)
{
    enabled = on;
    if (!on && cur >= 0 && !(tracks[cur].flags & 1)) { plat_music_stop(); cur = MUS_SILENCE; }
}

void music_service(void)
{
    if (cur_prio > base_prio - 0x28) cur_prio--;
    if (pending >= MUS_SILENCE) {
        int to = pending;
        if (to == MUS_SILENCE) {
            vol_target = 0;
        } else {
            int tr = transition(cur, to);
            if (tr == T_SEAMLESS) {
                const Track *k = &tracks[to];
                plat_music_set_range(UINT32_MAX, k->end, k->alt, !(k->mode & M_ONESHOT));
            } else if (tr == T_FADE_RESTART && cur >= 0 && vol > 0) {
                vol_target = 0;          /* start after the fade completes */
                goto fade;
            } else {
                start_track(to);
            }
        }
        prev = cur;
        cur = to;
        pending = MUS_SILENCE - 1;
    }
fade:
    if (vol != vol_target) {
        const float step = (float)0x444 / 0x7fff;
        vol = vol < vol_target ? (vol + step > vol_target ? vol_target : vol + step)
                               : (vol - step < vol_target ? vol_target : vol - step);
        plat_music_set_volume(vol);
        if (vol == 0 && pending >= 0) { start_track(pending); prev = cur; cur = pending; pending = MUS_SILENCE - 1; }
        else if (vol == 0 && cur == MUS_SILENCE) plat_music_stop();
    }
    /* Mus_OnTrackEnd: one-shots fall back to the previous track, or the bunker theme. */
    if (cur >= 0 && pending < MUS_SILENCE && plat_music_ended()) {
        int ended = cur;
        cur_prio = 0;
        if (ended == MUS_WIN) { plat_music_stop(); cur = MUS_SILENCE; }
        else if (prev >= 0 && !(tracks[prev].mode & M_ONESHOT)) music_request(prev, 100, 0);
        else music_request(MUS_BUNKER, 0x40, 0);
    }
}

bool music_is_playing(void) { return cur >= 0 || pending >= 0; }
int music_current(void) { return cur; }
