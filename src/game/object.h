/* Dynamic object system: 512-slot pool (0x47261c, stride 0x74), class descriptors, cell chains,
   delete queue, per-frame update, delta-sorted timer queue. See docs/game.md §2. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "shape.h"

#define OBJ_POOL 512

/* obj->flags (+0x0c) */
enum {
    OF_INACTIVE_LIST = 0x20,       /* moved to the inactive/useless list (FUN_0041e200) */
    OF_ALIVE      = 0x40,          /* set by ObjCreate */
    OF_DELETE     = 0x80,          /* queued in the delete list (ObjMarkForDelete) */
    OF_NOCOLLIDE  = 0x100,         /* ObjCheckCollision skips the object */
    OF_KILLED_BY_STORAGE = 0x200,  /* "impact": projectile/grenade/mine hit (explodes on destroy); mines crushed by a lift */
    OF_SOUND      = 0x1000000,     /* engine sound running / (wreck) out of fuel */
    OF_NEW        = 0x2000000,     /* first update pending (vehicles) / wreck: explode */
    OF_BLOCKED    = 0x4000000,     /* last move was blocked */
    OF_WRECK_BURN = 0x8000000,
};

typedef struct Obj Obj;
typedef struct ObjClass ObjClass;
typedef struct Team Team;
typedef int (*ThinkFn)(Obj *o);

/* 0x74-byte object (offsets of the original in comments). Class-specific area starts at +0x58. */
struct Obj {
    Obj *next, *prev;          /* +0x00/+0x04 membership of free/active/inactive/useless list */
    uint32_t id;               /* +0x08 bits 0-8 slot, bits 9+ serial (+0x200 per ObjCreate) */
    uint32_t flags;            /* +0x0c OF_* */
    int32_t team;              /* +0x10 0/1, 2 = neutral */
    const ObjClass *cls;       /* +0x14 */
    ThinkFn think;             /* +0x18 per-class "think"/controller (vehicles: returns control word) */
    uint32_t *cell;            /* +0x1c map cell word (or &outside_cell) */
    Obj *cnext, *cprev;        /* +0x20/+0x24 chain of objects in the same cell (head index in cell bits 16-24) */
    int32_t _28;
    Obj *parent;               /* +0x2c */
    Obj *child;                /* +0x30 first child */
    Obj *sibling;              /* +0x34 next child of the parent */
    Obj *delnext;              /* +0x38 delete-queue link */
    const Gfx *gfx;            /* +0x3c graphic / collision descriptor */
    int32_t pos[3];            /* +0x40/+0x44/+0x48 x, y, z (16.16; x,y in 0..0x10000000) */
    uint32_t heading;          /* +0x4c 0..0x3fffff */
    int32_t _50;               /* +0x50 zeroed by ObjCreate */
    int32_t speed;             /* +0x54 signed, px/tick 16.16 */
    /* class-specific (+0x58..+0x73); the unions name the per-class views of the same slot */
    int32_t u58;               /* +0x58 storage: bytes camera offset; grenade: hit kind; Expl: custom gfx (slot) */
    union {
        Team *tm;              /* +0x5c team struct (vehicle, storage, wreck) */
        const void *p5c;       /* projectile: ProjType*; debris: DebrisDef* */
        uint32_t v5c;          /* Expl: script cursor (VA); Stay: pitch; grenade: start x */
    };
    union {
        void *p60;             /* +0x60 vehicle: VehState*; storage: lift display graphic; Stay: graphic */
        int32_t i60;           /* projectile: pitch; grenade: start y; Mine: arming timer */
    };
    uint32_t e60;              /* +0x60 Expl: descriptor VA (kept apart from p60) */
    int32_t u64;               /* +0x64 wreck: creation tick; projectile: vx; Expl: frame; grenade: start z */
    int32_t u68;               /* +0x68 vehicle: launch counter; storage: dock delay; projectile: vy; Expl: rate */
    int32_t u6c;               /* +0x6c storage: vehicle type; wreck: 0xc0000; projectile: vz; Expl: scale */
    union {
        int32_t u70;           /* +0x70 storage: 1 = hold until released; heli: bob; wreck: copied */
        uint8_t b70[4];        /* projectile: life, anim frame, anim timer, hit kind */
    };
    uint32_t *cell70;          /* +0x70 Expl: the BNO cell it destroys (SpawnExplosion arg 6) */
    int8_t b57;                /* +0x57 Expl: pitch (FUN_00420a40); +0x54..+0x56: attach offset */
    int8_t att[3];             /* +0x54..+0x56 Expl attached to a parent: offset in px */
};

/* Class descriptor (0x48 bytes). Return conventions as in the original. */
struct ObjClass {
    int32_t id;                                            /* +0x00 */
    const char *name;                                      /* +0x04 */
    int (*init)(const ObjClass *c, Obj *o, void *arg);     /* +0x08 0 = fail, >0 link to cell, <0 don't link */
    int (*destroy)(const ObjClass *c, Obj *o);             /* +0x0c 0 = veto */
    void (*update)(const ObjClass *c, Obj *o);             /* +0x10 */
    const Gfx *gfx;                                        /* +0x14 */
    void *_18;
    ThinkFn think;                                         /* +0x1c default think */
    void (*on_attached)(Obj *o, Obj *parent);              /* +0x20 */
    void (*on_detached)(Obj *o, Obj *parent);              /* +0x24 */
    void (*on_child_added)(Obj *o, Obj *child);            /* +0x28 */
    void (*on_child_removed)(Obj *o, Obj *child);          /* +0x2c */
    int32_t prio;                                          /* +0x30 collision priority */
    uint32_t (*hit_static)(Obj *o, uint32_t *cell, int bno); /* +0x34 bit0 = block */
    uint32_t (*hit_object)(Obj *o, Obj *other);            /* +0x38 bit0 = block (==4 when called as "other") */
    int (*damage)(Obj *o, Obj *src, int32_t amount);       /* +0x3c */
    void *_40;
    uint32_t cflags;                                       /* +0x44 bit1: new objects go to the "useless" list */
};

/* Class names are read from RFIRE.BIN (descriptor +0x04, obj_class_names_load in object.c): OBJ_CLASS_NAME
   reserves the (static, file-scope compound literal) buffer for a class definition. */
#define OBJ_CLASS_NAME_MAX 32
#define OBJ_CLASS_NAME ((const char *)(char[OBJ_CLASS_NAME_MAX]){ 0 })

extern Obj obj_pool[OBJ_POOL];      /* slot 0 unused */
extern Obj *obj_delete_queue;       /* DAT_00480e34 */

void obj_system_init(void);                                                  /* FUN_0041dff0 */
Obj *obj_create(const ObjClass *c, int team, int32_t x, int32_t y, int32_t z, void *arg); /* ObjCreate 0x41e240 */
void obj_mark_delete(Obj *o);                                                /* ObjMarkForDelete 0x41e480 */
int obj_destroy_now(Obj *o);                                                 /* ObjDestroyNow 0x41e0a0 */
void obj_reap(Obj **cursor);                                                 /* FUN_0041e4b0 */
void obj_update_all(void);                                                   /* ObjUpdateAll 0x41e970 */
void obj_link_cell(Obj *o, uint32_t *cell);                                  /* ObjLinkToCell 0x41e6d0 */
void obj_unlink_cell(Obj *o);                                                /* ObjUnlinkFromCell 0x41e640 */
int obj_move(Obj *o, const int32_t *d);                                      /* ObjMove 0x41e7e0: 1 = blocked */
void obj_move_to(Obj *o, int32_t x, int32_t y, int32_t z);                   /* FUN_0041e8e0 */
void obj_revert_move(Obj *o);                                                /* FUN_0041e920 */
void obj_set_parent(Obj *o, Obj *parent);                                    /* ObjSetParent 0x41ec00 */
void obj_set_inactive(Obj *o);                                               /* FUN_0041e200 */
void obj_insert_after(Obj *after, Obj *o);                                   /* List_InsertAfter */
void obj_touch_useless(Obj *o);                                              /* FUN_0040b560(0x443e00, o): LRU refresh */
Obj *obj_from_slot(uint32_t slot);
typedef void (*ObjVisit)(Obj *o, void *ctx);
void obj_foreach_active(ObjVisit fn, void *ctx);
/* Every live object (active, inactive and "useless" lists): what the cell chains hold for drawing. */
void obj_foreach_live(ObjVisit fn, void *ctx);
int obj_count_active(void);

/* Timer queue at DAT_004568b0 (TimerQueueAdd 0x401000 / TimerQueueAdvance 0x401090).
   Callback returns <0 = remove, 0 = repeat with the same period, >0 = new period. */
typedef int (*TimerFn)(intptr_t a, intptr_t b, int32_t period);
int timer_add(int32_t delay, TimerFn fn, intptr_t a, intptr_t b);
int timer_advance(int32_t dt);
void timer_clear(void);                                                      /* FUN_00401160 */

/* Per-frame callback list PTR_0043ee90 (RunCallbackList 0x4011a0): fn returns nonzero to be removed. */
typedef int (*FrameTaskFn)(intptr_t arg, void *self);
int frame_task_add(FrameTaskFn fn, intptr_t arg);
int frame_tasks_run(void);
void frame_tasks_clear(void);                                                /* FUN_004011f0 */
