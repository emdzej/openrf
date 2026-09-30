/* Sound-effect system, ported from the original mixer (addresses are VAs in RFIRE.BIN).
   Layers:
   - Exec-style lists (FUN_0040b460..0x40b620) so ordering/tail picks match the original.
   - Instances (0x457860, 15 x 0x58) and voices (0x457210, 20 x 0x50): Snd_CreateInstance,
     Snd_ResortInstances, Snd_AssignVoices, Snd_MixAndUpdateVoices, the command table.
   - "DirectSound" buffers: one per voice (DuplicateSoundBuffer of the sample), with volume
     (hundredths of dB), pan and frequency, rendered by a linear-interpolating software mixer.
   Findings baked in (see docs/architecture.md section 7):
   - The sample-table rate field (+0x10) is never written by the retail code, so event frequency
     ratios evaluate to 0 and the event frequency is never sent to a voice: voices play at the
     sample's native rate unless a pitch callback (engine/rotor) or the bunker step sets it.
     SndCb_HeliRotorPitch therefore gives 2621 + 2621 * rotorSpeed Hz.
   - Loop markers are unused; looping instances loop the whole buffer. */
#include "sfx.h"
#include "assets.h"
#include "game/object.h"
#include "game/vehicle.h"
#include "game/fixmath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ lists */
typedef struct Node { struct Node *succ, *pred; } Node;
typedef struct { Node head, tail; } List;      /* head.pred == NULL, tail.succ == NULL */

static void list_init(List *l) { l->head.succ = &l->tail; l->head.pred = NULL; l->tail.succ = NULL; l->tail.pred = &l->head; }
static Node *list_first(List *l) { return l->head.succ->succ ? l->head.succ : NULL; }
static Node *node_next(Node *n) { return n->succ->succ ? n->succ : NULL; }
static void node_remove(Node *n)                          /* FUN_0040b480 */
{
    if (!n->succ) return;
    n->succ->pred = n->pred;
    n->pred->succ = n->succ;
    n->succ = NULL;
}
static void insert_after(Node *p, Node *n)                /* FUN_0040b5a0 */
{
    node_remove(n);
    n->pred = p; n->succ = p->succ;
    p->succ->pred = n; p->succ = n;
}
static void add_head(List *l, Node *n) { insert_after(&l->head, n); }       /* FUN_0040b520 */
static void add_tail(List *l, Node *n) { node_remove(n); insert_after(l->tail.pred, n); } /* FUN_0040b560 */
static Node *rem_tail(List *l)                            /* FUN_0040b4f0 */
{
    Node *n = l->tail.pred;
    if (!n->pred) return NULL;
    node_remove(n);
    return n;
}

/* ------------------------------------------------------------------ samples + DirectSound emulation */
typedef struct { int16_t *pcm; uint32_t len; int32_t rate; } SampleData;
static SampleData samples[SFX_NSAMPLES];

typedef struct {
    const SampleData *s;
    bool exists, started;      /* buffer duplicated / Play() called (0x46f504 flag) */
    bool playing, loop;        /* DSBSTATUS_PLAYING / DSBPLAY_LOOPING */
    uint64_t pos;              /* 32.32 sample position */
    int32_t freq, vol, pan;
    float gl, gr;
} DsBuf;
static DsBuf ds[SFX_NVOICES];

static void (*lock_fn)(void), (*unlock_fn)(void);
static void lock(void) { if (lock_fn) lock_fn(); }
static void unlock(void) { if (unlock_fn) unlock_fn(); }
void sfx_set_lock_fns(void (*l)(void), void (*u)(void)) { lock_fn = l; unlock_fn = u; }

static void ds_gains(DsBuf *b)
{
    float lin = powf(10.f, (float)b->vol / 2000.f);
    b->gl = lin * (b->pan > 0 ? powf(10.f, (float)-b->pan / 2000.f) : 1.f);
    b->gr = lin * (b->pan < 0 ? powf(10.f, (float)b->pan / 2000.f) : 1.f);
}
static void ds_release(int i) { lock(); memset(&ds[i], 0, sizeof ds[i]); unlock(); }
static void ds_set_volume(int i, int32_t v)
{
    if (v < -10000) v = -10000;
    lock(); ds[i].vol = v; ds_gains(&ds[i]); unlock();
}
static void ds_set_pan(int i, int32_t p)
{
    if (p < -10000) p = -10000;
    if (p > 10000) p = 10000;
    lock(); ds[i].pan = p; ds_gains(&ds[i]); unlock();
}
static void ds_set_freq(int i, int32_t f)                 /* SetFrequency: 0 = original, 100..100000 */
{
    if (!ds[i].exists) return;
    if (f != 0 && (f < 100 || f > 100000)) return;        /* DSERR_INVALIDPARAM */
    lock(); ds[i].freq = f ? f : ds[i].s->rate; unlock();
}

/* Snd_VoiceSetParam 0x42d370 (1 = volume, 2 = frequency, 3 = pan; pan arrives negated). */
static void voice_set_param(int i, int32_t v, int which)
{
    if (which == 1) { if (ds[i].started) ds_set_volume(i, v > 0 ? 0 : v); }
    else if (which == 2) ds_set_freq(i, v);
    else if (which == 3) { if (ds[i].started) ds_set_pan(i, -v); }
}

/* Snd_VoicePlay 0x42c930: duplicate the sample buffer, set pan/volume, Play(looping). */
static void voice_play(int i, int sample, uint32_t inst_flags, int32_t l, int32_t r)
{
    if (ds[i].started || sample < 0 || !samples[sample].pcm) return;   /* original: error box */
    int32_t vol, pan;
    if (r == l) { vol = l / 2 - 3000; pan = 0; }
    else if (r < l) { vol = l / 2 - 3000; pan = (r - l) * 10000 / l; }
    else { vol = r / 2 - 3000; pan = (r - l) * 10000 / r; }
    if (pan < -10000) pan = -10000;
    if (pan > 10000) pan = 10000;
    if (vol > 0) vol = 0;
    lock();
    DsBuf *b = &ds[i];
    memset(b, 0, sizeof *b);
    b->s = &samples[sample];
    b->exists = true;
    b->freq = b->s->rate;
    b->pan = pan;
    b->vol = vol < -10000 ? -10000 : vol;
    ds_gains(b);
    b->loop = (inst_flags & 1) != 0;
    b->playing = b->started = true;
    unlock();
}

/* ------------------------------------------------------------------ mixer state */
typedef struct Voice {
    Node n;
    int idx;                   /* [2] 1-based in the original; 0-based DS slot here */
    struct SfxInst *inst;      /* [3] */
    uint32_t flags;            /* [4] 1 new sample, 2 start pending, 4 killed (keeps its budget) */
    int32_t l, r;              /* [9] [10] */
    int sample;                /* [0x10] */
    struct Voice *start_next;
} Voice;

struct SfxInst {
    Node n;
    struct SfxInst *tmp;       /* [2] scratch chain */
    struct SfxInst *pend;      /* [3] "created since the last assign" chain (non-looping only) */
    bool in_pend;
    const SfxEvent *ev;        /* [4] */
    uint32_t handle;           /* [5] */
    uint32_t deadline;         /* [6] */
    int32_t prio, prio2;       /* [7] [8] */
    uint32_t flags;            /* [9] event flags & 0x5f | 0x20 updated | 0x80 detached with last pos */
    Voice *voice;              /* [10] */
    int sample;                /* [0xb] */
    Obj *owner;                /* [0xc] */
    int32_t gl, gr;            /* [0xe] [0xf] 16.16 */
    int32_t freq;              /* [0x10] */
    int32_t egain;             /* [0x11] */
    bool stereo_owner;         /* [0x12] gain fn: Snd_CalcGainStereoOwner vs TwoListeners */
    int32_t last[3];           /* [0x13..0x15] */
    const int32_t *spos[2];    /* [0x13] [0x14] for SFXF_OWN_GAINS */
};

static Voice voices[SFX_NVOICES];
static SfxInst insts[SFX_NINST];
static List voice_list, active, stopped, free_list;
static SfxInst *pending;
static int nvoices;
static uint32_t next_handle = 1;          /* DAT_00443730 */
static uint32_t now;                       /* DAT_00481304 */
static uint32_t cur_time;
static bool dirty_resort, dirty_assign, dirty_mix; /* DAT_004812fc / DAT_0048135c / DAT_0048137c */
static bool enabled;
static int log_level;

static const int32_t zero_pos[3];          /* DAT_00457850 */
static const int32_t unity_gain = 0x10000; /* DAT_00443738 */
static const int32_t *lis_pos[2] = { zero_pos, zero_pos };
static int32_t lis_xoff[2];
static const int32_t *lis_gain[2] = { &unity_gain, &unity_gain };

#define INST(n) ((SfxInst *)(n))
#define VOICE(n) ((Voice *)(n))

static void free_instance(SfxInst *in);
static void voice_stop(Voice *v);

/* ------------------------------------------------------------------ gains / callbacks */
static int32_t dist_units(const int32_t *a, const int32_t *b)   /* FUN_0041ece0 >> 16 */
{
    int32_t dx = (b[0] - a[0]) >> 16, dy = (b[1] - a[1]) >> 16, dz = (b[2] - a[2]) >> 16;
    uint32_t sq = (uint32_t)(dx * dx) + (uint32_t)(dy * dy) + (uint32_t)(dz * dz);
    return (int32_t)((uint32_t)(int32_t)sqrt((double)sq) << 16) >> 16;
}

static int32_t listener_gain(int k, const int32_t *src)
{
    int32_t lg = *lis_gain[k];
    if (lg < 1) return 0;
    int32_t p[3] = { lis_pos[k][0] + lis_xoff[k], lis_pos[k][1], lis_pos[k][2] };
    int32_t t = 0x1a8 - dist_units(src, p);
    int32_t g;
    if (t > 0x180) g = 0x10000;
    else if (t < 1) return 0;
    else g = (t << 16) / 0x180;
    if (lg < 0x10000) g = fix_mul(g, lg);
    return g;
}

static void calc_gain(SfxInst *in)
{
    if (in->stereo_owner) {                                  /* Snd_CalcGainStereoOwner 0x419420 */
        in->gl = in->spos[0] ? listener_gain(0, in->spos[0]) : 0;
        in->gr = in->spos[1] ? listener_gain(1, in->spos[1]) : 0;
        return;
    }
    if (!in->owner && !(in->flags & 0x80)) {                 /* Snd_CalcGainTwoListeners 0x419280 */
        in->gl = in->gr = (in->flags & SFXF_MUTED) ? 0 : 0x10000;
        if (in->flags & SFXF_MUTE_R) in->gr = 0;
        if (in->flags & SFXF_MUTE_L) in->gl = 0;
        return;
    }
    const int32_t *src = in->owner ? in->owner->pos : in->last;
    in->gl = listener_gain(0, src);
    in->gr = listener_gain(1, src);
}

static void run_callback(SfxInst *in)
{
    Obj *o = in->owner;
    if (!o || !o->p60 || in->ev->cb == SFX_CB_NONE) return;
    const VehState *s = veh_state(o);
    int32_t f;
    if (in->ev->cb == SFX_CB_ENGINE) {                       /* SndCb_JeepEnginePitch 0x42b960 */
        int32_t lo = s->def->snd_91, hi = s->def->snd_92;
        uint32_t sp = (uint32_t)(o->speed < 0 ? -o->speed : o->speed);
        f = ((int32_t)((uint32_t)(hi - lo) * sp) >> 16) + lo;
    } else {                                                 /* SndCb_HeliRotorPitch 0x42b9b0 */
        const int32_t rate_field = 0;                        /* sample table +0x10, never filled */
        f = fix_mul(0xa3d - rate_field, s->boat_target) + 0xa3d;
    }
    in->freq = f;
    if (in->voice) voice_set_param(in->voice->idx, f, 2);
}

static void decay_prio(SfxInst *in)
{
    if (in->prio != in->prio2 && in->deadline < now) in->prio = in->prio2;
}

static bool prio_cmp(SfxInst *a, SfxInst *b)              /* Snd_InstancePriorityCmp 0x418b30 */
{
    if (b->prio == a->prio) return a->gl + a->gr < b->gl + b->gr;
    return b->prio <= a->prio;
}

static void enqueue_sorted(SfxInst *in)                   /* FUN_0040b620(&active, in, cmp) */
{
    node_remove(&in->n);
    Node *c = active.head.succ;
    while (c->succ && !prio_cmp(in, INST(c))) c = c->succ;
    insert_after(c->pred, &in->n);
}

/* ------------------------------------------------------------------ owner attach / free */
static void detach(SfxInst *in)                           /* Snd_AttachInstanceToOwner(in, 0) 0x419020 */
{
    Obj *o = in->owner;
    in->owner = NULL;
    if (!o) return;
    if (!(in->flags & 1)) {
        memcpy(in->last, o->pos, sizeof in->last);
        in->flags |= 0x80;
        return;
    }
    free_instance(in);
}

static void free_instance(SfxInst *in)                    /* Snd_FreeInstance 0x418e30 */
{
    in->handle = next_handle++;
    if (in->voice) voice_stop(in->voice);
    if (in->owner) detach(in);
    add_tail(&free_list, &in->n);
    in->flags = 0;
}

static void voice_stop(Voice *v)                          /* Snd_VoiceStop 0x42d2e0 */
{
    if (!v) return;
    SfxInst *in = v->inst;
    if (in) {
        ds_release(v->idx);
        in->voice = NULL;
        if (!(in->flags & 1)) free_instance(in);
    }
    v->inst = NULL;
    v->flags &= ~1u;
    dirty_assign = true;
}

void sfx_detach_owner(Obj *owner)                         /* Snd_DetachAllFromOwner 0x419100 */
{
    if (!enabled || !owner) return;
    for (int i = 0; i < SFX_NINST; i++) if (insts[i].owner == owner) detach(&insts[i]);
}

/* ------------------------------------------------------------------ instances */
static SfxInst *create_instance(const SfxEvent *ev, Obj *owner)   /* Snd_CreateInstance 0x419140 */
{
    if (!samples[ev->sample].pcm) return NULL;
    Node *fn = list_first(&free_list);
    if (!fn) return NULL;
    SfxInst *in = INST(fn);
    in->ev = ev;
    in->prio = ev->prio;
    in->prio2 = ev->prio_decayed;
    in->deadline = ev->decay + now;
    in->stereo_owner = (ev->flags & SFXF_OWN_GAINS) != 0;
    in->spos[0] = in->spos[1] = NULL;
    in->flags |= ev->flags & 0x5f;
    if (!(in->flags & 1) && !in->in_pend) { in->pend = pending; pending = in; in->in_pend = true; }
    in->voice = NULL;
    in->sample = ev->sample;
    const int32_t rate_field = 0;                          /* sample table +0x10, never filled */
    in->freq = ev->freq < 0 ? (int32_t)(-(rate_field * ev->freq)) >> 16 : ev->freq ? ev->freq : rate_field;
    in->egain = ev->gain;
    in->owner = owner;                                     /* Snd_AttachInstanceToOwner (free => unowned) */
    decay_prio(in);
    calc_gain(in);
    enqueue_sorted(in);
    dirty_assign = true;
    in->flags |= 0x20;
    if (in->owner) run_callback(in);
    return in;
}

static void resort(void)                                  /* Snd_ResortInstances 0x418b70 */
{
    SfxInst *order[SFX_NINST];
    int n = 0;
    for (Node *c = active.tail.pred; c->pred && n < SFX_NINST; c = c->pred) order[n++] = INST(c);
    for (int k = 0; k < n; k++) {
        SfxInst *in = order[k];
        decay_prio(in);
        calc_gain(in);
        int32_t sum = in->gl + in->gr;
        Node *p = in->n.pred;
        bool moved = false;
        if (p->pred) {                                     /* move towards the head */
            while (1) {
                SfxInst *q = INST(p);
                if (q->prio == in->prio) { if (q->gl + q->gr < sum) break; }
                else if (in->prio <= q->prio) break;
                moved = true;
                p = p->pred;
                if (!p->pred) break;
            }
        }
        if (moved) { insert_after(p, &in->n); dirty_assign = true; continue; }
        Node *s = in->n.succ;
        if (s->succ) {                                     /* move towards the tail */
            while (1) {
                SfxInst *q = INST(s);
                if (q->prio == in->prio) { if (sum < q->gl + q->gr) break; }
                else if (q->prio <= in->prio) break;
                moved = true;
                s = s->succ;
                if (!s->succ) break;
            }
        }
        if (moved) { insert_after(s->pred, &in->n); dirty_assign = true; }
    }
    dirty_resort = false;
}

static void bind(Voice *v, SfxInst *in)                   /* Snd_BindInstanceToVoice 0x418c70 */
{
    if (in->voice != v) {
        if (in->voice) voice_stop(in->voice);
        if (v->inst) voice_stop(v);
    }
    v->flags &= ~4u;
    if (v->sample != in->sample) {
        v->sample = in->sample;
        if (!(in->flags & 1)) v->flags |= 1;
    }
    in->voice = v;
    v->flags |= 2;
    v->inst = in;
    dirty_mix = true;
}

static void assign_voices(void)                           /* Snd_AssignVoices 0x418d10 */
{
    dirty_mix = true;
    SfxInst *chain = NULL;
    int left = nvoices;
    for (Node *c = list_first(&active); c && left > 0; c = node_next(c)) {
        SfxInst *in = INST(c);
        if (in->gl > 0 || in->gr > 0) {
            if (in->voice) node_remove(&in->voice->n);
            left--;
            in->tmp = chain;
            chain = in;
        }
    }
    for (SfxInst *in = chain; in; in = in->tmp)
        if (!in->voice) {
            Node *vn = rem_tail(&voice_list);
            if (vn) bind(VOICE(vn), in);
        }
    for (SfxInst *in = chain; in; in = in->tmp)
        if (in->voice) add_head(&voice_list, &in->voice->n);
    /* voices that were bound to chain instances but lost them (never here) stay unlinked: relink */
    for (int i = 0; i < SFX_NVOICES; i++) if (!voices[i].n.succ) add_tail(&voice_list, &voices[i].n);
    for (SfxInst *p = pending, *nx; p; p = nx) {           /* one-shots that got no voice are dropped */
        nx = p->pend;
        p->in_pend = false;
        p->pend = NULL;
        if (!p->voice && p->ev) free_instance(p);
    }
    pending = NULL;
    dirty_assign = false;
}

static void mix_update(void)                              /* Snd_MixAndUpdateVoices 0x418e90 */
{
    int32_t bud_l = 0x7fff, bud_r = 0x7fff;
    Voice *start = NULL;
    for (Node *c = list_first(&voice_list); c; c = node_next(c)) {
        Voice *v = VOICE(c);
        SfxInst *in = v->inst;
        if (!in) {
            if (!(v->flags & 4)) { v->r = 0; v->l = 0; continue; }
        } else {
            int32_t g = (int32_t)(((int64_t)in->gl * in->egain) >> 16);
            v->l = g < bud_l ? g : bud_l;
            g = (int32_t)(((int64_t)in->gr * in->egain) >> 16);
            v->r = g < bud_r ? g : bud_r;
        }
        bud_l -= v->l;
        bud_r -= v->r;
        if (in && (v->flags & 2)) { v->start_next = start; start = v; }
    }
    for (Node *c = voice_list.tail.pred; c->pred; c = c->pred) {
        Voice *v = VOICE(c);
        if (!v->inst) continue;
        int32_t m, pan;
        if (v->l == v->r) { pan = 0; m = v->l; }
        else if (v->r < v->l) { pan = (v->r - v->l) * 10000 / v->l; m = v->l; }
        else { pan = (v->r - v->l) * 10000 / v->r; m = v->r; }
        m = m / 3 - 0x898;
        if (pan < -10000) pan = -10000;
        if (pan > 10000) pan = 10000;
        if (m > 0) m = 0;
        voice_set_param(v->idx, m, 1);
        voice_set_param(v->idx, -pan, 3);
    }
    for (Voice *v = start; v; v = v->start_next) {
        v->flags &= ~2u;
        voice_play(v->idx, v->sample, v->inst->flags, v->l, v->r);
    }
    dirty_mix = false;
}

/* ------------------------------------------------------------------ commands */
typedef struct { int cmd; intptr_t a; const void *b; SfxInst **out; uint32_t *out_handle; } Cmd;
enum { QUEUE_LEN = 10 };                                   /* 0x457d90..0x457e80, 0x18 each */
static Cmd queue[QUEUE_LEN];
static int nqueue;

static void cmd_stop_all(bool all)                        /* SndCmd10_StopAll 0x419870 */
{
    SfxInst *order[SFX_NINST];
    int n = 0;
    for (Node *c = active.tail.pred; c->pred && n < SFX_NINST; c = c->pred) order[n++] = INST(c);
    for (int k = 0; k < n; k++) if (all || !order[k]->owner) free_instance(order[k]);
}

static void exec_cmd(Cmd *c)
{
    SfxInst *in = (SfxInst *)c->a;
    switch (c->cmd) {
    case SFX_SYNC: break;
    case SFX_PLAY: {                                       /* SndCmd1_Play */
        const SfxEvent *ev = sfx_event((uint32_t)c->a);
        SfxInst *r = ev ? create_instance(ev, (Obj *)c->b) : NULL;
        if (c->out) *c->out = r;
        if (c->out_handle) *c->out_handle = r ? r->handle : 0;
        if (r && log_level) fprintf(stderr, "sfx: t=%u play %-14s owner=%p inst=%d h=%u prio=%d gain=%x/%x\n",
                                    now, ev->name, c->b, (int)(r - insts), r->handle, r->prio, r->gl, r->gr);
        else if (!r && ev && log_level) fprintf(stderr, "sfx: t=%u play %-14s DROPPED (no free instance)\n", now, ev->name);
        break;
    }
    case SFX_REFRESH: dirty_resort = dirty_mix = dirty_assign = true; break;
    case SFX_STOP:                                         /* SndCmd3_Stop */
        if (in && in->owner == (Obj *)c->b) {
            voice_stop(in->voice);
            add_tail(&stopped, &in->n);
            in->flags &= ~0x20u;
        }
        break;
    case SFX_UPDATE:                                       /* SndCmd4_Update */
        if (in && in->owner == (Obj *)c->b) {
            decay_prio(in);
            calc_gain(in);
            enqueue_sorted(in);
            dirty_assign = true;
            in->flags |= 0x20;
            if (in->owner) run_callback(in);
        }
        break;
    case SFX_OWNER_CB:                                     /* SndCmd5_OwnerCallbacks */
        if (!in) { for (int i = 0; i < SFX_NINST; i++) if (c->b && insts[i].owner == (Obj *)c->b) run_callback(&insts[i]); }
        else if (in->owner == (Obj *)c->b) run_callback(in);
        break;
    case SFX_LISTENER1: case SFX_LISTENER2: {              /* SndCmd6/7_SetListenerNPos */
        int k = c->cmd - SFX_LISTENER1;
        lis_pos[k] = c->b ? (const int32_t *)c->b : zero_pos;
        lis_xoff[k] = (int32_t)c->a;
        dirty_mix = dirty_assign = dirty_resort = true;
        break;
    }
    case SFX_LISTENER_GAIN:                                /* SndCmd8_SetListenerGain */
        if (c->a == 0 || c->a == 1) lis_gain[c->a] = c->b ? (const int32_t *)c->b : &unity_gain;
        break;
    case SFX_KILL: {                                       /* SndCmd9_KillByHandle */
        SfxInst *k = (SfxInst *)c->b;
        if (c->a && k && k->handle == (uint32_t)c->a) {
            Voice *v = k->voice;
            free_instance(k);
            if (v) v->flags |= 4;
        }
        break;
    }
    case SFX_STOP_ALL: cmd_stop_all(c->a != 0); break;
    }
}

static void service_main(void)                            /* Snd_Service(0x66) */
{
    now = cur_time;
    for (int i = 0; i < nqueue; i++) exec_cmd(&queue[i]);
    nqueue = 0;
    if (dirty_resort) resort();
    if (dirty_assign) assign_voices();
    if (dirty_mix) mix_update();
}

static void service_reap(void)                            /* Snd_Service(0x67): finished buffers */
{
    for (Node *c = list_first(&voice_list), *nx; c; c = nx) {
        nx = node_next(c);
        Voice *v = VOICE(c);
        if (!v->inst || !ds[v->idx].started) continue;
        lock();
        bool playing = ds[v->idx].playing;
        unlock();
        if (playing) continue;
        SfxInst *in = v->inst;
        ds_release(v->idx);
        in->voice = NULL;
        if (!(in->flags & 1)) free_instance(in);
        v->inst = NULL;
        v->flags &= ~1u;
        dirty_assign = true;
    }
}

static void queue_push(int cmd, intptr_t a, const void *b, SfxInst **out, uint32_t *out_handle, bool flush)
{
    if (!enabled) { if (out) *out = NULL; if (out_handle) *out_handle = 0; return; }
    if (nqueue == QUEUE_LEN) service_main();               /* queue full: service first */
    queue[nqueue++] = (Cmd){ cmd, a, b, out, out_handle };
    if (flush) service_main();
}

void sfx_cmd(int cmd, intptr_t a, const void *b, bool flush) { queue_push(cmd, a, b, NULL, NULL, flush); }

SfxInst *sfx_play(uint32_t event_va, Obj *owner, uint32_t *handle)
{
    SfxInst *r = NULL;
    uint32_t h = 0;
    queue_push(SFX_PLAY, (intptr_t)event_va, owner, &r, &h, true);
    if (handle) *handle = h;
    return r;
}

void sfx_hook(int cmd, uint32_t event_va, Obj *owner)
{
    if (cmd < 0) sfx_detach_owner(owner);
    else queue_push(cmd, (intptr_t)event_va, owner, NULL, NULL, false);
}

void sfx_set_stereo_sources(SfxInst *in, const int32_t *pl, const int32_t *pr)
{
    if (in) { in->spos[0] = pl; in->spos[1] = pr; }
}

/* FUN_004042b0 + the TimerQueueAdd(FUN_00404380 -> cmd 9) kill. */
typedef struct { SfxInst *in; uint32_t handle, at; } TimedKill;
static TimedKill kills[8];

void sfx_play_step(int which, int32_t ratio, int32_t ticks)
{
    if (which < 0 || which > 1) return;
    uint32_t h;
    SfxInst *in = sfx_play(sfx_events[sfx_step_events[which]].va, NULL, &h);
    if (!in) return;
    if (in->voice) voice_set_param(in->voice->idx, (int32_t)((uint32_t)ratio * (uint32_t)-11000) >> 16, 2);
    if (ticks > 0 && in->handle == h) {
        for (int i = 0; i < 8; i++)
            if (!kills[i].in) { kills[i] = (TimedKill){ in, h, cur_time + (uint32_t)ticks }; return; }
        sfx_cmd(SFX_KILL, (intptr_t)h, in, true);          /* original: timer add failed -> kill now */
    }
}

void sfx_set_time(uint32_t t) { cur_time = t; }

void sfx_frame(void)
{
    if (!enabled) return;
    for (int i = 0; i < 8; i++)
        if (kills[i].in && (int32_t)(cur_time - kills[i].at) >= 0) {
            sfx_cmd(SFX_KILL, (intptr_t)kills[i].handle, kills[i].in, false);
            kills[i].in = NULL;
        }
    sfx_cmd(SFX_REFRESH, 0, NULL, true);
    service_reap();
    static uint32_t last_log;
    if (log_level && cur_time - last_log >= 64) {
        last_log = cur_time;
        SfxVoiceInfo vi[SFX_NVOICES];
        int n = sfx_debug_voices(vi, SFX_NVOICES), k = 0;
        for (int i = 0; i < n; i++) k += vi[i].event != NULL;
        fprintf(stderr, "sfx: t=%u instances=%d voices=%d\n", cur_time, sfx_debug_active_instances(), k);
        for (int i = 0; i < n; i++)
            if (vi[i].event)
                fprintf(stderr, "sfx:   v%-2d %-14s prio=%3d g=%5x/%5x lvl=%4x/%4x vol=%5d pan=%6d f=%5d %s\n",
                        vi[i].voice, vi[i].event, vi[i].prio, vi[i].gl, vi[i].gr, vi[i].l, vi[i].r,
                        vi[i].vol, vi[i].pan, vi[i].freq, vi[i].playing ? "playing" : "-");
    }
}

const SfxEvent *sfx_event(uint32_t va)
{
    uint32_t d = va - SFX_EVENT_BASE;
    if (va < SFX_EVENT_BASE || d % SFX_EVENT_STRIDE || d / SFX_EVENT_STRIDE >= SFX_NEVENTS) return NULL;
    return &sfx_events[d / SFX_EVENT_STRIDE];
}

/* ------------------------------------------------------------------ init */
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static bool load_wav(const char *rel, SampleData *out)    /* Wav_LoadFile 0x4079f0 (RIFF WAVE) */
{
    size_t n;
    uint8_t *d = file_read_all(rel, &n);
    if (!d) return false;
    bool ok = false;
    if (n >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WAVE", 4)) {
        int ch = 0, bits = 0; uint32_t rate = 0;
        for (size_t p = 12; p + 8 <= n;) {
            uint32_t sz = rd32(d + p + 4);
            const uint8_t *c = d + p + 8;
            if (!memcmp(d + p, "fmt ", 4) && sz >= 16) {
                ch = c[2] | c[3] << 8; rate = rd32(c + 4); bits = c[14] | c[15] << 8;
            } else if (!memcmp(d + p, "data", 4) && ch && rate) {
                if (sz > n - p - 8) sz = (uint32_t)(n - p - 8);
                int bps = bits / 8 * ch;
                uint32_t frames = bps ? sz / (uint32_t)bps : 0;
                out->pcm = malloc((frames ? frames : 1) * sizeof(int16_t));
                for (uint32_t i = 0; i < frames; i++) {
                    int acc = 0;
                    for (int k = 0; k < ch; k++) {
                        const uint8_t *s = c + (size_t)i * bps + k * (bits / 8);
                        acc += bits == 8 ? (s[0] - 128) << 8 : (int16_t)(s[0] | s[1] << 8);
                    }
                    out->pcm[i] = (int16_t)(acc / ch);
                }
                out->len = frames;
                out->rate = (int32_t)rate;
                ok = frames > 0;
                break;
            }
            p += 8 + sz + (sz & 1);
        }
    }
    free(d);
    return ok;
}

static void mixer_reset(void)                             /* Snd_InitMixer 0x419970 */
{
    list_init(&voice_list); list_init(&active); list_init(&stopped); list_init(&free_list);
    memset(voices, 0, sizeof voices);
    memset(insts, 0, sizeof insts);
    for (int i = 0; i < SFX_NVOICES; i++) { voices[i].idx = i; voices[i].sample = -1; add_head(&voice_list, &voices[i].n); }
    nvoices = SFX_NVOICES;
    for (int i = 0; i < SFX_NINST; i++) { insts[i].handle = next_handle++; add_head(&free_list, &insts[i].n); }
    pending = NULL;
    nqueue = 0;
    memset(kills, 0, sizeof kills);
    dirty_resort = dirty_assign = dirty_mix = false;
    for (int i = 0; i < SFX_NVOICES; i++) ds_release(i);
    lis_pos[0] = lis_pos[1] = zero_pos;
    lis_xoff[0] = lis_xoff[1] = 0;
    lis_gain[0] = lis_gain[1] = &unity_gain;
}

bool sfx_init(void)                                       /* Snd_LoadAllSamples 0x42cbc0 + Snd_InitMixer */
{
    const char *lg = getenv("OPENRF_SFX_LOG");
    log_level = lg ? atoi(lg) : 0;
    if (!enabled) {
        int n = 0;
        for (int i = 0; i < SFX_NSAMPLES; i++) {
            if (load_wav(sfx_samples[i].path, &samples[i])) n++;
            else fprintf(stderr, "sfx: cannot load %s\n", sfx_samples[i].path);
        }
        if (log_level) fprintf(stderr, "sfx: loaded %d/%d samples\n", n, SFX_NSAMPLES);
        enabled = n > 0;
    }
    mixer_reset();
    return enabled;
}

void sfx_shutdown(void)
{
    if (enabled) mixer_reset();
    enabled = false;
    lock_fn = unlock_fn = NULL;
    for (int i = 0; i < SFX_NSAMPLES; i++) { free(samples[i].pcm); samples[i] = (SampleData){ 0 }; }
}

/* ------------------------------------------------------------------ render */
void sfx_render(int16_t *out, int frames)
{
    static float acc[4096 * 2];
    while (frames > 0) {
        int n = frames > 4096 ? 4096 : frames;
        memset(acc, 0, sizeof(float) * 2 * (size_t)n);
        for (int v = 0; v < SFX_NVOICES; v++) {
            DsBuf *b = &ds[v];
            if (!b->playing || !b->s) continue;
            const int16_t *pcm = b->s->pcm;
            uint64_t len = b->s->len, end = len << 32;
            uint64_t step = ((uint64_t)(uint32_t)b->freq << 32) / 44100;
            float gl = b->gl / 32768.f, gr = b->gr / 32768.f;
            for (int i = 0; i < n; i++) {
                if (b->pos >= end) {
                    if (!b->loop) { b->playing = false; break; }
                    b->pos %= end;
                }
                uint32_t k = (uint32_t)(b->pos >> 32);
                float f = (float)(uint32_t)b->pos * (1.f / 4294967296.f);
                float s0 = pcm[k];
                float s1 = k + 1 < len ? pcm[k + 1] : b->loop ? pcm[0] : 0.f;
                float s = s0 + (s1 - s0) * f;
                acc[2 * i] += s * gl;
                acc[2 * i + 1] += s * gr;
                b->pos += step;
            }
        }
        for (int i = 0; i < 2 * n; i++) {
            float x = acc[i] * 32768.f;
            out[i] = (int16_t)(x > 32767.f ? 32767 : x < -32768.f ? -32768 : (int)x);
        }
        out += 2 * n;
        frames -= n;
    }
}

/* ------------------------------------------------------------------ introspection */
int sfx_debug_voices(SfxVoiceInfo *out, int max)
{
    int n = 0;
    for (Node *c = list_first(&voice_list); c && n < max; c = node_next(c)) {
        Voice *v = VOICE(c);
        SfxInst *in = v->inst;
        lock();
        DsBuf b = ds[v->idx];
        unlock();
        out[n++] = (SfxVoiceInfo){ v->idx, in ? in->ev->name : NULL, in ? in->handle : 0, in ? in->flags : 0,
                                   in ? in->prio : 0, in ? in->gl : 0, in ? in->gr : 0, v->l, v->r,
                                   b.playing, b.vol, b.pan, b.freq };
    }
    return n;
}

int sfx_debug_active_instances(void)
{
    int n = 0;
    for (Node *c = list_first(&active); c; c = node_next(c)) n++;
    return n;
}

bool sfx_debug_instance(const SfxInst *in, SfxVoiceInfo *out)
{
    bool act = false;
    for (Node *c = list_first(&active); c; c = node_next(c)) if (INST(c) == in) act = true;
    if (!act) return false;
    DsBuf b = { 0 };
    Voice *v = in->voice;
    if (v) { lock(); b = ds[v->idx]; unlock(); }
    *out = (SfxVoiceInfo){ v ? v->idx : -1, in->ev->name, in->handle, in->flags, in->prio, in->gl, in->gr,
                           v ? v->l : 0, v ? v->r : 0, b.playing, b.vol, b.pan, b.freq };
    return true;
}
