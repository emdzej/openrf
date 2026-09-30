#!/usr/bin/env python3
"""Reference renderer for the Return Fire world view (RenderWorldView 0x419dd0).

Reproduces, in integer fixed point, what the game draws for one player view:
  * camera/projection maths (ViewInit 0x403750, Camera_Update 0x403a40, InitProjectionAndRotTables
    0x408440, ProjectVertices 0x401290),
  * the far->near floor walk emitting one trapezoid cel per cell (mode 0xc / 0x13),
  * the per-cell model queue, depth key and insertion sort (View_QueueModel 0x4085a0),
  * the model draw functions (Model_DrawStatic/Yaw/YawPitch + the building/tower/gate wrappers),
  * the cel rasterisers (Cel_DrawTrapezoid 0x40fe4f, Cel_DrawQuad 0x410b9c + Cel_ScanEdge 0x410120,
    Cel_DrawShadowSpans 0x411ba5) with TRANS.TBL blend/darken/brighten tables.
See docs/render.md.

Usage:
  python3 tools/view.py MAP.RFM [--at X Y | --pad] [--mode bunker|drive|heli|lift|intro]
                        [--pitch P] [--height H] [--out out/view/name.png] [--scale 2] [--raw fb.raw]
X/Y are world pixels (cell*32+16 = cell centre).  Default: the team-0 home pad, bunker view.
"""
import argparse
import math
import os
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from car import Car          # noqa: E402
import models as M           # noqa: E402

F = 0x12c0000                 # DAT_004438a8: focal length 300.0 (16.16)
TWO_PI_256 = math.pi / 128   # double at 0x43d038 (2*pi/256), bit-identical


def s32(v):
    v &= 0xffffffff
    return v - 0x100000000 if v & 0x80000000 else v


def cdiv(a, b):
    """C integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def fixmul(a, b):          # FixMul 0x438300: imul; shrd 16 (floor)
    return s32((a * b) >> 16)


def fixdiv(a, b):          # FixDiv 0x438390: (a/65536)/(b/65536)*65536, _ftol (trunc)
    return s32(int((a * (1 / 65536)) / (b * (1 / 65536)) * 65536.0))


def cosfixed(a):           # CosFixed 0x438360: full circle = 0x1000000
    return int(math.cos(a * (1 / 65536) * TWO_PI_256) * 65536.0)


def sinfixed(a):           # SinFixed 0x438520
    return int(math.sin(a * (1 / 65536) * TWO_PI_256) * 65536.0)


def ptab(i):
    """PTR_DAT_00440c30[i] = 0x12c0000 / (300 - i), i = -1024..511 (0 at i=300)."""
    d = 300 - i
    return 0 if d == 0 else cdiv(F, d)


def mat_mul(a, b):         # Mat_Mul3x3 0x438460: out = a*b, FixMul per term
    return [fixmul(a[r * 3], b[c]) + fixmul(a[r * 3 + 1], b[3 + c]) + fixmul(a[r * 3 + 2], b[6 + c])
            for r in range(3) for c in range(3)]


def vec_mul(v, m):         # VecMulMat3 0x42bb00 / Mat_TransformVerts 0x4383c0: row vector * m
    return [fixmul(v[0], m[c]) + fixmul(v[1], m[3 + c]) + fixmul(v[2], m[6 + c]) for c in range(3)]


# 64-step yaw (0x481780) and pitch (0x48a7b0) tables, 9 ints each
YAW = []
PITCH = []
for _i in range(64):
    _a = _i * 0x40000
    _c, _s = cosfixed(_a), sinfixed(_a)
    YAW.append([_c, _s, 0, -_s, _c, 0, 0, 0, 0x10000])
    PITCH.append([0x10000, 0, 0, 0, _c, _s, 0, -_s, _c])


# ----------------------------------------------------------------------------------------------
# World (LoadLevelMap 0x4322f0 + SetCellStaticObject 0x417850 + jitter table 0x41f190)
# ----------------------------------------------------------------------------------------------
class MSRand:
    def __init__(self, seed):
        self.s = seed & 0xffffffff

    def rand(self):
        self.s = (self.s * 214013 + 2531011) & 0xffffffff
        return (self.s >> 16) & 0x7fff

    def range(self, n):        # RandRange 0x4335f0
        return ((self.rand() & 0x7fff) * 2 * n) >> 16


class World:
    def __init__(self, pe, path):
        d = open(path, 'rb').read()
        assert d[:4] == b'WRL\0'
        w, h = struct.unpack_from('<HH', d, 8)
        size, off = struct.unpack_from('<II', d, 0x44)
        tiles = d[off:off + size]
        self.pe = pe
        self.bno = M.bno_table(pe)
        tt = pe.rd(0x452858, 240 * 4)
        cells = [2] * 16384
        ox, oy = (128 - w) >> 1, (128 - h) >> 1
        seed = 0
        self.pads = {0: [], 1: []}
        for y in range(h):
            for x in range(w):
                b = tiles[y * w + x]
                seed += b
                if b > 0xef:
                    b = 0
                i = (y + oy) * 128 + x + ox
                ter, obj, team, hnd = tt[b * 4:b * 4 + 4]
                if ter == 0xff:
                    cells[i] &= ~0x7f
                    continue
                cells[i] = (cells[i] & ~0x7f) | ter
                if obj:
                    cells[i] = self.set_static(cells[i], obj, 0, team)
                if hnd == 1:
                    self.pads[team].append((x + ox, y + oy))
        for i in range(16384):          # bridge spans
            if cells[i] & 0x7f == 0x54:
                j = i + 1
                while cells[j] & 0x7f != 0x55:
                    cells[j] = self.set_static(cells[j], 0x4a, 0, (cells[i] & 0xc000) >> 14)
                    j += 1
            if cells[i] & 0x7f == 0x56:
                j = i + 128
                while cells[j] & 0x7f != 0x57:
                    cells[j] = self.set_static(cells[j], 0x4b, 0, (cells[i] & 0xc000) >> 14)
                    j += 128
        self.cells = cells
        # FUN_0041f190(sum of raw tile bytes): 256 x {dx, dy, n, rnd} at 0x472290
        r = MSRand(seed)
        self.jit = []
        for _ in range(256):
            a = r.range(25) - 12
            b = r.range(25) - 12
            c = r.range(11)
            e = r.range(256)
            self.jit.append((a, b, c, e))

    def set_static(self, cell, t, hp, team):
        b = self.bno[t]
        if not b['model'] or t == 0:
            return cell & 0xf1ff0000
        if b['terrain'] != 0xff:
            ter = (b['terrain'] + team) if b['flags'] == 8 else b['terrain']
            cell = (cell & ~0x7f) | (ter & 0x7f)
        cell = (cell & ~0x3f80) | (t << 7)
        if b['strength'] == 0:
            v = 0
        else:
            v = max(1, min(15, hp + b['strength']))
        cell = (cell & ~0xe000000) | ((v << 25) & 0xe000000)
        cell = (cell & ~0xc000) | ((team << 14) & 0xc000)
        return cell

    def jit_by_cell(self, ci):
        """FUN_00417820(cell): &0x472290[(idx & 0x1e0)*2 + (idx & 0xf)*4] (note the odd bit mix)."""
        return self.jit[((ci & 0x1e0) * 2 + (ci & 0xf) * 4) // 4]



# ----------------------------------------------------------------------------------------------
# Camera / view struct
# ----------------------------------------------------------------------------------------------
class View:
    """The render-relevant part of the 0x24c-byte view struct (0x48bd90)."""

    def __init__(self, w=320, h=152, clip_x=0, clip_y=0, pitch=0x180000, height=0):
        self.w, self.h = w, h                         # [2],[3]
        self.clip = (clip_x, clip_y, clip_x + w - 1, clip_y + h - 1)
        self.cx = (w // 2) << 16                      # [4]
        self.cy = (h // 2) << 16                      # [5]
        self.camx = self.camy = 0                     # [6],[7]
        self.camh = height                            # [8]  (0x114 smoothed, target 0x134)
        self.set_pitch(pitch)

    def set_pitch(self, p):
        self.pitch = p                                # [9] = +0x24 (target +0x144)
        c, s = cosfixed(p), sinfixed(p)
        self.m10 = -c                                 # +0x28
        self.m11 = s                                  # +0x2c
        self.mat = [0x10000, 0, 0, 0, s, c, 0, -c, s]  # +0x3c..+0x5c
        self.update_extent()

    def update_extent(self):
        """ViewInit/Camera_Update: [0xd] = world Y of the top screen row, [0xc] = world X of its left end."""
        hh = -self.h >> 1
        top = 0
        if hh != 0:
            t = cdiv(fixmul(F, self.m11), hh)
            top = fixdiv(self.camh + F, t - self.m10)
        inv = fixdiv(self.camh + fixmul(self.m10, top) + F, F)
        self.top = top >> 16                          # [0xd]
        self.left = (inv * (-self.w >> 1)) >> 16      # [0xc]

    def look_at(self, tx, ty, tz=0, zoff=0, win_x=0, win_y=12):
        """Steady state of Camera_Update for a tracker at (tx,ty,tz) (16.16):
        target y is pulled north by (tz+zoff)*cos(pitch); cam = target - screen window (0x1c4/0x1c8)."""
        t128 = tz + zoff
        ty2 = (t128 >> 16) * self.m10 + ty
        self.camx = tx - (win_x << 16)
        self.camy = ty2 - (win_y << 16)


MODES = {   # (pitch, height, z offset) of the trackers created by the game
    'bunker': (0x180000, 0, 0),              # FUN_00404ee0 (bunker vehicle select)
    'drive': (0x180000, -0xaa0000, 0xa0000),  # SpawnVehicle: DAT_0044afb0[tank/jeep/msv] = -170
    'heli': (0x180000, -0x640000, 0xa0000),   # DAT_0044afb0[heli] = -100
    'lift': (0x400000, 0xfa0000, 0xa0000),    # FUN_004181b0: straight down from 250
    'intro': (0x200000, 0x10e0000, 0),        # ViewInit defaults (start of the zoom-in)
}


# ----------------------------------------------------------------------------------------------
# Rasteriser (8-bit framebuffer, cel semantics)
# ----------------------------------------------------------------------------------------------
KEYBLEND = {**{i: (0x7d, 0x7c, 0x7b, 0x7a) for i in range(524, 579)},
            **{i: (0x0a, 0x7a, 0x7c, 0x80) for i in range(452, 457)}}


class Raster:
    def __init__(self, car, trans, W=320, H=240):
        self.car = car
        self.W, self.H = W, H
        self.fb = bytearray(W * H)
        self.blend = trans[4:4 + 0x10000]
        self.darken = [trans[0x10004 + k * 256:0x10004 + k * 256 + 256] for k in range(32)]
        self.bright = trans[0x12004:0x12004 + 0x2000]
        self.clip = (0, 0, W - 1, H - 1)
        self.cels = 0

    # -- pixel ops ---------------------------------------------------------------------------
    def pixop(self, ccb, idx):
        """Return fn(dst_index, src) -> None implementing the cel's draw mode."""
        mode = ccb['pre0']
        if idx in KEYBLEND:
            mode = 0x10
        fb = self.fb
        if mode in (0, 0x13, 0xc):
            if ccb['flags'] & 0x20 and mode == 0:
                def op(o, s):
                    fb[o] = s
            else:
                def op(o, s):
                    if s:
                        fb[o] = s
        elif mode in (1, 2):
            t = self.darken[4 if mode == 1 else 2]

            def op(o, s):
                if s:
                    fb[o] = t[fb[o]]
        elif mode in (3, 4):
            bl = self.blend

            def op(o, s):
                if s:
                    fb[o] = bl[s * 256 + fb[o]]
        elif mode == 5:
            br = self.bright

            def op(o, s):
                if s:
                    fb[o] = br[s * 256 + fb[o]]
        elif mode == 0x10:
            keys = set(KEYBLEND.get(idx, ())) | {0x0b}
            bl = self.blend

            def op(o, s):
                if s:
                    fb[o] = bl[s * 256 + fb[o]] if s in keys else s
        elif mode == 0x11:
            pl = self.car.d[ccb['plut']:ccb['plut'] + 256]

            def op(o, s):
                if s:
                    fb[o] = pl[s] if s < len(pl) else s
        else:
            def op(o, s):
                if s:
                    fb[o] = s
        return op

    # -- Cel_DrawTrapezoid 0x40fe4f (mode 0xc) --------------------------------------------------
    def trapezoid(self, p, idx):
        c = self.car.ccbs[idx]
        w, h = c['width'], c['height']
        src = self.car.d[c['source']:c['source'] + w * h]
        cl, ct, cr, cb = self.clip
        y = p[1]
        n = p[7] - p[1]
        if n <= 0:
            return
        L, R = p[0] << 16, p[2] << 16
        dL = cdiv((p[6] << 16) - (p[0] << 16), n)
        dR = cdiv((p[4] << 16) - (p[2] << 16), n)
        dv = cdiv((h << 16) - 1, n)
        V = 0
        if y < ct:
            k = ct - y
            if k > n:
                return
            L += dL * k; R += dR * k; V = dv * k; n -= k; y = ct
        elif y > cb:
            return
        if cb < y + n:
            n = cb - y
        opaque = bool(c['flags'] & 0x20)
        fb, W = self.fb, self.W
        xr_lim = cr + 1
        while True:
            xl = L >> 16
            span = (R >> 16) - xl
            if span > 0:
                du = cdiv(w << 16, span)
                cnt = None
                if xl < cl:
                    k = cl - xl
                    if k <= span:
                        U = k * du; xl += k; cnt = span - k
                elif xl <= xr_lim:
                    U = 0; cnt = span
                if cnt is not None:
                    if xl + cnt > xr_lim:
                        cnt = xr_lim - xl
                    row = (V >> 16) * w
                    o = y * W + xl
                    for _ in range(max(cnt, 0)):
                        s = src[row + (U >> 16)]
                        U += du
                        if s or opaque:
                            fb[o] = s
                        o += 1
            if n < 1:
                return
            L += dL; R += dR; V += dv; y += 1; n -= 1

    # -- Cel_DrawShadowSpans 0x411ba5 (mode 0xd) ------------------------------------------------
    def shadow_spans(self, p, idx):
        c = self.car.ccbs[idx]
        w, h, sp = c['width'], c['height'], c['source']
        d = self.car.d
        tbl = self.darken[4]
        cl, ct, cr, cb = self.clip
        y = p[1]
        n = p[7] - p[1] + 1
        if n <= 0:
            return
        L, R = p[0] << 16, p[2] << 16
        dL = cdiv((p[6] << 16) - (p[0] << 16), n)
        dR = cdiv((p[4] << 16) - (p[2] << 16), n)
        dv = cdiv((h << 16) - 1, n)
        V = 0
        if y < ct:
            k = ct - y
            if n < k:
                return
            L += dL * k; R += dR * k; V = dv * k; n -= k; y = ct
        elif y > cb:
            return
        if cb < n + y:
            n = cb - y
        fb, W = self.fb, self.W
        while True:
            ro = struct.unpack_from('<h', d, sp + (V >> 16) * 2)[0]
            if ro:
                q = sp + ro
                wid = (R - L) >> 4
                while True:
                    x0 = (((d[q] * wid) >> 4) + L) >> 16
                    if x0 <= cr:
                        x0 = max(x0, cl)
                        x1 = (((d[q + 1] * wid) >> 4) + L) >> 16
                        if x1 >= cl:
                            x1 = min(x1, cr)
                            o = y * W
                            for x in range(x0, x1 + 1):
                                fb[o + x] = tbl[fb[o + x]]
                    q += 2
                    if d[q] == 0:
                        break
            if n < 1:
                return
            L += dL; R += dR; V += dv; y += 1; n -= 1

    # -- Cel_DrawQuad 0x410b9c + Cel_ScanEdge 0x410120 ------------------------------------------
    def quad(self, p, idx):
        c = self.car.ccbs[idx]
        w, h = c['width'], c['height']
        src = self.car.d[c['source']:c['source'] + w * h]
        cl, ct, cr, cb = self.clip
        edges = []
        box = [0x7fffffff, -0x7fffffff]
        W1, H1 = (w - 1) << 16, (h - 1) << 16

        def scan(x1, y1, x2, y2, u1, v1, u2, v2):
            box[0] = min(box[0], x1, x2)
            box[1] = max(box[1], x1, x2)
            du, dv = (u2 - u1) + 1, (v2 - v1) + 1
            if y1 < y2:
                if y2 < ct or cb < y1:
                    return
                k = y1 - y2
                e = dict(dx=cdiv((x1 - x2) << 16, k), du=-cdiv(du, k), dv=-cdiv(dv, k),
                         x=x1 << 16, u=u1, v=v1, y0=y1, y1=min(y2, cb))
            elif y1 == y2:
                if x1 == x2:
                    return
                if y1 < ct or cb < y1:
                    return
                if x1 < x2:
                    e = dict(dx=0x7fffffff, du=0x7fffffff, dv=0, x=x1 << 16, u=u1, v=v1, y0=y1, y1=y1)
                else:
                    e = dict(dx=0x7fffffff, du=0x7fffffff, dv=0, x=x2 << 16, u=u2, v=v2, y0=y1, y1=y1)
            else:
                if y1 < ct or cb < y2:
                    return
                k = y2 - y1
                e = dict(dx=cdiv((x2 - x1) << 16, k), du=cdiv(du, k), dv=cdiv(dv, k),
                         x=x2 << 16, u=u2, v=v2, y0=y2, y1=min(y1, cb))
            e['ky'], e['kx'] = e['y0'], e['x']
            # sorted insert by (start y, start x, dx)
            pos = 0
            while pos < len(edges):
                o = edges[pos]
                if e['ky'] < o['ky']:
                    break
                if e['ky'] == o['ky'] and (o['kx'] > e['kx'] or (o['kx'] == e['kx'] and o['dx'] >= e['dx'])):
                    break
                pos += 1
            edges.insert(pos, e)
            if e['y0'] < ct:
                k = ct - e['y0']
                e['x'] += e['dx'] * k; e['y0'] = ct; e['u'] += e['du'] * k; e['v'] += e['dv'] * k

        scan(p[0], p[1], p[2], p[3], 0, 0, W1, 0)
        scan(p[2], p[3], p[4], p[5], W1, 0, W1, H1)
        scan(p[6], p[7], p[4], p[5], 0, H1, W1, H1)
        scan(p[0], p[1], p[6], p[7], 0, 0, 0, H1)
        if not (cl <= box[1] and box[0] <= cr and edges):
            return
        op = self.pixop(c, idx)
        fb, W = self.fb, self.W

        def spans(y, yend, Le, Re):
            if y > yend:
                return y
            for _ in range(yend - y + 1):
                xl = Le['x'] >> 16
                xr = Re['x'] >> 16
                xe = min(xr, cr)
                n = xr - xl + 1
                if n != 0:
                    dv = cdiv(Re['v'] - Le['v'], n)
                    du = cdiv(Re['u'] - Le['u'], n)
                else:
                    du = dv = 0
                U, V = Le['u'], Le['v']
                if xl < cl:
                    k = cl - xl
                    U += k * du; V += k * dv; xl = cl
                if xl <= xe and 0 <= y < self.H:
                    o = y * W + xl
                    for _x in range(xe - xl + 1):
                        uu, vv = U >> 16, V >> 16
                        if 0 <= uu < w and 0 <= vv < h:
                            op(o, src[vv * w + uu])
                        U += du; V += dv; o += 1
                for e in (Le, Re):
                    e['x'] += e['dx']; e['u'] += e['du']; e['v'] += e['dv']
                y += 1
            return y

        rest = edges[:]
        a = rest.pop(0)
        if not rest:
            return
        b = rest.pop(0)
        if b['kx'] < a['kx']:
            Le, Re = b, a
        else:
            Le, Re = a, b
        y = Le['y0']
        if Le['y0'] != Le['y1'] and Re['y0'] != Re['y1']:
            y = spans(y, rest[0]['y0'] if rest else Re['y1'], Le, Re)
        while rest:
            e = rest.pop(0)
            if e['y0'] == Le['y1']:
                Le = e
            else:
                Re = e
            y = spans(y, rest[0]['y0'] if rest else Re['y1'], Le, Re)

    # -- dispatch (Cel_DrawList 0x4262f0) --------------------------------------------------------
    def draw(self, idx, corners, mode=None):
        """corners: 4 (x,y) in 16.16 screen space (explicit-corner cel)."""
        self.cels += 1
        c = self.car.ccbs[idx]
        m = c['pre0'] if mode is None else mode
        cl, ct = self.clip[0], self.clip[1]
        p = []
        for x, y in corners:
            p += [(x >> 16) + cl, (y >> 16) + ct]
        if m == 0xc:
            self.trapezoid(p, idx)
        elif m == 0xd:
            self.shadow_spans(p, idx)
        elif m in (0, 1, 2, 3, 4, 5, 0x10, 0x11, 0x12, 0x13):
            if m == 0x13:
                c = dict(c, pre0=0x13)
            self.quad(p, idx)


# ----------------------------------------------------------------------------------------------
# Model queue and draw (View_QueueModel / View_DrawQueuedModels / Model_* )
# ----------------------------------------------------------------------------------------------
class Item:
    __slots__ = ('team', 'm', 'pos', 'yaw', 'pitch', 'key', 'cell', 'ci')


class Renderer:
    def __init__(self, pe, models, car, trans, world):
        self.pe, self.models, self.car, self.world = pe, models, car, world
        self.r = Raster(car, trans)
        self.bld = {}   # building face sprite tables

    # ---- queue ------------------------------------------------------------------------------
    def queue_cell(self, v, ci, x, y):
        """View_QueueCellContents 0x419cf0 for a map cell (static BNO only: no live objects)."""
        if ci is None:
            return
        cell = self.world.cells[ci]
        t = (cell & 0x3f80) >> 7
        if t:
            self.queue_model(v, self.world.bno[t]['model'], [(x + 16) << 16, (y + 16) << 16, 0], ci)

    def queue_model(self, v, addr, pos, ci):
        while addr:
            m = self.models.get(addr)
            if m is None or m.special:
                return
            p = list(pos)
            if m.pos_hook == 0x41f210:             # Model_PosJitter
                j = self.world.jit[((p[0] >> 21) & 15) + ((p[1] >> 21) & 15) * 16]   # 0x472290 + xc*4 + yc*64
                p[0] += j[0] << 16
                p[1] += j[1] << 16
            it = Item()
            it.m, it.ci = m, ci
            it.pos = [m.offset[0] - v.camx + p[0], p[1] - v.camy + m.offset[1], 0]
            it.yaw = it.pitch = 0
            cell = self.world.cells[ci]
            skip = False
            if m.depth_hook == 0:
                zb = v.camh - m.zbias
                it.team = (cell & 0xc000) >> 14
            else:
                ret, skip = self.depth_hook(m, it, cell, ci)
                zb = v.camh - ret
            it.pos[2] = 0 if m.flags & 0x10 else p[2] + m.offset[2]
            it.pos = vec_mul(it.pos, v.mat)
            ax = abs(it.pos[0])
            it.key = s32((it.pos[1] >> 8) - it.pos[2] + zb + (ax >> 9))
            it.pos[2] -= v.camh
            if not skip:
                # insertion: walk from the tail while new.key > node.key; insert after (list is
                # in descending key order from the head, drawn head first = far first)
                q = self.queue
                k = len(q)
                while k > 0 and it.key > q[k - 1].key:
                    k -= 1
                q.insert(k, it)
            addr = m.next

    def depth_hook(self, m, it, cell, ci):
        h = m.depth_hook
        team = (cell & 0xc000) >> 14
        if h == 0x4174e0:                           # Model_TowerHook (no live turret object)
            if cell & 0xe000000 == 0:
                return 0, True
            it.team = team
            j = self.world.jit_by_cell(ci)
            it.yaw = (j[3] & 0x3f) << 16
            it.pitch = (j[0] & 0xff) << 14
            return m.zbias, False
        if h == 0x41f380:                           # Model_CellHook
            it.team = team
            return m.zbias, False
        if h == 0x4174a0:                           # Model_HookHide
            return 0, True
        if h == 0x41f4f0:                           # DEST_PLANTER: hidden when team bits == 1
            if cell & 0xc000 == 0x4000:
                return 0, True
        it.team = team
        return m.zbias, False

    # ---- draw -------------------------------------------------------------------------------
    def draw_item(self, v, it):
        m = it.m
        fn = m.fn
        if fn == 0x41f550:
            self.patch_building(it)
        if fn in (0x408840, 0x41f550, 0x41fbe0, 0x4175a0, 0x417610, 0x417680, 0x4176f0,
                  0x41f3b0, 0x41f420, 0x41f470, 0x41f4b0):
            mat = v.mat
        elif fn == 0x408a20:
            mat = mat_mul(YAW[it.yaw >> 16], v.mat) if it.yaw & 0xffff0000 else v.mat
        elif fn == 0x408aa0:
            pit = abs(it.pitch)
            if pit == 0:
                mat = mat_mul(YAW[it.yaw >> 16], v.mat) if it.yaw else v.mat
            else:
                mat = mat_mul(mat_mul(PITCH[(pit >> 16) & 63], YAW[it.yaw >> 16]), v.mat)
        else:
            mat = v.mat
        verts = [vec_mul(p, mat) for p in m.verts]
        self.emit_faces(v, it, verts)
        if fn == 0x41f550:
            self.unpatch_building(it)

    def project(self, v, it, verts):
        """ProjectVertices 0x401290."""
        out = []
        px, py, pz = it.pos
        for vx, vy, vz in verts:
            iz = (vz + pz) >> 16
            if iz > 0x1ff:
                return None
            s = ptab(iz)
            X = (s32(s32(((vx + px) >> 14) * s) & 0xfffe0003) >> 2) + v.cx
            Y = (s32(s32(((vy + py) >> 14) * s) & 0xfffe0003) >> 2) + v.cy
            out.append((s32(X), s32(Y), s))
        return out

    def emit_faces(self, v, it, verts):
        """Model_EmitFaces 0x4088a0."""
        P = self.project(v, it, verts)
        if P is None:
            return
        m = it.m
        faces = self.face_override.get(id(it), m.faces) if hasattr(self, 'face_override') else m.faces
        if m.nfaces < 1:
            order = m.orders[(it.yaw & 0xfff9ffff) >> 19] if m.orders else []
            for fi in order:
                self.emit(P, faces[fi], it)
        else:
            for f in faces:
                fl = f['flags']
                a, b = f['test']
                if (fl & 1) and not (P[a][0] <= P[b][0]):
                    continue
                if (fl & 2) and not (P[a][1] <= P[b][1]):
                    continue
                self.emit(P, f, it)

    def emit(self, P, f, it):
        idx = f['sprite'] + (it.team if f['flags'] & 8 else 0)
        c = f['corners']
        self.r.draw(idx, [(P[k][0], P[k][1]) for k in c])

    # ---- Model_DrawBuilding 0x41f550 ----------------------------------------------------------
    def patch_building(self, it):
        pe, w = self.pe, self.world
        m = it.m
        ci = it.ci
        j = w.jit_by_cell(ci)
        b1 = j[3]
        sb1 = b1 - 256 if b1 & 0x80 else b1
        tab = 0x4478f0 if m.shape == 0x447890 else 0x4478d8
        T = [pe.s32(tab + k * 4) for k in range(6)]
        side = (m.flags & 0xff0000) >> 16
        v8 = (sb1 & 0x60) >> 5
        faces = [dict(f) for f in m.faces]
        faces[4]['sprite'] = pe.s8(0x4478d0 + (sb1 & 7)) + T[0]

        def wall(nci, s):
            if nci is not None and (w.cells[nci] & 0x3f80) == 0x1980:
                return T[1]
            if side == s:
                return T[3] if (b1 & 0x18) == 0x18 else T[2]
            return T[5] if v8 == s else T[4]
        faces[1]['sprite'] = wall(ci - 1, 1) + 2
        faces[2]['sprite'] = wall(ci + 1, 2)
        faces[3]['sprite'] = wall(ci + 128 if ci + 128 < 16384 else None, 3)
        if not hasattr(self, 'face_override'):
            self.face_override = {}
        self.face_override[id(it)] = faces

    def unpatch_building(self, it):
        self.face_override.pop(id(it), None)

    # ---- RenderWorldView 0x419dd0 -------------------------------------------------------------
    def render(self, v):
        r = self.r
        r.clip = v.clip
        self.queue = []
        cells = self.world.cells
        uv5 = (v.camx >> 16) + v.left
        uv2 = (v.camy >> 16) - 0x20 + v.top
        col = uv5 >> 5                                   # local_40
        xrel = v.left - (uv5 & 0x1f)                     # local_3c
        row = uv2 >> 5                                   # local_18
        yrel = (v.top - (uv2 & 0x1f)) - 0x20             # local_20
        i8 = v.m10 * yrel + v.camh
        xs = fixmul(xrel * 0x10000, ptab(-i8 >> 16))     # local_2c
        ys = fixdiv((F >> 16) * v.m11, F + i8) * yrel    # local_30
        tw = fixdiv(F, i8 + F) << 5                      # local_1c
        on = 0                                           # local_10
        ypix = row << 5                                  # local_24
        rbase = row << 7                                 # local_14
        while True:
            top_y = ys
            if v.cy <= ys:
                if on == 0:
                    break
                on = 0
            xc = xs
            tw_top = tw
            if on:
                while tw + xc < -0x20 - v.cx:
                    col += 1
                    xrel += 0x20
                    xc += tw
            cx_ = col - 1
            rowok = 0 <= rbase <= 0x3fff
            cell = rbase + cx_ if (rowok and 0 <= cx_ <= 0x7f) else None
            self.queue_cell(v, cell, cx_ * 0x20, ypix)
            cx_ += 1
            if cell is None:
                cell = rbase + cx_ if (rowok and 0 <= cx_ < 0x80) else None
            else:
                cell += 1
                if cx_ > 0x7f:
                    cell = None
            yrel += 0x20
            i6 = v.m10 * yrel + v.camh
            xs = fixmul(xrel << 16, ptab(-i6 >> 16))
            ybot = fixdiv((F >> 16) * v.m11, F + i6) * yrel
            ys = ybot
            tw = fixdiv(F, i6 + F) << 5
            xc = xc + v.cx
            xb = v.cx + xs
            c_top = xc & ~0x7fff
            c_bot = xb & ~0x7fff
            y_top = top_y + v.cy
            y_bot = v.cy + ybot
            if xc < v.w * 0x10000:
                px = cx_ << 5
                while True:
                    self.queue_cell(v, cell, px, ypix)
                    xc += tw_top
                    if on:
                        l_top, l_bot = c_top, c_bot
                        c_top = xc & ~0x7fff
                        xb += tw
                        c_bot = xb & ~0x7fff
                        t = 2 if cell is None else cells[cell] & 0x7f
                        mode = 0xc
                        if cell is not None and (t in (1, 2) or 3 < t < 0x34):
                            mode = 0x13
                        r.draw(t, [(l_top, y_top), (c_top, y_top), (c_bot, y_bot), (l_bot, y_bot)], mode)
                    px += 0x20
                    cx_ += 1
                    if cell is None:
                        if rowok and 0 <= px < 0x1000:
                            cell = rbase + cx_
                    else:
                        cell += 1
                        if px > 0xfff:
                            cell = None
                    if not (xc < v.w * 0x10000):
                        break
            self.queue_cell(v, cell, cx_ << 5, ypix)
            row += 1
            ypix += 0x20
            rbase += 0x80
            if ybot < v.cy:
                on = 1
        for it in self.queue:
            self.draw_item(v, it)
        return r


def load_trans(car):
    p = os.path.join(ROOT, 'cd/ART/TRANS.TBL')
    t = open(p, 'rb').read()
    assert len(t) == 0x14004 and t[0] == 1
    return t


def to_png(r, car, path, scale=1, statusbar=True, crop=None):
    pal = car.pal
    im = Image.frombytes('P', (r.W, r.H), bytes(r.fb))
    flat = []
    for c in pal:
        flat += list(c)
    im.putpalette(flat)
    im = im.convert('RGB')
    if statusbar:
        sb = os.path.join(ROOT, 'cd/ART/1PBSCRL.RFA')
        if os.path.exists(sb):
            im.paste(Image.open(sb).convert('RGB'), (0, 152))
    if crop:
        im = im.crop(crop)
    if scale != 1:
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('map')
    ap.add_argument('--at', nargs=2, type=float, help='world pixel x y of the tracked point')
    ap.add_argument('--cell', nargs=2, type=int, help='cell x y of the tracked point')
    ap.add_argument('--mode', default='bunker', choices=sorted(MODES))
    ap.add_argument('--pitch', type=lambda s: int(s, 0))
    ap.add_argument('--height', type=float, help='camera height [8] in pixels')
    ap.add_argument('--out')
    ap.add_argument('--scale', type=int, default=2)
    ap.add_argument('--raw', help='also write the 320x240 8-bit index buffer (DirectDraw palette) here')
    a = ap.parse_args()
    pe, models = M.load()
    car = Car(open(os.path.join(ROOT, 'cd/ART/ART.CAR'), 'rb').read())
    world = World(pe, a.map)
    pitch, height, zoff = MODES[a.mode]
    if a.pitch is not None:
        pitch = a.pitch
    if a.height is not None:
        height = int(a.height * 65536)
    v = View(320, 152, pitch=pitch, height=height)
    if a.at:
        tx, ty = int(a.at[0] * 65536), int(a.at[1] * 65536)
    else:
        cx, cy = a.cell if a.cell else world.pads[0][0]
        tx, ty = (cx * 32 + 16) << 16, (cy * 32 + 16) << 16
    v.look_at(tx, ty, 0, zoff)
    ren = Renderer(pe, models, car, load_trans(car), world)
    ren.render(v)
    out = a.out or os.path.join(ROOT, 'out/view', '%s_%s.png' % (os.path.splitext(os.path.basename(a.map))[0], a.mode))
    to_png(ren.r, car, out, a.scale)
    if a.raw:
        open(a.raw, 'wb').write(bytes(ren.r.fb))
    print('%s: cam=(%.1f,%.1f) pitch=0x%x height=%.1f top=%d left=%d cels=%d models=%d -> %s' % (
        os.path.basename(a.map), v.camx / 65536, v.camy / 65536, pitch, height / 65536, v.top, v.left,
        ren.r.cels, len(ren.queue), out))


if __name__ == '__main__':
    main()
