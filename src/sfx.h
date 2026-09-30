/* Sound effects: port of the original command-queue mixer (Snd_QueueCommand 0x42cb40, Snd_Service
   0x42cff0, Snd_InitMixer 0x419970 ... Snd_MixAndUpdateVoices 0x418e90) over an emulated set of
   DirectSound secondary buffers rendered by a software mixer. docs/architecture.md section 7.

   15 logical instances (priority-sorted), 20 voices, per-side budget 0x7fff, two listeners
   (L = listener 1, R = listener 2), distance gain clamp((0x1a8 - dist) / 0x180, 0, 1). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

enum { SFX_NSAMPLES = 37, SFX_NEVENTS = 48, SFX_NLIST = 44, SFX_NVOICES = 20, SFX_NINST = 15 };
#define SFX_WHOLE INT32_MIN          /* marker value 0x80000000: whole sample */
#define SFX_EVENT_BASE 0x43e960u     /* VA of sfx_events[0] */
#define SFX_EVENT_STRIDE 0x18u

/* Sample table 0x43e550 (0x1c per entry). flags 0x100 = looping sample (informational);
   loop markers {0x1f start, 0x20 end} (-1 = none). The retail code never reads the markers:
   looping instances loop the whole buffer (DSBPLAY_LOOPING). */
typedef struct { const char *path; uint32_t flags; int32_t loop_start, loop_end; } SfxSample;

/* Sound-event table 0x43e960 (0x18 per entry). */
enum { SFX_CB_NONE, SFX_CB_ENGINE /* SndCb_JeepEnginePitch 0x42b960 */, SFX_CB_ROTOR /* SndCb_HeliRotorPitch 0x42b9b0 */ };
enum {
    SFXF_LOOP_OWNER = 0x01,   /* loops; stopped when the owner goes away (else keeps its last position) */
    SFXF_MUTE_R     = 0x02,   /* unowned: right gain 0 */
    SFXF_MUTE_L     = 0x04,   /* unowned: left gain 0 */
    SFXF_OWN_GAINS  = 0x20,   /* per-listener source positions (Snd_CalcGainStereoOwner, Drone) */
    SFXF_MUTED      = 0x40,   /* unowned: start with both gains 0 */
};
typedef struct {
    uint32_t va;
    const char *name;
    uint8_t sample;               /* +0x04 index into sfx_samples */
    uint8_t prio, prio_decayed;   /* +0x08 / +0x09 */
    uint8_t decay;                /* +0x0a ticks (16 ms) until prio -> prio_decayed */
    uint8_t flags;                /* +0x0b SFXF_* */
    int32_t freq;                 /* +0x0c 0 native, <0 -(16.16 ratio), >0 Hz */
    int32_t gain;                 /* +0x10 16.16 mix share */
    uint8_t cb;                   /* +0x14 SFX_CB_* */
} SfxEvent;

/* sfx_tables.c, filled from RFIRE.BIN by exe_load() */
extern SfxSample sfx_samples[SFX_NSAMPLES];
extern SfxEvent sfx_events[SFX_NEVENTS];
extern uint8_t sfx_event_list[SFX_NLIST];   /* 0x43ede0: index -> sfx_events index */
extern uint8_t sfx_step_events[2];          /* 0x43f278: PreRaise, Raise */
#define SFX_THROW_LIST 19                         /* 0x43ee2c = sfx_event_list + 19 (3 variants) */

/* Commands of Snd_QueueCommand (table PTR_FUN_00443758). */
enum {
    SFX_SYNC, SFX_PLAY, SFX_REFRESH, SFX_STOP, SFX_UPDATE, SFX_OWNER_CB,
    SFX_LISTENER1, SFX_LISTENER2, SFX_LISTENER_GAIN, SFX_KILL, SFX_STOP_ALL
};

typedef struct Obj Obj;
typedef struct SfxInst SfxInst;

/* Loads all samples (plain WAV .SDT through the file layer, vfs.h) and resets the mixer. Returns false when no
   sample could be loaded (then everything is a no-op). */
bool sfx_init(void);
void sfx_shutdown(void);
/* Optional lock around state shared with the render callback (plat_sfx_lock/unlock). */
void sfx_set_lock_fns(void (*lock)(void), void (*unlock)(void));
/* Game clock in 16 ms ticks (GetGameClock(3)); read at each service. */
void sfx_set_time(uint32_t ticks16);

/* Snd_QueueCommand(cmd, a, b, flushNow). a: event VA (PLAY), handle (KILL), x offset (LISTENERn),
   listener index (LISTENER_GAIN), 0/1 (STOP_ALL), instance (STOP/UPDATE/OWNER_CB).
   b: owner Obj* (PLAY/STOP/UPDATE/OWNER_CB), const int32_t pos[3] (LISTENERn), const int32_t* gain
   (LISTENER_GAIN), instance (KILL). */
void sfx_cmd(int cmd, intptr_t a, const void *b, bool flush);
/* PLAY with flush; returns the instance (NULL if none was free) and its handle. */
SfxInst *sfx_play(uint32_t event_va, Obj *owner, uint32_t *handle);
/* game_hooks.sound adapter: cmd -1 = Snd_DetachAllFromOwner (immediate), otherwise queued. */
void sfx_hook(int cmd, uint32_t event_va, Obj *owner);
/* Snd_DetachAllFromOwner 0x419100: looping sounds stop, others keep the owner's last position. */
void sfx_detach_owner(Obj *owner);
/* SFXF_OWN_GAINS instances (Drone): source position per listener (NULL = silent on that side). */
void sfx_set_stereo_sources(SfxInst *in, const int32_t *pos_l, const int32_t *pos_r);
/* Bunker zoom-script sound step FUN_004042b0: plays step event `which` unowned, sets the voice
   frequency to ratio(16.16, negative) * -11000 >> 16 and kills it after `ticks` (0 = never). */
void sfx_play_step(int which, int32_t ratio, int32_t ticks);
/* End of frame: Snd_QueueCommand(2,0,0,1) then Snd_Service(0x67) (reap finished voices). */
void sfx_frame(void);

const SfxEvent *sfx_event(uint32_t va);   /* NULL if va is not an event record */

/* Software mixer: renders interleaved S16 stereo at 44100 Hz (the audio callback, or tests). */
void sfx_render(int16_t *out, int frames);

/* Introspection (tests, OPENRF_SFX_LOG=1). Voices in voice-list (priority) order. */
typedef struct {
    int voice;                 /* 0..19 */
    const char *event;         /* bound instance's event, NULL if none */
    uint32_t handle, inst_flags;
    int32_t prio, gl, gr;      /* instance priority and 16.16 listener gains */
    int32_t l, r;              /* budget-limited voice levels (0..0x7fff) */
    bool playing;              /* DirectSound buffer playing */
    int32_t vol, pan, freq;    /* hundredths of dB, -10000..10000, Hz */
} SfxVoiceInfo;
int sfx_debug_voices(SfxVoiceInfo *out, int max);
int sfx_debug_active_instances(void);
/* Instance state (voice fields are -1 / 0 when it has no voice). False if it is not active. */
bool sfx_debug_instance(const SfxInst *in, SfxVoiceInfo *out);
