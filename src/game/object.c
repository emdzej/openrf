/* Object pool, lists, cell chains, update dispatch, timers. Ported from 0x401000..0x4011f0 and
   0x41dff0..0x41ecb0. The original lists are Amiga-style MinLists (FUN_0040b460..0x40b5e0); here they
   are circular lists with a sentinel, with the same head/tail semantics. */
#include "game.h"
#include "vehicle.h"
#include "weapon.h"
#include "effect.h"
#include "ai.h"
#include "rules.h"
#include "../exe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Obj obj_pool[OBJ_POOL];
Obj *obj_delete_queue;
static Obj list_free, list_active, list_inactive, list_useless;   /* 0x443dd0 / de0 / df0 / e00 */
static uint32_t serial;                                              /* _DAT_00457f44 */
static int32_t move_old[3];                                          /* DAT_00480e20..28 */

static void list_init(Obj *l) { l->next = l->prev = l; }
static bool list_empty(const Obj *l) { return l->next == l; }
static void node_remove(Obj *n)           /* FUN_0040b480 */
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
    n->next = n->prev = NULL;
}
static Obj *rem_head(Obj *l)              /* FUN_0040b4c0 */
{
    if (list_empty(l)) return NULL;
    Obj *n = l->next;
    node_remove(n);
    return n;
}
static void add_tail(Obj *l, Obj *n)      /* FUN_0040b560 */
{
    if (n->next) node_remove(n);
    n->next = l; n->prev = l->prev; l->prev->next = n; l->prev = n;
}
static void add_head(Obj *l, Obj *n)      /* FUN_0040b520 */
{
    if (n->next) node_remove(n);
    n->prev = l; n->next = l->next; l->next->prev = n; l->next = n;
}

Obj *obj_from_slot(uint32_t slot) { return slot ? &obj_pool[slot & 0x1ff] : NULL; }
static uint32_t slot_of(const Obj *o) { return o->id & 0x1ff; }

void obj_system_init(void)
{
    obj_delete_queue = NULL;
    g_move_old_pos = NULL;
    memset(obj_pool, 0, sizeof obj_pool);
    list_init(&list_free); list_init(&list_active); list_init(&list_inactive); list_init(&list_useless);
    for (int i = 1; i < OBJ_POOL; i++) {
        obj_pool[i].id = (uint32_t)i;
        add_tail(&list_free, &obj_pool[i]);
    }
}

void obj_unlink_cell(Obj *o)
{
    uint32_t *c = o->cell;
    if (!c) return;
    if (CELL_HEAD(*c) == slot_of(o)) {
        Obj *n = o->cnext;
        if (n) { *c = (*c & ~0x1ff0000u) | (slot_of(n) << 16); n->cprev = NULL; }
        else *c &= 0xfe00ffffu;
    } else {
        if (o->cprev) o->cprev->cnext = o->cnext;
        if (o->cnext) o->cnext->cprev = o->cprev;
    }
    o->cell = NULL;
}

void obj_link_cell(Obj *o, uint32_t *c)
{
    if (!c) {
        c = &G.outside;
        if (o->pos[0] >= 0 && o->pos[0] < WORLD_SIZE && o->pos[1] >= 0 && o->pos[1] < WORLD_SIZE)
            c = &G.cell[(o->pos[1] >> 21) * 128 + (o->pos[0] >> 21)];
    }
    obj_unlink_cell(o);
    o->cell = c;
    uint32_t h = CELL_HEAD(*c);
    Obj *head = NULL;
    if (h) { obj_pool[h].cprev = o; head = &obj_pool[h]; }
    o->cprev = NULL;
    o->cnext = head;
    *c = (*c & ~0x1ff0000u) | (slot_of(o) << 16);
}

void obj_set_parent(Obj *o, Obj *parent)
{
    if (o == parent || !o || o->parent == parent) return;
    Obj *old = o->parent;
    if (old) {
        Obj **pp = &old->child;
        while (*pp != o) {
            if (!*pp) goto detached;
            pp = &(*pp)->sibling;
        }
        *pp = o->sibling;
detached:
        o->parent = NULL;
        if (old->cls->on_child_removed) old->cls->on_child_removed(old, o);
        if (o->cls->on_detached) o->cls->on_detached(o, old);
    }
    if (!parent) { o->sibling = NULL; return; }
    o->parent = parent;
    o->sibling = parent->child;
    parent->child = o;
    if (parent->cls->on_child_added) parent->cls->on_child_added(parent, o);
    if (o->cls->on_attached) o->cls->on_attached(o, parent);
}

/* Remove o from the delete queue if it is still queued. */
static void delq_remove(Obj *o)
{
    if (!(o->flags & OF_DELETE)) return;
    o->flags &= ~OF_DELETE;
    if (obj_delete_queue == o) { obj_delete_queue = o->delnext; return; }
    for (Obj *p = obj_delete_queue; p && p->delnext; p = p->delnext)
        if (p->delnext == o) { p->delnext = o->delnext; break; }
}

/* Shared tail of ObjDestroyNow / FUN_0041e4b0 / ObjUpdateAll after the class destroy hook agreed. */
static void obj_release(Obj *o)
{
    delq_remove(o);
    obj_set_parent(o, NULL);
    while (o->child) obj_set_parent(o->child, NULL);
    if (game_hooks.sound) game_hooks.sound(-1, 0, o);   /* Snd_DetachAllFromOwner 0x42d1a0 */
    obj_unlink_cell(o);
    node_remove(o);
    o->id &= 0x1ff;
    o->flags = 0;
    add_head(&list_free, o);
}

int obj_destroy_now(Obj *o)
{
    if (o->cls->destroy && !o->cls->destroy(o->cls, o)) return 0;
    obj_release(o);
    return 1;
}

void obj_mark_delete(Obj *o)
{
    if (o->flags & OF_DELETE) return;
    o->flags |= OF_DELETE;
    o->delnext = obj_delete_queue;
    obj_delete_queue = o;
}

/* FUN_0041e4b0: drain the delete queue; *cursor (an iteration cursor) is advanced past freed objects. */
void obj_reap(Obj **cursor)
{
    Obj *o;
    while ((o = obj_delete_queue) != NULL) {
        obj_delete_queue = o->delnext;
        if (cursor && *cursor == o) *cursor = o->next;
        o->flags &= ~OF_DELETE;
        if (o->cls->destroy && !o->cls->destroy(o->cls, o)) continue;
        obj_release(o);
    }
}

Obj *obj_create(const ObjClass *c, int team, int32_t x, int32_t y, int32_t z, void *arg)
{
    Obj *o = rem_head(&list_free);
    if (!o) {
        if (!list_empty(&list_useless)) {
            Obj *v = list_useless.next;
            if (!v->cls->destroy || v->cls->destroy(v->cls, v)) obj_release(v);
        }
        o = rem_head(&list_free);
        if (!o) return NULL;
    }
    serial += 0x200;
    o->id = (o->id & 0x1ff) | serial;
    o->flags = OF_ALIVE;
    o->team = team;
    o->cls = c;
    o->think = c->think;
    o->gfx = c->gfx;
    o->pos[0] = x; o->pos[1] = y; o->pos[2] = z;
    o->_50 = 0;
    add_tail(&list_active, o);
    if (!c->init) { obj_link_cell(o, NULL); return o; }
    int r = c->init(c, o, arg);
    if (r == 0) { add_head(&list_free, o); return NULL; }
    if (r > 0) obj_link_cell(o, NULL);
    return o;
}

void obj_set_inactive(Obj *o)
{
    if (o->flags & OF_INACTIVE_LIST) return;
    if (o->cls->cflags & 2) add_tail(&list_useless, o);
    else add_head(&list_inactive, o);
    o->flags |= OF_INACTIVE_LIST;
}

void obj_insert_after(Obj *after, Obj *o)
{
    if (o->next) node_remove(o);
    o->prev = after; o->next = after->next; after->next->prev = o; after->next = o;
}

void obj_touch_useless(Obj *o) { add_tail(&list_useless, o); }

void obj_update_all(void)
{
    obj_reap(NULL);
    Obj *cur = list_active.next;
    while (cur != &list_active) {
        Obj *nxt = cur->next;
        if (cur->cls->update) cur->cls->update(cur->cls, cur);
        cur = nxt;
        if (!G.running) return;
        obj_reap(&cur);
    }
}

void obj_foreach_active(ObjVisit fn, void *ctx)
{
    for (Obj *o = list_active.next, *n; o != &list_active; o = n) { n = o->next; fn(o, ctx); }
}

void obj_foreach_live(ObjVisit fn, void *ctx)
{
    Obj *lists[3] = { &list_active, &list_inactive, &list_useless };
    for (int k = 0; k < 3; k++)
        for (Obj *o = lists[k]->next, *n; o != lists[k]; o = n) { n = o->next; fn(o, ctx); }
}

int obj_count_active(void)
{
    int n = 0;
    for (Obj *o = list_active.next; o != &list_active; o = o->next) n++;
    return n;
}

int obj_move(Obj *o, const int32_t *d)
{
    move_old[0] = o->pos[0]; move_old[1] = o->pos[1]; move_old[2] = o->pos[2];
    g_move_old_pos = move_old;
    int32_t nx = d[0] + o->pos[0], ny = d[1] + o->pos[1];
    o->pos[2] += d[2];
    uint32_t *target = NULL;
    if (nx < 0 || nx >= WORLD_SIZE || ny < 0 || ny >= WORLD_SIZE) target = &G.outside;
    else if (((uint32_t)(o->pos[0] ^ nx) & 0xffe00000u) || ((uint32_t)(o->pos[1] ^ ny) & 0xffe00000u))
        target = &G.cell[(ny >> 21) * 128 + (nx >> 21)];
    o->pos[0] = nx; o->pos[1] = ny;
    if (target) obj_link_cell(o, target);
    if (obj_check_collision(o)) {
        o->pos[0] = move_old[0]; o->pos[1] = move_old[1]; o->pos[2] = move_old[2];
        if (target) obj_link_cell(o, NULL);
        g_move_old_pos = NULL;
        return 1;
    }
    return 0;
}

void obj_move_to(Obj *o, int32_t x, int32_t y, int32_t z)
{
    int32_t d[3] = { x - o->pos[0], y - o->pos[1], z - o->pos[2] };
    obj_move(o, d);
}

void obj_revert_move(Obj *o)
{
    if (!g_move_old_pos) return;
    o->pos[0] = move_old[0]; o->pos[1] = move_old[1]; o->pos[2] = move_old[2];
    obj_link_cell(o, NULL);
    g_move_old_pos = NULL;
}

/* ---------------- timers ---------------- */
typedef struct Timer { struct Timer *next, *prev; int32_t rem; TimerFn fn; intptr_t a, b; int32_t period; } Timer;
static Timer tq = { &tq, &tq, 0, 0, 0, 0, 0 };

static void tq_insert_sorted(Timer *t)
{
    Timer *p = tq.next;
    while (p != &tq) {
        if (t->rem <= p->rem) {
            p->rem -= t->rem;
            t->next = p; t->prev = p->prev; p->prev->next = t; p->prev = t;   /* FUN_0040b5e0: insert before */
            return;
        }
        t->rem -= p->rem;
        p = p->next;
    }
    t->next = &tq; t->prev = tq.prev; tq.prev->next = t; tq.prev = t;
}

int timer_add(int32_t delay, TimerFn fn, intptr_t a, intptr_t b)
{
    Timer *t = calloc(1, sizeof *t);
    if (!t) return 0;
    t->period = t->rem = delay;
    t->fn = fn; t->a = a; t->b = b;
    tq_insert_sorted(t);
    return 1;
}

int timer_advance(int32_t dt)
{
    int n = 0;
    for (;;) {
        Timer *t = tq.next;
        if (dt < 0 || t == &tq) return n;
        t->rem -= dt;
        if (t->rem > 0) return n;
        dt = -t->rem;
        t->prev->next = t->next; t->next->prev = t->prev;
        int r = t->fn(t->a, t->b, t->period);
        if (r < 0) free(t);
        else {
            if (r == 0) r = t->period; else t->period = r;
            t->rem = r;
            tq_insert_sorted(t);
        }
        n++;
    }
}

void timer_clear(void)
{
    while (tq.next != &tq) { Timer *t = tq.next; tq.next = t->next; free(t); }
    tq.next = tq.prev = &tq;
}

typedef struct Task { struct Task *next; FrameTaskFn fn; intptr_t arg; } Task;
static Task *tasks, **tasks_tail = &tasks;

int frame_task_add(FrameTaskFn fn, intptr_t arg)
{
    Task *t = calloc(1, sizeof *t);
    if (!t) return 0;
    t->fn = fn; t->arg = arg;
    *tasks_tail = t; tasks_tail = &t->next;
    return 1;
}

int frame_tasks_run(void)
{
    int n = 0;
    for (Task **pp = &tasks; *pp; ) {
        Task *t = *pp;
        if (t->fn(t->arg, t)) {
            *pp = t->next;
            if (tasks_tail == &t->next) tasks_tail = pp;
            free(t);
        } else pp = &t->next;
        n++;
    }
    return n;
}

void frame_tasks_clear(void)
{
    while (tasks) { Task *t = tasks; tasks = t->next; free(t); }
    tasks_tail = &tasks;
}

/* ---- class names from RFIRE.BIN (class descriptor VA -> +0x04 name) ---- */
static void obj_class_names_load(void)
{
    static const struct { const ObjClass *c; uint32_t va; } cls[] = {
        { &class_man, 0x43fb10 },
        { &class_sub, 0x43fc10 },
        { &class_storage_up, 0x443288 },
        { &class_storage_down, 0x4432d8 },
        { &class_expl, 0x44aa50 },
        { &class_turret, 0x44ad40 },
        { &class_turret_large, 0x44ad90 },
        { &class_gate, 0x44ade0 },
        { &class_flag, 0x44ae30 },
        { &class_wreck, 0x44b0d8 },
        { &class_vehicle, 0x44b128 },
        { &class_missle, 0x450950 },
        { &class_tracer, 0x4509a0 },
        { &class_death_missle, 0x4509f8 },
        { &class_grenade, 0x450d48 },
        { &class_fwall, 0x454f48 },
        { &class_shadow, 0x454fb8 },
        { &class_mine, 0x455008 },
        { &class_drone, 0x455778 },
        { &class_stay, 0x455820 },
    };
    for (size_t i = 0; i < sizeof cls / sizeof *cls; i++)
        snprintf((char *)cls[i].c->name, OBJ_CLASS_NAME_MAX, "%s", exe_str(exe_u32(cls[i].va + 4)));
}
EXE_LOADER(obj_class_names_load)
