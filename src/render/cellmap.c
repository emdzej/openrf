/* Packed cell map from an .RFM for rendering: LoadLevelMap 0x4322f0 cell build,
   SetCellStaticObject 0x417850 and InitCellJitterTable 0x41f190 (as tools/view.py World). */
#include "render.h"
#include "../world.h"
#include "../assets.h"
#include <stdlib.h>
#include <string.h>

static uint32_t set_static(uint32_t cell, int t, int hp, int team)   /* SetCellStaticObject 0x417850 */
{
    const RenderBno *b = &render_bno[t];
    if (!b->model_addr || t == 0) return cell & 0xf1ff0000u;
    if (b->terrain != 0xff) {
        int ter = b->flags == 8 ? b->terrain + team : b->terrain;
        cell = (cell & ~0x7fu) | (ter & 0x7f);
    }
    cell = (cell & ~0x3f80u) | (uint32_t)t << 7;
    int v = 0;
    if (b->strength) {
        v = hp + b->strength;
        v = v < 1 ? 1 : v > 15 ? 15 : v;
    }
    cell = (cell & ~0xe000000u) | (((uint32_t)v << 25) & 0xe000000u);
    cell = (cell & ~0xc000u) | (((uint32_t)team << 14) & 0xc000u);
    return cell;
}

bool cellmap_load(CellMap *m, const char *rel)
{
    size_t sz;
    uint8_t *d = file_read_all(rel, &sz);
    if (!d) return false;
    memset(m, 0, sizeof *m);
    if (sz < 0x50 || memcmp(d, "WRL", 4)) { free(d); return false; }
    int w = d[8] | d[9] << 8, h = d[10] | d[11] << 8;
    uint32_t size = d[0x44] | d[0x45] << 8 | d[0x46] << 16 | (uint32_t)d[0x47] << 24;
    uint32_t off = d[0x48] | d[0x49] << 8 | d[0x4a] << 16 | (uint32_t)d[0x4b] << 24;
    if (w > 128 || h > 128 || off > sz || size > sz - off || (uint32_t)(w * h) > size) { free(d); return false; }
    const uint8_t *tiles = d + off;
    for (int i = 0; i < 128 * 128; i++) m->cell[i] = 2;
    int ox = (128 - w) >> 1, oy = (128 - h) >> 1;
    uint32_t seed = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int b = tiles[y * w + x];
            seed += b;
            if (b > 0xef) b = 0;
            int i = (y + oy) * 128 + x + ox;
            TileDef t = tile_table[b];
            if (t.terrain == 0xff) { m->cell[i] &= ~0x7fu; continue; }
            m->cell[i] = (m->cell[i] & ~0x7fu) | t.terrain;
            if (t.object) m->cell[i] = set_static(m->cell[i], t.object, 0, t.team);
            if (t.handler == 1 && t.team < 2 && m->npads[t.team] < 8) {
                m->pad_x[t.team][m->npads[t.team]] = x + ox;
                m->pad_y[t.team][m->npads[t.team]++] = y + oy;
            }
        }
    for (int i = 0; i < 128 * 128; i++) {   /* bridge spans */
        if ((m->cell[i] & 0x7f) == 0x54)
            for (int j = i + 1; j < 128 * 128 && (m->cell[j] & 0x7f) != 0x55; j++)
                m->cell[j] = set_static(m->cell[j], 0x4a, 0, (m->cell[i] & 0xc000) >> 14);
        if ((m->cell[i] & 0x7f) == 0x56)
            for (int j = i + 128; j < 128 * 128 && (m->cell[j] & 0x7f) != 0x57; j += 128)
                m->cell[j] = set_static(m->cell[j], 0x4b, 0, (m->cell[i] & 0xc000) >> 14);
    }
    /* g_CellJitter256 0x472290: srand(sum of raw tile bytes), MSVC rand, RandRange(n) = (r*2n)>>16 */
    uint32_t s = seed;
#define RR(n) (s = s * 214013u + 2531011u, (int)(((((s >> 16) & 0x7fff) & 0x7fff) * 2 * (n)) >> 16))
    for (int k = 0; k < 256; k++) {
        int a = RR(25) - 12;
        int b = RR(25) - 12;
        int c = RR(11);
        int e = RR(256);
        m->jitter[k][0] = (int8_t)a; m->jitter[k][1] = (int8_t)b;
        m->jitter[k][2] = (int8_t)c; m->jitter[k][3] = (int8_t)e;
    }
#undef RR
    free(d);
    return true;
}

RenderMap cellmap_view(const CellMap *m) { return (RenderMap){ .cell = m->cell, .jitter = (const int8_t (*)[4])m->jitter }; }
