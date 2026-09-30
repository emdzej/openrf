/* .STM player. Layout documented in docs/stm.md: a pre-interleaved AVI dump
   (Cinepak 320x240 @ 15 fps + 22050 Hz PCM) in fixed-size blocks.
   Like the original (FUN_004256f0) video is slaved to the audio clock. */
#include "movie.h"
#include "audio.h"
#include "cinepak.h"
#include "platform.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

typedef struct { const uint8_t *data; uint32_t size; bool key; } Frame;

struct Movie {
    uint8_t *file;
    Frame *frames;
    uint32_t nframes;
    double fps;
    int w, h;
    Cinepak cp;
    bool have_ov, fading;
    MovieOverlay ov;
    uint32_t *out;               /* composed output (overlay playback) */
    int shown;
    uint64_t fade_t0;
};

/* Pixel-double the frame into out and blit the overlay (key 0), scaled by fade (0..256). */
static void compose(uint32_t *out, const uint32_t *fr, int w, int h, const MovieOverlay *ov, int fade)
{
    int sx = ov->out_w / w, sy = ov->out_h / h;
    if (sx < 1) sx = 1;
    if (sy < 1) sy = 1;
    for (int y = 0; y < ov->out_h; y++)
        for (int x = 0; x < ov->out_w; x++) {
            int fx = x / sx, fy = y / sy;
            out[y * ov->out_w + x] = (fx < w && fy < h) ? fr[fy * w + fx] : 0;
        }
    for (int j = 0; j < ov->h; j++) {
        int y = ov->y + j;
        if (y < 0 || y >= ov->out_h) continue;
        for (int i = 0; i < ov->w; i++) {
            int x = ov->x + i;
            uint8_t c = ov->pixels[j * ov->w + i];
            if (!c || x < 0 || x >= ov->out_w) continue;
            RGB p = ov->palette[c];
            out[y * ov->out_w + x] = (uint32_t)p.r << 16 | (uint32_t)p.g << 8 | p.b;
        }
    }
    if (fade < 256)
        for (int k = 0; k < ov->out_w * ov->out_h; k++) {
            uint32_t c = out[k];
            out[k] = ((((c >> 16) & 255) * (uint32_t)fade >> 8) << 16) | ((((c >> 8) & 255) * (uint32_t)fade >> 8) << 8) |
                     ((c & 255) * (uint32_t)fade >> 8);
        }
}

Movie *movie_open(const char *rel, const MovieOverlay *ov)
{
    size_t fsize;
    uint8_t *f = vfs_read_all(rel, &fsize);
    if (!f || fsize < 0x1000) { free(f); fprintf(stderr, "movie %s missing\n", rel); return NULL; }
    Movie *m = calloc(1, sizeof *m);
    m->file = f;
    uint32_t data_start = rd32(f), chunk_size = rd32(f + 4), nchunks = rd32(f + 8);
    uint32_t vscale = rd32(f + 0xCC), vrate = rd32(f + 0xD0);
    m->nframes = rd32(f + 0xD8);
    int channels = rd16(f + 0x98 + 2), freq = (int)rd32(f + 0x98 + 4), bits = rd16(f + 0x98 + 14);
    m->w = (int)rd32(f + 0x144 + 4);
    m->h = (int)rd32(f + 0x144 + 8);
    m->fps = vscale ? (double)vrate / vscale : 15.0;

    m->frames = calloc(m->nframes ? m->nframes : 1, sizeof *m->frames);
    audio_pcm_open(freq, channels, bits);
    for (uint32_t c = 0; c < nchunks; c++) {
        size_t base = data_start + (size_t)c * chunk_size;
        if (base + chunk_size > fsize) break;
        const uint8_t *ch = f + base;
        uint32_t aoff = rd32(ch + 8), acount = rd32(ch + 12), voff = rd32(ch + 16);
        for (uint32_t r = voff; r && r + 8 <= chunk_size; ) {
            const uint8_t *rec = ch + r;
            uint16_t num = rd16(rec + 6);
            const uint8_t *fr = rec + 8;
            uint32_t len = (uint32_t)fr[1] << 16 | fr[2] << 8 | fr[3];
            if (num < m->nframes && r + 8 + len <= chunk_size) m->frames[num] = (Frame){ fr, len, rec[4] & 1 };
            r = rd32(rec);
        }
        if (aoff && aoff + acount * 4 <= chunk_size) audio_pcm_push(ch + aoff, (int)(acount * 4));
    }
    cinepak_init(&m->cp, m->w, m->h);
    if (ov) {
        m->have_ov = true;
        m->ov = *ov;
        m->out = malloc(sizeof(uint32_t) * (size_t)ov->out_w * (size_t)ov->out_h);
    }
    m->shown = -1;
    return m;
}

Step movie_step(Movie *m)
{
    const MovieOverlay *ov = m->have_ov ? &m->ov : NULL;
    if (m->fading) {                                   /* fade the last picture out */
        uint64_t t = plat_ticks_ms() - m->fade_t0;
        if (t >= (uint64_t)ov->fade_out_ms) return STEP_DONE;
        compose(m->out, m->cp.frame, m->w, m->h, ov, (int)(256 - t * 256 / (uint64_t)ov->fade_out_ms));
        plat_present_rgb(m->out, ov->out_w, ov->out_h);
        return STEP_FRAME;
    }
    bool end = plat_any_key_pressed();
    int target = 0;
    if (!end) {
        target = (int)(audio_pcm_played_seconds() * m->fps);
        if (target >= (int)m->nframes) { if (audio_pcm_drained()) end = true; target = (int)m->nframes - 1; }
    }
    if (end) {
        if (!(ov && m->out && ov->fade_out_ms > 0)) return STEP_DONE;
        m->fading = true;
        m->fade_t0 = plat_ticks_ms();
        return STEP_AGAIN;
    }
    /* Decode every frame up to the target (inter frames depend on predecessors);
       when far behind, jump to the latest keyframe like the original does. */
    if (target - m->shown > 4)
        for (int k = target; k > m->shown + 1; k--)
            if (m->frames[k].key) { m->shown = k - 1; break; }
    while (m->shown < target) {
        m->shown++;
        if (m->frames[m->shown].data) cinepak_decode(&m->cp, m->frames[m->shown].data, m->frames[m->shown].size);
    }
    if (ov && m->out) { compose(m->out, m->cp.frame, m->w, m->h, ov, 256); plat_present_rgb(m->out, ov->out_w, ov->out_h); }
    else plat_present_rgb(m->cp.frame, m->w, m->h);
    return STEP_FRAME;
}

void movie_close(Movie *m)
{
    if (!m) return;
    free(m->out);
    cinepak_free(&m->cp);
    audio_pcm_close();
    free(m->frames);
    free(m->file);
    free(m);
}
