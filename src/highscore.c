/* High-score table RFire_HS (RecordHighScore 0x42d8b0, HighScoresDlgProc 0x42ea20): 0x1c-byte header
   {u32 0x1c, "rfhs", u32 n1P, u32 0x48, u32 n2P, u32 0x50}, 1-player records {u16 level (0x7f = custom map),
   char map[33], char player[33], u32 ms} sorted by level, then the 2-player records. Byte i of the file is
   stored as (plain ^ 0x5a) + "retufire"[i & 7]. Kept under the storage key RFire_HS (plat_storage_*: on the
   SDL build ~/Library/Application Support/Return Fire/RFire_HS). */
#include "endgame.h"
#include "exe.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HS_HDR = 0x1c, HS_REC1 = 0x48, HS_REC2 = 0x50 };
static char hs_key[8];                          /* file key, 8 bytes at 0x44c4f0 in RFIRE.BIN */
static void hs_tables_load(void) { exe_read(hs_key, 0x44c4f0, sizeof hs_key); }
EXE_LOADER(hs_tables_load)

#define HS_KEY "RFire_HS"                        /* storage key (the original's file name) */

const char *highscore_path(void) { return plat_storage_location(HS_KEY); }

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* The decoded file (byte i stored as (plain ^ 0x5a) + key[i & 7]); a fresh header when missing/invalid. */
static uint8_t *hs_load(size_t *size)
{
    uint8_t *b = NULL;
    size_t n = 0;
    int len = plat_storage_get(HS_KEY, NULL, 0);
    if (len > 0 && (b = malloc((size_t)len)) != NULL) {
        n = plat_storage_get(HS_KEY, b, len) == len ? (size_t)len : 0;
        for (size_t i = 0; i < n; i++) b[i] = (uint8_t)((uint8_t)(b[i] - (uint8_t)hs_key[i & 7]) ^ 0x5a);
        if (n < HS_HDR || rd32(b) != HS_HDR || memcmp(b + 4, "rfhs", 5) || rd32(b + 0x10) != HS_REC1 ||
            rd32(b + 0x18) != HS_REC2 || HS_HDR + (size_t)rd32(b + 0xc) * HS_REC1 + (size_t)rd32(b + 0x14) * HS_REC2 > n) {
            free(b);
            b = NULL;
        }
    }
    if (!b) {                                                      /* new table: header only */
        b = calloc(1, HS_HDR);
        wr32(b, HS_HDR);
        memcpy(b + 4, "rfhs", 5);
        wr32(b + 0x10, HS_REC1);
        wr32(b + 0x18, HS_REC2);
        n = HS_HDR;
    }
    *size = n;
    return b;
}

static bool hs_save(const uint8_t *plain, size_t n)
{
    uint8_t *e = malloc(n ? n : 1);
    for (size_t i = 0; i < n; i++) e[i] = (uint8_t)((plain[i] ^ 0x5a) + (uint8_t)hs_key[i & 7]);
    bool ok = plat_storage_set(HS_KEY, e, (int)n);
    free(e);
    return ok;
}

/* 2-player record {char name1[33], name2[33] (sorted), u32 wins1 +0x44, wins2 +0x48, draws +0x4c}: the
   pair's counters are bumped, a new pair is inserted in name order. */
static bool record_2p(const GameResult *r)
{
    const char *n1 = getenv("OPENRF_P1"), *n2 = getenv("OPENRF_P2");   /* DAT_0048bb20 / DAT_0048bb49 */
    if (!n1 || !*n1) n1 = getenv("USER");
    if (!n2 || !*n2) n2 = "Player 2";
    if (!n1 || !*n1) return false;
    uint8_t rec[HS_REC2] = { 0 };
    int w = r->winner;
    if (strcmp(n1, n2) < 0) { strncpy((char *)rec, n1, 0x20); strncpy((char *)rec + 0x21, n2, 0x20); }
    else {
        strncpy((char *)rec, n2, 0x20); strncpy((char *)rec + 0x21, n1, 0x20);
        if (w == 0 || w == 1) w ^= 1;
    }
    size_t n;
    uint8_t *b = hs_load(&n);
    uint32_t n1c = rd32(b + 0xc), n2c = rd32(b + 0x14);
    size_t base = HS_HDR + (size_t)n1c * HS_REC1, pos = base + (size_t)n2c * HS_REC2;
    uint8_t *hit = NULL;
    for (uint32_t i = 0; i < n2c; i++) {
        uint8_t *q = b + base + (size_t)i * HS_REC2;
        int c = strncmp((const char *)q, (const char *)rec, 33);
        if (c < 0) continue;
        if (c == 0 && strncmp((const char *)q + 0x21, (const char *)rec + 0x21, 33) == 0) { hit = q; break; }
        pos = base + (size_t)i * HS_REC2;
        break;
    }
    int field = w == 0 ? 0x44 : w == 1 ? 0x48 : 0x4c;
    bool ok;
    if (hit) {
        wr32(hit + field, rd32(hit + field) + 1);
        ok = hs_save(b, n);
    } else {
        wr32(rec + field, 1);
        uint8_t *nb = malloc(n + HS_REC2);
        memcpy(nb, b, pos);
        memcpy(nb + pos, rec, HS_REC2);
        memcpy(nb + pos + HS_REC2, b + pos, n - pos);
        wr32(nb + 0x14, n2c + 1);
        ok = hs_save(nb, n + HS_REC2);
        free(nb);
    }
    free(b);
    return ok;
}

int highscore_list2(HighScore2P *out, int max)
{
    size_t n;
    uint8_t *b = hs_load(&n);
    int n1 = (int)rd32(b + 0xc), n2 = (int)rd32(b + 0x14);
    for (int i = 0; i < n2 && i < max; i++) {
        const uint8_t *q = b + HS_HDR + (size_t)n1 * HS_REC1 + (size_t)i * HS_REC2;
        memcpy(out[i].name[0], q, 33); out[i].name[0][32] = 0;
        memcpy(out[i].name[1], q + 0x21, 33); out[i].name[1][32] = 0;
        out[i].wins[0] = rd32(q + 0x44); out[i].wins[1] = rd32(q + 0x48); out[i].draws = rd32(q + 0x4c);
    }
    free(b);
    return n2;
}

bool highscore_record(const GameResult *r)
{
    if (r->nplayers == 2) return record_2p(r);                     /* 2P: wins of either side and draws */
    if (r->nplayers != 1 || r->winner != 0) return false;          /* 1P: only wins are recorded */
    const char *user = getenv("USER");                             /* DAT_0048bb20: player name (WNetGetUser) */
    if (!user || !*user) return false;
    uint8_t rec[HS_REC1] = { 0 };
    const char *m = r->map_rel ? r->map_rel : "";
    const char *s = strrchr(m, '/');
    if (!s) s = strrchr(m, '\\');
    s = s ? s + 1 : m;
    size_t l = strlen(s);
    if (l > 4 && s[l - 4] == '.') l -= 4;
    if (l > 0x20) l = 0x20;
    bool stock = strstr(m, "1PLAYER") || strstr(m, "1Player");
    uint16_t level = stock ? (uint16_t)(r->level + 1) : 0x7f;      /* custom maps: 0x7f */
    rec[0] = (uint8_t)level; rec[1] = (uint8_t)(level >> 8);
    memcpy(rec + 2, s, l);
    strncpy((char *)rec + 0x23, user, 0x20);
    wr32(rec + 0x44, r->time_ms);
    size_t n;
    uint8_t *b = hs_load(&n);
    uint32_t n1 = rd32(b + 0xc);
    size_t pos = HS_HDR + (size_t)n1 * HS_REC1;
    bool replaced = false;
    for (uint32_t i = 0; i < n1; i++) {                            /* sorted by level; best time per map */
        uint8_t *q = b + HS_HDR + (size_t)i * HS_REC1;
        uint16_t ql = (uint16_t)(q[0] | q[1] << 8);
        if (level > ql) continue;
        if (ql != level) { if (!replaced) pos = HS_HDR + (size_t)i * HS_REC1; break; }
        if (strncmp((const char *)q + 2, (const char *)rec + 2, 33) == 0) {
            if (rd32(q + 0x44) <= r->time_ms) { free(b); return false; }
            memcpy(q, rec, HS_REC1);
            replaced = true;
        }
    }
    bool ok;
    if (replaced) ok = hs_save(b, n);
    else {
        uint8_t *nb = malloc(n + HS_REC1);
        memcpy(nb, b, pos);
        memcpy(nb + pos, rec, HS_REC1);
        memcpy(nb + pos + HS_REC1, b + pos, n - pos);
        wr32(nb + 0xc, n1 + 1);
        ok = hs_save(nb, n + HS_REC1);
        free(nb);
    }
    free(b);
    return ok;
}

int highscore_list(HighScore1P *out, int max)
{
    size_t n;
    uint8_t *b = hs_load(&n);
    int n1 = (int)rd32(b + 0xc);
    for (int i = 0; i < n1 && i < max; i++) {
        const uint8_t *q = b + HS_HDR + (size_t)i * HS_REC1;
        out[i].level = q[0] | q[1] << 8;
        memcpy(out[i].map, q + 2, 33); out[i].map[32] = 0;
        memcpy(out[i].player, q + 0x23, 33); out[i].player[32] = 0;
        out[i].time_ms = rd32(q + 0x44);
    }
    free(b);
    return n1;
}
