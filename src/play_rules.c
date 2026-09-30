/* App glue for src/game/rules.h: EndGame hook + game clock (GameClockStart/Stop 0x42d830/0x42d860), the
   flag camera tracker, the flag cues of Mus_Director 0x41d730, the draw fields of MAN / Gate / Flag and
   the end-of-game sequence (endgame.c). */
#include "play_rules.h"
#include "endgame.h"
#include "music.h"
#include "platform.h"
#include "game/rules.h"
#include "game/vehicle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PlayOutcome play_outcome = { -2, 0, 0, 1 };
static View *g_view[2];
static uint64_t clock_t0;
static int mus_state;                         /* DAT_00480e5c, flag branches only (5 Flag Pickup, 7 Discovery) */
static int demo_rules;                        /* OPENRF_DEMO=rules: gate, soldiers and the enemy flag near the pad; =win: EndGame(0) */

/* Debug scene (screenshots): once the tank is out, an own gate in its path south of the pad (walls either
   side), an enemy building east that lets its men out, a prison west and the enemy flag lying north-east. */
static void demo_scene(void)
{
    int px = G.teams[0].pad->x >> 21, py = G.teams[0].pad->y >> 21;
    set_cell_static(43, &G.cell[(py + 3) * 128 + px], 0, 0);
    for (int k = 1; k <= 2; k++) {
        set_cell_static(46, &G.cell[(py + 3) * 128 + px - k], 0, 0);
        set_cell_static(46, &G.cell[(py + 3) * 128 + px + k], 0, 0);
    }
    set_cell_static(24, &G.cell[(py + 8) * 128 + px + 7], 0, 1);
    set_cell_static(15, &G.cell[(py + 8) * 128 + px + 3], 0, 1);
    int32_t f[3] = { (px + 2) * 0x200000 + 0x100000, (py - 1) * 0x200000 + 0x100000, 0 };
    if (!G.flag_obj[1]) G.flag_obj[1] = flag_spawn(1, f);
}

static void demo_men(void)                     /* the buildings south of the gate let their men out */
{
    int px = G.teams[0].pad->x >> 21, py = G.teams[0].pad->y >> 21;
    uint32_t *b = &G.cell[(py + 8) * 128 + px + 7], *pr = &G.cell[(py + 8) * 128 + px + 3];
    while (CELL_HP(*b) > 1) bno_damage(0x10000, NULL, b, NULL);    /* nearly destroyed: SpawnMenFromBuilding */
    while (CELL_HP(*pr) > 1) bno_damage(0x10000, NULL, pr, NULL);  /* prisoners of war: our men */
}

static void on_end_game(int winner)           /* EndGame 0x40f380: GameClockStop */
{
    play_outcome.winner = winner;
    play_outcome.time_ms = (uint32_t)(plat_ticks_ms() - clock_t0);
}

static void on_flag_camera(int team, Obj *flag)
{
    if (team < 0 || team > 1 || !g_view[team]) return;              /* 1 player: view 0 only */
    /* ViewAddTracker(view, 0, flag, 0xa0000, 0x180000, DAT_0043eea0 = -150 px, 0, NULL), timer 120. The
       original replaces the view's only tracker node; here it outranks the vehicle tracker until it expires. */
    CamTracker *t = camera_add_tracker(g_view[team], 0x81, flag->pos, NULL, 0xa0000, 0x180000, (int32_t)0xff6a0000, false, NULL);
    if (t) t->timer = 0x78;
}

void play_rules_start(View *v0, View *v1)
{
    g_view[0] = v0;
    g_view[1] = v1;
    clock_t0 = plat_ticks_ms();
    play_outcome = (PlayOutcome){ -2, 0, G.level, G.nplayers };
    mus_state = 0;
    const char *d = getenv("OPENRF_DEMO");
    demo_rules = d && !strcmp(d, "rules") ? 1 : d && !strcmp(d, "win") ? 10 : 0;
    game_hooks.end_game = on_end_game;
    rules_hooks.flag_camera = on_flag_camera;
}

void play_rules_collect(Obj *o, RenderObj *r)
{
    switch (o->cls->id) {
    case 14:                                                       /* MAN: frame; the draw stamps +0x5c */
        r->u64 = MAN_FRAME(o);
        r->stamp = &MAN_DRAWN(o);
        break;
    case 13:                                                       /* Gate: opening */
        r->u64 = GATE_OPEN(o);
        r->stamp = NULL;
        break;
    case 12:                                                       /* Flag: cloth frame */
        r->u5c = FLAG_WAVE(o);
        r->stamp = NULL;
        break;
    }
}

void play_rules_frame(void)
{
    if (demo_rules == 10 && g_tick >= 120) { rules_end_game(0); demo_rules = 11; }   /* OPENRF_DEMO=win: end sequence */
    if (demo_rules == 1 && g_tick >= 300) { demo_scene(); demo_rules = 2; }
    if (demo_rules == 2 && g_tick >= 600) { demo_men(); demo_rules = 3; }
    uint32_t b = rules_music_bits;
    rules_music_bits = 0;
    if ((b & 0x100) && mus_state < 8) {                            /* flag revealed: 11 Flag Discovery, 0xfe */
        music_request(MUS_FLAG_DISCOVERY, 0xfe, 0);
        mus_state = 7;
        return;
    }
    if (b & 0x200) {                                               /* enemy flag carried: 12 Flag Pickup, 0x8a */
        music_request(MUS_FLAG_PICKUP, 0x8a, 0);
        mus_state = 5;
        return;
    }
    if (mus_state == 7 && music_current() != MUS_FLAG_DISCOVERY) mus_state = 0;
    if (G.nplayers > 1) {                                          /* DAT_00480e9c: views in the bunker select */
        int in_bunker = 0;
        for (int t = 0; t < 2; t++) in_bunker += G.views[t].mode == BV_SELECT || G.views[t].mode == BV_LAUNCH;
        if (in_bunker >= G.nplayers && mus_state < 3) { music_request(MUS_BUNKER, 0x68, 0); mus_state = 3; }
        else if (in_bunker < G.nplayers && mus_state == 3) mus_state = 0;   /* a vehicle track takes over (state 4) */
    }
    if (mus_state == 5) {                                          /* state 1: Mus_Request(-1, 100) */
        mus_state = 0;
        music_request(MUS_SILENCE, 100, 0);
        /* then the 0x400 branch (Mus_PickVehicleTrack) brings the vehicle's music back (DAT_00480e48: the
           vehicle that started last, of either side) */
        Obj *v = get_team_vehicle(0);
        if (G.nplayers > 1) {
            Obj *v1 = get_team_vehicle(1);
            if (!v || (v1 && v1->id > v->id)) v = v1;
        }
        if (v && v->cls->id == 1 && v->p60) {
            uint32_t m = veh_state(v)->def->music;
            if ((int8_t)(m & 0xff) >= 0) music_request((int8_t)(m & 0xff), (int)((m >> 8) & 0xff), (int)v->id);
        }
    }
}

bool play_rules_finish(const char *rfm_rel, bool quit)
{
    game_hooks.end_game = NULL;
    rules_hooks.flag_camera = NULL;
    g_view[0] = g_view[1] = NULL;
    if (quit) return false;
    if (G.winner == -2) return true;                               /* left with Esc */
    play_outcome.winner = G.winner;
    GameResult r = { G.winner, G.level, G.nplayers, play_outcome.time_ms, rfm_rel };
    fprintf(stderr, "game over: winner %d, level %d, %u.%03u s\n", r.winner, r.level + 1, r.time_ms / 1000, r.time_ms % 1000);
    if (!endgame_fade_out(1000)) return false;                     /* EndGame: SetFadeTarget(0, 1000) */
    if (!endgame_sequence(&r)) return false;
    if (r.nplayers == 2 && highscore_record(&r)) {                 /* State_BackScreen: RecordHighScore */
        HighScore2P hs[64];
        int n = highscore_list2(hs, 64);
        fprintf(stderr, "2-player scores (%s):\n", highscore_path());
        for (int i = 0; i < n && i < 64; i++)
            fprintf(stderr, "  %-16s %3u  %-16s %3u  draws %u\n", hs[i].name[0], hs[i].wins[0], hs[i].name[1], hs[i].wins[1], hs[i].draws);
    } else if (r.nplayers == 1 && highscore_record(&r)) {
        HighScore1P hs[64];
        int n = highscore_list(hs, 64);
        fprintf(stderr, "high scores (%s):\n", highscore_path());
        for (int i = 0; i < n && i < 64; i++)
            fprintf(stderr, "  level %2d  %-12s %-16s %u.%03u s\n", hs[i].level, hs[i].map, hs[i].player,
                    hs[i].time_ms / 1000, hs[i].time_ms % 1000);
    }
    return true;
}
