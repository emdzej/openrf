/* RFM loader, reproducing the cell build of FUN_004322f0 (docs/rfm.md). */
#include "world.h"
#include "assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

bool world_load(World *w, const char *rel)
{
    size_t sz;
    uint8_t *d = file_read_all(rel, &sz);
    if (!d) return false;
    memset(w, 0, sizeof *w);
    uint32_t dsize = sz >= 0x50 ? rd32(d + 0x44) : 0, doff = sz >= 0x50 ? rd32(d + 0x48) : 0;
    int mw = sz >= 0x50 ? rd16(d + 8) : 0, mh = sz >= 0x50 ? rd16(d + 10) : 0;
    if (sz < 0x50 || memcmp(d, "WRL", 4) || !d[0x40] || dsize + doff != sz ||
        mw > WORLD_W || mh > WORLD_H || (uint32_t)(mw * mh) > dsize) {
        free(d);
        return false;
    }
    w->players = d[0x16];
    snprintf(w->author, sizeof w->author, "%.40s", (const char *)d + 0x17);
    for (int i = 0; i < 6; i++) w->vehicles[i] = -1;
    for (uint32_t p = 0x50; p + 8 <= doff; ) {
        uint32_t n = rd32(d + p + 4);
        const uint8_t *body = d + p + 8;
        if (n < 8 || p + n > doff) break;
        if (!memcmp(d + p, "NAME", 4)) snprintf(w->name, sizeof w->name, "%.*s", (int)(n - 8), (const char *)body);
        else if (!memcmp(d + p, "LEVL", 4) && n >= 12) w->level = (int)rd32(body);
        else if (!memcmp(d + p, "VHCL", 4) && n >= 14)
            for (int i = 0; i < 6; i++) w->vehicles[i] = body[i] == 0xFF ? -1 : body[i];
        p += n;
    }
    for (int y = 0; y < WORLD_H; y++)
        for (int x = 0; x < WORLD_W; x++)
            w->cell[y][x] = (Cell){ TER_DEEP, 0, TEAM_NEUTRAL, 0 };
    w->pad_x[0] = w->pad_x[1] = -1;
    int ox = (WORLD_W - mw) / 2, oy = (WORLD_H - mh) / 2;
    for (int y = 0; y < mh; y++)
        for (int x = 0; x < mw; x++) {
            uint8_t b = d[doff + y * mw + x];
            if (b >= 0xF0) b = 0;
            TileDef t = tile_table[b];
            Cell *c = &w->cell[y + oy][x + ox];
            if (t.terrain == 0xFF) { c->terrain = TER_GRASS; continue; }
            c->terrain = t.terrain;
            c->team = t.team;
            if (t.object) {
                c->object = t.object;
                c->strength = obj_table[t.object].strength;
                if (obj_table[t.object].terrain_override != 0xFF) c->terrain = obj_table[t.object].terrain_override;
            }
            if (t.handler == 1 && t.team < 2) { w->pad_x[t.team] = x + ox; w->pad_y[t.team] = y + oy; }
            if (t.handler == 2 && t.team < 2 && w->nflags[t.team] < 64) {
                int k = w->nflags[t.team]++;
                w->flag_x[t.team][k] = x + ox; w->flag_y[t.team][k] = y + oy;
            }
        }
    /* Bridge spans are filled between matching end pieces. */
    for (int y = 0; y < WORLD_H; y++)
        for (int x = 0; x < WORLD_W; x++) {
            Cell *c = &w->cell[y][x];
            if (c->terrain == TER_BRIDGE_H0)
                for (int j = x + 1; j < WORLD_W && w->cell[y][j].terrain != TER_BRIDGE_H1; j++)
                    w->cell[y][j].object = OBJ_BRIDGE_H, w->cell[y][j].team = c->team;
            if (c->terrain == TER_BRIDGE_V0)
                for (int j = y + 1; j < WORLD_H && w->cell[j][x].terrain != TER_BRIDGE_V1; j++)
                    w->cell[j][x].object = OBJ_BRIDGE_V, w->cell[j][x].team = c->team;
        }
    free(d);
    return true;
}
