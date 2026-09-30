/* Random mine placement FUN_0041c9b0 (StartNewGame 0x40b710: VHCL mines, or level*4 on 1-player levels 6-9)
   and the per-level install of the MAN / Flag hooks. The Mine class itself is in weapon.c. */
#include "rules.h"
#include "weapon.h"
#include <stdlib.h>

static int bx0, by0, bx1, by1;               /* DAT_00457f28/34/30/38: team 0 pad cell -2 .. +2 */

static uint32_t terr_at(int i) { return (i >= 0 && i < 128 * 128) ? (G.cell[i] & 0x7f) : 0; }
static bool road(uint32_t t) { return t > 0x48 && t < 0x5a; }            /* FUN_0041cd80 */

/* FUN_0041cdb0 (near_road = 1 is the inlined first pass): open ground away from the home pad. */
static bool mine_site(int i, int x, int y, int near_road)
{
    uint32_t w = G.cell[i];
    if (w & 0x3f80) return false;
    if (bx0 < x && x < bx1 && by0 < y && y < by1) return false;
    uint32_t t = w & 0x7f;
    if (w & 8) return false;
    if (t > 0x53) return false;
    if (t > 0x48) return true;
    if (t != 3 && t != 0 && t < 0x34) return false;
    if (!near_road) return true;
    return road(terr_at(i - 128)) || road(terr_at(i + 128)) || road(terr_at(i - 1)) || road(terr_at(i + 1));
}

/* One pass: collect the sites, then place up to `left` mines at random unused sites, each at a random
   point 6..30 px into the cell. Returns the count the original carries into the next pass (left - 1 when it
   ran out of sites: the decrement at the top of its loop). */
static int place_pass(int left, int near_road)
{
    uint8_t (*site)[2] = malloc(sizeof(uint8_t[2]) * (128 * 128 + 1));
    if (!site) return 0;
    int n = 0;
    for (int i = 0; i < 128 * 128; i++)
        if (mine_site(i, i & 127, i >> 7, near_road)) { site[n][0] = (uint8_t)(i & 127); site[n][1] = (uint8_t)(i >> 7); n++; }
    site[n][0] = 0;
    int remaining = n, carry = left - 1;
    for (;;) {
        carry = left - 1;
        if (left < 1 || remaining < 1) break;
        int32_t r = rand_range(remaining--);
        int k;
        for (k = 0; k < n; k++) {
            if (site[k][0] == 0xff) continue;
            if (--r < 1) {
                int y = site[k][1], x = site[k][0];
                int32_t py = (rand_range(0x19) + y * 0x20 + 6) * 0x10000;
                int32_t px = (rand_range(0x19) + x * 0x20 + 6) * 0x10000;
                spawn_mine(px, py);
                site[k][0] = 0xff;
                break;
            }
        }
        left = carry;
        if (k == n) break;                                                 /* (cannot happen: r < remaining) */
    }
    free(site);
    return carry;
}

void place_random_mines(int n)                                             /* FUN_0041c9b0 */
{
    const PadRec *p = G.teams[0].pad;
    if (!p || n < 1) return;
    bx0 = (p->x >> 21) - 2; by0 = (p->y >> 21) - 2;
    bx1 = (p->x >> 21) + 2; by1 = (p->y >> 21) + 2;
    int left = place_pass(n, 1);                                           /* next to roads first */
    if (left > 0) place_pass(left, 0);
}

void rules_level_start(int32_t mines)
{
    if (!game_hooks.spawn_men) game_hooks.spawn_men = spawn_men_from_building;
    if (!game_hooks.spawn_man) game_hooks.spawn_man = spawn_man;
    if (!game_hooks.flag_spawn) game_hooks.flag_spawn = flag_spawn;
    rules_music_bits = 0;
    if (mines > 0 && mines != 0xff) place_random_mines(mines);
}
