/* Portable mixer (audio.h). Replaces the SDL backend's separate audio streams (music, movie PCM, SFX)
   with one pull function, so any host that can play 44100 Hz stereo can run the game's audio.

   Resampling: sources that are not 44100 Hz (the 22050 Hz movie soundtracks) are linearly interpolated;
   SDL used its own (band-limited) resampler, so movie audio differs slightly in the top octave. Music
   (44100 Hz S16 stereo) and the SFX mixer pass through unchanged. */
#include "audio.h"
#include "platform.h"
#include "vfs.h"
#include <stdlib.h>
#include <string.h>

void audio_lock(void) { plat_audio_lock(); }
void audio_unlock(void) { plat_audio_unlock(); }

/* ------------------------------------------------------------------ resampler */

typedef bool (*FetchFn)(void *src, float fr[2]);
typedef struct {
    uint32_t freq, acc;          /* source rate; position between prev and next in 1/AUDIO_RATE units */
    float prev[2], next[2];
    bool primed, eof;            /* eof: next is a copy of the last frame, nothing after it */
} Resamp;

static void fetch_or_hold(Resamp *r, FetchFn fetch, void *src)
{
    if (!fetch(src, r->next)) { r->next[0] = r->prev[0]; r->next[1] = r->prev[1]; r->eof = true; }
}

/* One output frame; false once the source has ended. */
static bool resamp_frame(Resamp *r, FetchFn fetch, void *src, float out[2])
{
    if (!r->primed) {
        if (!fetch(src, r->prev)) return false;
        r->eof = false;
        fetch_or_hold(r, fetch, src);
        r->primed = true;
        r->acc = 0;
    }
    while (r->acc >= AUDIO_RATE) {
        r->acc -= AUDIO_RATE;
        if (r->eof) return false;
        r->prev[0] = r->next[0]; r->prev[1] = r->next[1];
        fetch_or_hold(r, fetch, src);
    }
    float t = (float)r->acc / AUDIO_RATE;
    out[0] = r->prev[0] + (r->next[0] - r->prev[0]) * t;
    out[1] = r->prev[1] + (r->next[1] - r->prev[1]) * t;
    r->acc += r->freq;
    return true;
}

static void decode_frame(const uint8_t *p, int channels, int bits, float fr[2])
{
    for (int c = 0; c < 2; c++) {
        const uint8_t *s = p + (channels > 1 ? c : 0) * (bits / 8);
        fr[c] = bits == 8 ? (float)(s[0] - 128) / 128.0f : (float)(int16_t)(s[0] | s[1] << 8) / 32768.0f;
    }
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* ------------------------------------------------------------------ music */

/* Stream a byte range of a WAV's data chunk on demand (SCORE.WAV is 223 MB). Ranges can be changed while
   playing to continue seamlessly (MusTrans1_ContinueSeamless). */
typedef struct {
    VfsFile *f;
    uint32_t data_off, data_len;
    uint32_t pos, end, loop_start;
    bool loop, ended;
    int channels, bits, frame_bytes;
    float vol;
    Resamp rs;
    uint8_t buf[16384];          /* file bytes [buf_start, buf_start + buf_len) of the data chunk */
    uint32_t buf_start, buf_len;
} Music;
static Music mus;

static bool music_fetch(void *u, float fr[2])
{
    Music *m = u;
    uint32_t fb = (uint32_t)m->frame_bytes;
    if (m->pos >= m->end || m->end - m->pos < fb) {
        if (!m->loop) return false;
        m->pos = m->loop_start;
        if (m->pos >= m->end || m->end - m->pos < fb) return false;
    }
    if (m->pos < m->buf_start || m->pos + fb > m->buf_start + m->buf_len) {
        uint32_t n = sizeof m->buf;
        if (n > m->data_len - m->pos) n = m->data_len - m->pos;
        int64_t got = vfs_read_at(m->f, (uint64_t)m->data_off + m->pos, m->buf, n);
        if (got < (int64_t)fb) { m->buf_len = 0; return false; }
        m->buf_start = m->pos;
        m->buf_len = (uint32_t)got;
    }
    decode_frame(m->buf + (m->pos - m->buf_start), m->channels, m->bits, fr);
    m->pos += fb;
    return true;
}

void audio_music_stop(void)
{
    audio_lock();
    VfsFile *f = mus.f;
    mus.f = NULL;
    audio_unlock();
    vfs_close(f);
}

bool audio_music_play(const char *rel, uint32_t start, uint32_t end, uint32_t loop_start, bool loop)
{
    audio_music_stop();
    VfsFile *f = vfs_open(rel);
    if (!f) return false;
    uint8_t hdr[12];
    if (vfs_read_at(f, 0, hdr, 12) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) { vfs_close(f); return false; }
    int channels = 0, bits = 0;
    uint32_t freq = 0, data_off = 0, data_len = 0;
    for (uint64_t p = 12;;) {
        uint8_t ch[8];
        if (vfs_read_at(f, p, ch, 8) != 8) break;
        uint32_t sz = rd32(ch + 4);
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fmt[16];
            if (vfs_read_at(f, p + 8, fmt, 16) != 16) break;
            channels = fmt[2] | fmt[3] << 8;
            freq = rd32(fmt + 4);
            bits = (fmt[14] | fmt[15] << 8) == 8 ? 8 : 16;
        } else if (!memcmp(ch, "data", 4)) {
            data_off = (uint32_t)(p + 8);
            data_len = sz;
            break;
        }
        p += 8 + (uint64_t)sz + (sz & 1);
    }
    if (!data_off || !freq || channels < 1 || channels > 2) { vfs_close(f); return false; }
    if ((uint64_t)data_off + data_len > vfs_file_size(f)) data_len = (uint32_t)(vfs_file_size(f) - data_off);
    audio_lock();
    mus = (Music){ .f = f, .data_off = data_off, .data_len = data_len, .channels = channels, .bits = bits,
                   .frame_bytes = channels * bits / 8, .vol = 1, .rs = { .freq = freq } };
    audio_unlock();
    audio_music_set_range(start, end, loop_start, loop);
    return true;
}

void audio_music_set_range(uint32_t pos, uint32_t end, uint32_t loop_start, bool loop)
{
    audio_lock();
    if (mus.f) {
        if (end == 0 || end > mus.data_len) end = mus.data_len;
        if (pos != UINT32_MAX) { mus.pos = pos > end ? end : pos & ~3u; mus.rs.primed = false; }
        if (mus.ended) mus.rs.primed = false;
        mus.end = end;
        mus.loop_start = loop_start & ~3u;
        mus.loop = loop;
        mus.ended = false;
    }
    audio_unlock();
}

bool audio_music_ended(void)
{
    audio_lock();
    bool e = !mus.f || mus.ended;
    audio_unlock();
    return e;
}

void audio_music_set_volume(float v)
{
    audio_lock();
    mus.vol = v;
    audio_unlock();
}

/* ------------------------------------------------------------------ movie PCM */

typedef struct {
    bool open, drained;
    int channels, bits, frame_bytes;
    uint8_t *data;
    size_t len, cap;             /* bytes pushed */
    size_t rd;                   /* next source frame */
    uint64_t out_frames;         /* output frames rendered (the playback clock) */
    Resamp rs;
} Pcm;
static Pcm pcm;

static bool pcm_fetch(void *u, float fr[2])
{
    Pcm *p = u;
    if ((p->rd + 1) * (size_t)p->frame_bytes > p->len) return false;
    decode_frame(p->data + p->rd * (size_t)p->frame_bytes, p->channels, p->bits, fr);
    p->rd++;
    return true;
}

bool audio_pcm_open(int freq, int channels, int bits)
{
    audio_pcm_close();
    if (freq <= 0 || channels < 1 || channels > 2 || (bits != 8 && bits != 16)) return false;
    audio_lock();
    pcm = (Pcm){ .open = true, .channels = channels, .bits = bits, .frame_bytes = channels * bits / 8,
                 .rs = { .freq = (uint32_t)freq } };
    audio_unlock();
    return true;
}

void audio_pcm_push(const void *data, int bytes)
{
    if (bytes <= 0) return;
    audio_lock();
    if (pcm.open) {
        if (pcm.len + (size_t)bytes > pcm.cap) {
            size_t c = pcm.cap ? pcm.cap : 1 << 20;
            while (c < pcm.len + (size_t)bytes) c *= 2;
            uint8_t *nd = realloc(pcm.data, c);
            if (nd) { pcm.data = nd; pcm.cap = c; }
        }
        if (pcm.len + (size_t)bytes <= pcm.cap) {
            memcpy(pcm.data + pcm.len, data, (size_t)bytes);
            pcm.len += (size_t)bytes;
            if (pcm.drained) { pcm.drained = false; pcm.rs.primed = false; }
        }
    }
    audio_unlock();
}

double audio_pcm_played_seconds(void)
{
    audio_lock();
    double s = pcm.open ? (double)pcm.out_frames / AUDIO_RATE : 0;
    audio_unlock();
    return s;
}

bool audio_pcm_drained(void)
{
    audio_lock();
    bool d = !pcm.open || pcm.drained;
    audio_unlock();
    return d;
}

void audio_pcm_close(void)
{
    audio_lock();
    uint8_t *d = pcm.data;
    pcm = (Pcm){ 0 };
    audio_unlock();
    free(d);
}

/* ------------------------------------------------------------------ output */

static void (*sfx_mix)(int16_t *out, int frames);

void audio_set_sfx(void (*mix)(int16_t *out, int frames))
{
    audio_lock();
    sfx_mix = mix;
    audio_unlock();
}

void audio_render(float *out, int frames)
{
    enum { CHUNK = 512 };
    int16_t s16[CHUNK * 2];
    while (frames > 0) {
        int n = frames < CHUNK ? frames : CHUNK;
        if (sfx_mix) {
            sfx_mix(s16, n);
            for (int i = 0; i < n * 2; i++) out[i] = (float)s16[i] / 32768.0f;
        } else memset(out, 0, sizeof(float) * (size_t)n * 2);
        if (mus.f && !mus.ended)
            for (int i = 0; i < n; i++) {
                float fr[2];
                if (!resamp_frame(&mus.rs, music_fetch, &mus, fr)) { mus.ended = true; break; }
                out[i * 2] += fr[0] * mus.vol;
                out[i * 2 + 1] += fr[1] * mus.vol;
            }
        if (pcm.open && !pcm.drained)
            for (int i = 0; i < n; i++) {
                float fr[2];
                if (!resamp_frame(&pcm.rs, pcm_fetch, &pcm, fr)) { pcm.drained = true; break; }
                out[i * 2] += fr[0];
                out[i * 2 + 1] += fr[1];
                pcm.out_frames++;
            }
        for (int i = 0; i < n * 2; i++) out[i] = out[i] > 1.0f ? 1.0f : out[i] < -1.0f ? -1.0f : out[i];
        out += n * 2;
        frames -= n;
    }
}

int audio_frames_for_frame(uint32_t rate_num, uint32_t rate_den)
{
    static uint64_t rem;
    if (!rate_num) return 0;
    uint64_t total = (uint64_t)AUDIO_RATE * rate_den + rem;
    rem = total % rate_num;
    return (int)(total / rate_num);
}
