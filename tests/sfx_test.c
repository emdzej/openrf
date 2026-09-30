/* Headless test of the sound-effect mixer (src/sfx.c): no audio device, events are played through
   the command queue and rendered with sfx_render. Checks instance/voice limits, priority order and
   budget, distance attenuation, owner tracking, looping and the engine pitch callbacks, then
   writes a scripted sequence to out/sfx/test.wav and prints its levels.
   Usage: sfx_test [data_root (default: cd)] [out.wav] */
#include "sfx.h"
#include "assets.h"
#include "game/object.h"
#include "game/vehicle.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "exe.h"
#include "vfs_host.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t tick;
static int32_t lis[3], lis_gain = 0x10000;

static uint32_t ev(const char *name)
{
    for (int i = 0; i < SFX_NEVENTS; i++) if (!strcmp(sfx_events[i].name, name)) return sfx_events[i].va;
    printf("no event %s\n", name);
    exit(2);
}

static void reset(void)
{
    sfx_init();
    tick = 1000;
    sfx_set_time(tick);
    sfx_cmd(SFX_LISTENER1, 0, lis, false);
    sfx_cmd(SFX_LISTENER2, 0, lis, false);
    sfx_cmd(SFX_LISTENER_GAIN, 0, &lis_gain, false);
    sfx_cmd(SFX_LISTENER_GAIN, 1, &lis_gain, true);
}

/* One 16 ms game frame of audio (705.6 samples at 44.1 kHz). */
static int16_t *wav;
static size_t wav_frames, wav_cap;
static double frac;
static void frame(bool record)
{
    tick++;
    sfx_set_time(tick);
    sfx_frame();
    frac += 44100.0 * 0.016;
    int n = (int)frac;
    frac -= n;
    int16_t buf[1024 * 2];
    sfx_render(buf, n);
    if (!record) return;
    if (wav_frames + (size_t)n > wav_cap) { wav_cap = (wav_cap + (size_t)n) * 2; wav = realloc(wav, wav_cap * 4); }
    memcpy(wav + wav_frames * 2, buf, (size_t)n * 4);
    wav_frames += (size_t)n;
}

static int nvoices_bound(void)
{
    SfxVoiceInfo vi[SFX_NVOICES];
    int n = sfx_debug_voices(vi, SFX_NVOICES), k = 0;
    for (int i = 0; i < n; i++) k += vi[i].event != NULL;
    return k;
}

static void test_limits(void)
{
    reset();
    int ok = 0;
    for (int i = 0; i < 20; i++) ok += sfx_play(ev("Button"), NULL, NULL) != NULL;
    CHECK(ok == SFX_NINST, "15 instances expected, got %d", ok);
    CHECK(sfx_debug_active_instances() == SFX_NINST, "active %d", sfx_debug_active_instances());
    frame(false);
    CHECK(nvoices_bound() == SFX_NINST, "voices bound %d", nvoices_bound());
    /* Button = 0.19 s: after 0.5 s all one-shots are reaped and freed. */
    for (int i = 0; i < 32; i++) frame(false);
    CHECK(sfx_debug_active_instances() == 0, "one-shots not freed: %d", sfx_debug_active_instances());
    CHECK(nvoices_bound() == 0, "voices still bound %d", nvoices_bound());
}

static void test_priority_budget(void)
{
    reset();
    /* Laugh: prio 200, gain 0x2ccc. Four copies: 0x2ccc, 0x2ccc, 0x7fff - 2 * 0x2ccc, 0. */
    SfxInst *a[4];
    for (int i = 0; i < 4; i++) a[i] = sfx_play(ev("Laugh"), NULL, NULL);
    SfxInst *lo = sfx_play(ev("Ding"), NULL, NULL);      /* prio 120: last in the list */
    frame(false);
    SfxVoiceInfo vi[SFX_NVOICES];
    int n = sfx_debug_voices(vi, SFX_NVOICES), k = 0;
    int32_t sum = 0, lv[8];
    for (int i = 0; i < n; i++) if (vi[i].event) { if (k < 8) lv[k] = vi[i].l; k++; sum += vi[i].l; }
    CHECK(k == 5, "bound %d", k);
    CHECK(sum <= 0x7fff, "budget exceeded %x", sum);
    CHECK(lv[0] == 0x2ccc && lv[1] == 0x2ccc && lv[2] == 0x7fff - 2 * 0x2ccc && lv[3] == 0 && lv[4] == 0,
          "levels %x %x %x %x %x", lv[0], lv[1], lv[2], lv[3], lv[4]);
    SfxVoiceInfo d;
    CHECK(sfx_debug_instance(lo, &d) && d.prio == 120, "Ding prio");
    /* volume = min(0, max/3 - 2200) (hundredths of dB): 0x2ccc saturates at 0 dB, 0x7fff-2*0x2ccc
       -> -472, starved voices -2200 (-22 dB, not silent). */
    CHECK(sfx_debug_instance(a[0], &d) && d.vol == 0, "vol %d", d.vol);
    CHECK(sfx_debug_instance(a[2], &d) && d.l == 0x7fff - 2 * 0x2ccc && d.vol == 0, "vol %d", d.vol);
    CHECK(sfx_debug_instance(lo, &d) && d.vol == -2200, "starved vol %d", d.vol);
    /* Priority decay: Boom 199 -> 150 after 60 ticks. */
    SfxInst *b = sfx_play(ev("Boom"), NULL, NULL);
    CHECK(sfx_debug_instance(b, &d) && d.prio == 199, "boom prio %d", d.prio);
    for (int i = 0; i < 61; i++) frame(false);
    CHECK(sfx_debug_instance(b, &d) && d.prio == 150, "boom decayed prio %d", d.prio);
}

static void test_attenuation(void)
{
    reset();
    static Obj o[5];
    const int dist[5] = { 30, 100, 300, 423, 500 };
    const int32_t want[5] = { 0x10000, (0x1a8 - 100) * 0x10000 / 0x180, (0x1a8 - 300) * 0x10000 / 0x180, 0x10000 / 0x180, 0 };
    SfxInst *in[5];
    for (int i = 0; i < 5; i++) { o[i].pos[0] = dist[i] << 16; in[i] = sfx_play(ev("Cannon"), &o[i], NULL); }
    frame(false);
    for (int i = 0; i < 5; i++) {
        SfxVoiceInfo d;
        bool act = sfx_debug_instance(in[i], &d);
        /* a one-shot that gets no voice at the next assign (gain 0 here) is dropped */
        CHECK(act == (i < 4), "cannon %d active=%d", i, act);
        if (act) CHECK(d.gl == want[i] && d.gr == want[i], "dist %d: gain %x/%x want %x", dist[i], d.gl, d.gr, want[i]);
        int32_t lvl = (int32_t)(((int64_t)want[i] * 0x1555) >> 16), vol = lvl / 3 - 2200;
        if (act) CHECK(d.l == lvl && d.vol == (vol > 0 ? 0 : vol), "dist %d: level %x vol %d", dist[i], d.l, d.vol);
        if (act) printf("dist %3d: gain %5x level %4x vol %5d (%.1f dB)\n", dist[i], d.gl, d.l, d.vol, d.vol / 100.0);
    }
    /* Listener gain 0 (bunker select) silences owned sounds, not unowned ones. */
    int32_t zero = 0;
    sfx_cmd(SFX_LISTENER_GAIN, 0, &zero, false);
    sfx_cmd(SFX_LISTENER_GAIN, 1, &zero, true);
    SfxInst *u = sfx_play(ev("GClick"), NULL, NULL);
    frame(false);
    SfxVoiceInfo d;
    CHECK(sfx_debug_instance(in[0], &d) && d.gl == 0, "owned sound with gain 0: %x", d.gl);
    CHECK(sfx_debug_instance(u, &d) && d.gl == 0x10000 && d.gr == 0x10000, "unowned %x/%x", d.gl, d.gr);
    /* Two listeners 20 units apart: a source on the left is louder on the left, pan < 0. */
    reset();
    static int32_t l1[3], l2[3];
    sfx_cmd(SFX_LISTENER1, -(10 << 16), l1, false);
    sfx_cmd(SFX_LISTENER2, 10 << 16, l2, true);
    static Obj left;
    left.pos[0] = -(200 << 16);
    SfxInst *s = sfx_play(ev("ExplLow"), &left, NULL);
    frame(false);
    CHECK(sfx_debug_instance(s, &d) && d.gl > d.gr && d.pan < 0, "stereo: gains %x/%x pan %d", d.gl, d.gr, d.pan);
    printf("stereo: source 200 left -> gains %x/%x levels %x/%x vol %d pan %d\n", d.gl, d.gr, d.l, d.r, d.vol, d.pan);
}

static VehicleDef jeep_def = { .type = VT_JEEP, .snd_91 = 0x1b92, .snd_92 = 0x3a0c };
static VehicleDef heli_def = { .type = VT_HELI, .snd_91 = 0x129, .snd_92 = 0x116a };
static VehState jeep_st = { .def = &jeep_def }, heli_st = { .def = &heli_def };
static Obj jeep = { .p60 = &jeep_st }, heli = { .p60 = &heli_st };

static void test_owner_loop(void)
{
    reset();
    jeep.pos[0] = 0; jeep.speed = 0;
    SfxInst *e = sfx_play(ev("jeep"), &jeep, NULL);
    frame(false);
    SfxVoiceInfo d;
    CHECK(sfx_debug_instance(e, &d) && d.playing && (d.inst_flags & 1), "jeep idle not looping");
    CHECK(d.freq == 11025, "voice starts at the native rate before the first callback: %d", d.freq);
    jeep.speed = 0x8000;
    sfx_hook(SFX_OWNER_CB, 0, &jeep);
    frame(false);
    CHECK(sfx_debug_instance(e, &d) && d.freq == 0x1b92 + (0x3a0c - 0x1b92) / 2, "engine pitch %d", d.freq);
    for (int i = 0; i < 125; i++) frame(false);            /* 2 s > 0.6 s sample */
    CHECK(sfx_debug_instance(e, &d) && d.playing, "loop stopped");
    /* Non-looping owned sound keeps playing at the owner's last position after detach. */
    SfxInst *c = sfx_play(ev("ExplLarge"), &jeep, NULL);
    jeep.pos[0] = 300 << 16;
    frame(false);
    sfx_hook(-1, 0, &jeep);                                /* owner deleted */
    jeep.pos[0] = 0;                                       /* slot reused somewhere else */
    frame(false);
    CHECK(!sfx_debug_instance(e, &d), "looping sound survived its owner");
    CHECK(sfx_debug_instance(c, &d) && (d.inst_flags & 0x80) && d.gl == (0x1a8 - 300) * 0x10000 / 0x180,
          "detached one-shot: flags %x gain %x", d.inst_flags, d.gl);
    /* Rotor: 2621 + 2621 * speed. */
    heli_st.boat_target = 0x20000;
    SfxInst *h = sfx_play(ev("heli"), &heli, NULL);
    sfx_hook(SFX_OWNER_CB, 0, &heli);
    frame(false);
    CHECK(sfx_debug_instance(h, &d) && d.freq == 0xa3d * 3, "rotor pitch %d", d.freq);
    /* Stop all. */
    sfx_cmd(SFX_STOP_ALL, 1, NULL, true);
    CHECK(sfx_debug_active_instances() == 0 && nvoices_bound() == 0, "stop all");
}

static void write_wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); fails++; return; }
    uint32_t data = (uint32_t)wav_frames * 4;
    uint8_t h[44] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x44\xac\0\0\x10\xb1\x02\0\x04\0\x10\0data";
    uint32_t riff = 36 + data;
    memcpy(h + 4, &riff, 4);
    memcpy(h + 40, &data, 4);
    fwrite(h, 1, 44, f);
    fwrite(wav, 4, wav_frames, f);
    fclose(f);
}

static void scripted(const char *out)
{
    reset();
    jeep.pos[0] = 0; jeep.speed = 0;
    heli.pos[0] = 60 << 16; heli_st.boat_target = 0;
    /* 0.0 s JeepStart, 1.0 s idle loop with a speed sweep 0 -> 1 over 3 s, then back to idle. */
    sfx_hook(SFX_PLAY, ev("JeepStart"), &jeep);
    SfxInst *idle = NULL;
    SfxInst *rotor = NULL;
    int32_t fmin = 1 << 30, fmax = 0, rmin = 1 << 30, rmax = 0;
    for (int t = 0; t < 625; t++) {                        /* 10 s */
        if (t == 62) idle = sfx_play(ev("jeep"), &jeep, NULL);
        if (t >= 62 && t < 250) jeep.speed = (t - 62) * 0x10000 / 188;
        if (t == 250) jeep.speed = 0;
        if (t == 300) sfx_detach_owner(&jeep);           /* jeep gone: idle loop stops */
        if (t == 300) { sfx_hook(SFX_PLAY, ev("Servo"), &heli); rotor = sfx_play(ev("heli"), &heli, NULL); }
        if (t >= 300 && t < 480) heli_st.boat_target = (t - 300) * 0x40000 / 180;
        if (t == 480) sfx_detach_owner(&heli);
        if (t == 490) sfx_hook(SFX_PLAY, ev("ExplLarge"), NULL);
        if (t == 500) { static Obj far; far.pos[0] = 200 << 16; sfx_hook(SFX_PLAY, ev("Boom"), &far); }
        if (idle) sfx_hook(SFX_OWNER_CB, 0, &jeep);
        if (rotor && t < 480) sfx_hook(SFX_OWNER_CB, 0, &heli);
        frame(true);
        SfxVoiceInfo d;
        if (idle && t < 300 && sfx_debug_instance(idle, &d) && d.voice >= 0 && t > 64) {
            if (d.freq < fmin) fmin = d.freq;
            if (d.freq > fmax) fmax = d.freq;
        }
        if (rotor && t > 302 && t < 480 && sfx_debug_instance(rotor, &d) && d.voice >= 0) {
            if (d.freq < rmin) rmin = d.freq;
            if (d.freq > rmax) rmax = d.freq;
        }
    }
    printf("jeep idle pitch %d..%d Hz, rotor pitch %d..%d Hz\n", fmin, fmax, rmin, rmax);
    CHECK(fmin == 0x1b92 && fmax > 14700 && fmax <= 0x3a0c, "jeep sweep %d..%d", fmin, fmax);
    CHECK(rmin < 3000 && rmax > 12900 && rmax <= 0xa3d * 5, "rotor sweep %d..%d", rmin, rmax);
    write_wav(out);
    /* Levels per 0.5 s window. */
    double dur = (double)wav_frames / 44100;
    printf("%s: %.2f s, %zu frames\n", out, dur, wav_frames);
    CHECK(fabs(dur - 10.0) < 0.05, "duration %.3f", dur);
    int clip = 0;
    double rms_w[20] = { 0 };
    for (size_t i = 0; i < wav_frames; i++) {
        int w = (int)(i / 22050);
        for (int c = 0; c < 2; c++) {
            int s = wav[i * 2 + c];
            if (s == 32767 || s == -32768) clip++;
            if (w < 20) rms_w[w] += (double)s * s;
        }
    }
    printf("RMS dBFS per 0.5 s:");
    for (int w = 0; w < 20; w++) {
        rms_w[w] = sqrt(rms_w[w] / (22050 * 2)) / 32768;
        printf(" %.0f", rms_w[w] > 0 ? 20 * log10(rms_w[w]) : -99);
    }
    printf("\nclipped samples: %d\n", clip);
    CHECK(rms_w[0] > 0.003, "JeepStart silent");
    CHECK(rms_w[4] > 0.003 && rms_w[8] > 0.003, "jeep idle silent");
    CHECK(rms_w[11] > 0.003 && rms_w[14] > 0.003, "heli silent");
    CHECK(rms_w[16] > 0.003 && rms_w[18] > 0.003, "explosion silent");
    CHECK(clip < 100, "clipping");
}

int main(int argc, char **argv)
{
    if (argc > 1) vfs_mount_path(argv[1]); else vfs_mount_default();
    if (!exe_load()) { fprintf(stderr, "%s\n", exe_error()); return 1; }   /* tables from RFIRE.BIN */
    const char *out = argc > 2 ? argv[2] : "out/sfx/test.wav";
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", out);
    for (char *p = dir + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(dir, 0755); *p = '/'; }
    if (!sfx_init()) { printf("no samples\n"); return 1; }
    test_limits();
    test_priority_budget();
    test_attenuation();
    test_owner_loop();
    scripted(out);
    sfx_shutdown();
    printf(fails ? "%d FAILURES\n" : "all sfx checks passed\n", fails);
    return fails != 0;
}
