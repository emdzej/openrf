#!/usr/bin/env python3
"""Extract every 3D model definition from the Return Fire game binary.

The PC port keeps the 3DO engine's "model" structs in .data.  A model is a linked list of
*parts*; each part is queued and depth-sorted on its own (View_QueueModel 0x4085a0).
Part layout (see docs/render.md for the full description):

  +0x00 draw fn       Model_DrawStatic 0x408840 / Model_DrawYaw 0x408a20 / Model_DrawYawPitch
                      0x408aa0 / Model_DrawTurret 0x408b80 / Model_DrawTilted 0x408d40, or a
                      wrapper that patches faces/verts and then calls one of those
  +0x04 next part
  +0x08 collision shape pointer (not used by the renderer)
  +0x0c radius-ish size (16.16, not used by the renderer)
  +0x10 flags: 0x10 = ignore the object's z (part sits on the ground); byte 2 = building door side
  +0x14 +0x18 +0x1c   offset of the part origin from the object/cell origin (16.16 px)
  +0x20 z-bias (16.16) used by the depth key
  +0x24 depth hook  fn(item, cell, obj) -> z-bias; may set DAT_0045f398 (-1 = do not draw)
  +0x28 position hook fn(def, pos*) (0x41f210 = per-cell random jitter)
  +0x2c nverts, +0x30 verts (3 x s32 16.16: x east, y south, z up)
  +0x34 nfaces (>0: face array with visibility tests; <=0: use per-yaw face order lists)
  +0x38 faces (0x20 bytes: sprite, flags, testA, testB, c0, c1, c2, c3)
  +0x3c nfaces>0: cached camera pitch for the vertex cache (init 0x7fff)
        nfaces<=0: 8 pointers to byte lists of face indices (terminated by a negative byte),
                   chosen by yaw>>19 (8 x 45 degrees)
  +0x40 nfaces>0: vertex cache buffer (transformed verts, reused while pitch is unchanged)

Usage: python3 tools/models.py [--exe cd/RFIRE.BIN] [--car cd/ART/ART.CAR] [--out out/models]
Writes out/models/models.json.
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rfexe  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE_BASE = 0x400000

DRAW_FN_NAMES = {
    0x408840: 'Model_DrawStatic', 0x408a20: 'Model_DrawYaw', 0x408aa0: 'Model_DrawYawPitch',
    0x408b80: 'Model_DrawTurret', 0x408d40: 'Model_DrawTilted',
    # wrappers (patch faces / verts, then call one of the above)
    0x41f550: 'Model_DrawBuilding', 0x41fbe0: 'Model_DrawAmmo',
    0x4175a0: 'Model_DrawGateH_W', 0x417610: 'Model_DrawGateH_E',
    0x417680: 'Model_DrawGateV_N', 0x4176f0: 'Model_DrawGateV_S',
    0x41f3b0: 'Model_DrawDestBush', 0x41f420: 'Model_DrawDestTree',
    0x41f470: 'Model_DrawDestWTower', 0x41f4b0: 'Model_DrawDestFuel',
    0x417d70: 'Model_DrawStorage', 0x417e40: 'Model_DrawStorageLift', 0x417f10: 'Model_DrawStoragePad',
    0x4061c0: 'Model_DrawMan', 0x406280: 'Model_DrawManSwim', 0x407320: 'Model_DrawSub',
    0x42f200: 'Model_DrawTank', 0x42f300: 'Model_DrawMSV', 0x42f400: 'Model_DrawJeep',
    0x42f660: 'Model_DrawJeepShadow', 0x42f6e0: 'Model_DrawFlagCarried', 0x42f740: 'Model_DrawFlag',
    0x42f860: 'Model_DrawHeliShadow', 0x42f990: 'Model_DrawHeli', 0x42fb10: 'Model_DrawRotorA',
    0x42fba0: 'Model_DrawRotorB', 0x435090: 'Model_DrawDrone', 0x435120: 'Model_DrawDroneShadow',
}
HOOK_NAMES = {
    0x41f210: 'Model_PosJitter', 0x4174e0: 'Model_TowerHook', 0x41f380: 'Model_CellHook',
    0x4174a0: 'Model_HookHide', 0x41f4f0: 'Model_HookDestPlanter', 0x41f6f0: 'Model_HookDestTent',
    0x41f770: 'Model_HookStdDest', 0x41f950: 'Model_HookBridgeDest', 0x417d20: 'Model_HookStorage',
    0x41f800: 'Model_HookObjAnim', 0x41f830: 'Model_HookObjVariant',
}
# Object class descriptors (class+0x14 = default model), see docs/architecture.md section 6
CLASSES = {
    0x450950: 'Missle', 0x44b128: 'Vehicle', 0x44ad40: 'Turret Gun', 0x44ad90: 'Turret Gun (large)',
    0x454f48: 'FWall', 0x454fb8: 'Shadow', 0x443288: 'Storage (rising)', 0x4432d8: 'Storage (lowering)',
    0x44b0d8: 'Destroyed Vehicle', 0x455778: 'Drone', 0x455008: 'Mine', 0x44aa50: 'Expl',
    0x44ae30: 'Flag', 0x44ade0: 'Gate', 0x43fb10: 'MAN', 0x43fc10: 'SUB', 0x4509f8: 'Death Missle',
    0x455820: 'Stay', 0x450d48: 'Grenade', 0x4509a0: 'TRACER',
}
VEHICLES = {0x44b3a8: 'Tank', 0x44b690: 'Jeep', 0x44b978: 'MSV', 0x44bc60: 'Heli'}
VEH_FIELDS = {0x52: 'body', 0x53: 'shadow', 0x55: 'part 0x55', 0x58: 'wreck A', 0x59: 'wreck B'}
PROJ_TABLE, PROJ_COUNT, PROJ_STRIDE = 0x450a78, 12, 0x3c
# models only referenced from code (immediate operands), function names from Ghidra
CODE_USERS = {
    0x43f900: 'MAN walk (ManUpdate)', 0x43f998: 'MAN swim (ManUpdate)', 0x43fa30: 'MAN misc (0x4064d0/0x406550)',
    0x4418a0: 'large turret (TurretInit)', 0x4428a8: 'gate H (SpawnGate)', 0x442d70: 'gate V (SpawnGate)',
    0x449478: 'mine (MineUpdate)', 0x44ccb0: 'tank turret (Model_DrawTank)', 0x44dfb0: 'jeep (Model_DrawJeep)',
    0x44e110: 'jeep part (0x42f5c0)', 0x44e610: 'carried flag (FlagUpdate)', 0x44e740: 'flag (FlagUpdate)',
    0x44e9b8: 'heli shadow (0x417e40/Model_DrawHeliShadow)', 0x44ee68: 'heli (lift/dock)',
    0x44f058: 'rotor A (0x42fb10)', 0x44f188: 'rotor B (0x42fba0)', 0x44f1d0: 'rotor (lift, 0x42b5e0)',
    0x452dc8: 'explosion part (0x41fd90)', 0x455730: 'drone shadow (SpawnDrone)',
    0x449e20: 'bridge debris (0x41f870)', 0x449e68: 'bridge debris (0x41f870)', 0x449eb0: 'bridge debris (0x41f870)',
    0x449ef8: 'bridge debris (0x41f870)', 0x449f40: 'bridge debris (0x41f870)', 0x449f88: 'bridge debris (0x41f870)',
}


class PE(rfexe.Exe):
    """RFIRE.BIN reader (tools/rfexe.py); unmapped addresses read as zero here."""

    def rd(self, a, n):
        try:
            return super().rd(a, n)
        except ValueError:
            return bytes(n)


def bno_table(pe):
    out = []
    for i in range(91):
        b = 0x451438 + i * 0x38
        out.append(dict(id=i, name=pe.cstr(pe.u32(b + 4)), model=pe.u32(b + 8), flags=pe.u32(b + 0xc),
                        terrain=pe.rd(b + 0x10, 1)[0], strength=pe.rd(b + 0x11, 1)[0],
                        dest_terrain=pe.rd(b + 0x30, 1)[0], dest_bno=pe.rd(b + 0x31, 1)[0],
                        radar=pe.u32(b + 0x34)))
    return out


def draw_fns():
    return set(DRAW_FN_NAMES)


class Model:
    """One model part (the struct at `addr`)."""

    def __init__(self, pe, addr):
        u, s = pe.u32, pe.s32
        self.addr = addr
        self.fn = u(addr)
        self.next = u(addr + 4)
        self.shape = u(addr + 8)
        self.size = s(addr + 0xc)
        self.flags = u(addr + 0x10)
        self.offset = [s(addr + 0x14), s(addr + 0x18), s(addr + 0x1c)]
        self.zbias = s(addr + 0x20)
        self.depth_hook = u(addr + 0x24)
        self.pos_hook = u(addr + 0x28)
        self.nverts = s(addr + 0x2c)
        self.special = not (0 <= self.nverts <= 256 and (self.nverts == 0 or pe.is_data(u(addr + 0x30))))
        self.verts, self.faces, self.orders = [], [], None
        if self.special or self.nverts == 0:
            self.special = True
            self.nfaces = 0
            return
        vp = u(addr + 0x30)
        self.verts = [[s(vp + i * 12 + k * 4) for k in range(3)] for i in range(self.nverts)]
        self.nfaces = s(addr + 0x34)
        fp = u(addr + 0x38)
        self.faces_addr = fp
        if self.nfaces > 0:
            nf = self.nfaces
        else:
            # per-yaw face orders: 8 pointers at +0x3c, byte lists terminated by a negative byte
            self.orders = []
            for k in range(8):
                p = u(addr + 0x3c + k * 4)
                lst = []
                while p and len(lst) < 64:
                    b = pe.s8(p)
                    if b < 0:
                        break
                    lst.append(b)
                    p += 1
                self.orders.append(lst)
            nf = max((max(o) for o in self.orders if o), default=-1) + 1
        for i in range(nf):
            f = [s(fp + i * 32 + k * 4) for k in range(8)]
            self.faces.append(dict(sprite=f[0], flags=f[1], test=[f[2], f[3]], corners=f[4:8]))

    def to_json(self, car=None):
        fx = lambda v: round(v / 65536.0, 5)
        j = dict(addr='0x%06x' % self.addr, draw_fn=DRAW_FN_NAMES.get(self.fn, '0x%06x' % self.fn),
                 next='0x%06x' % self.next if self.next else None,
                 flags='0x%x' % self.flags, offset=[fx(v) for v in self.offset], zbias=fx(self.zbias),
                 depth_hook=HOOK_NAMES.get(self.depth_hook, '0x%06x' % self.depth_hook) if self.depth_hook else None,
                 pos_hook=HOOK_NAMES.get(self.pos_hook, '0x%06x' % self.pos_hook) if self.pos_hook else None,
                 shape='0x%06x' % self.shape if self.shape else None, size=fx(self.size))
        if self.special:
            j['special'] = True
            return j
        j['verts'] = [[fx(c) for c in v] for v in self.verts]
        j['nfaces_field'] = self.nfaces
        faces = []
        for f in self.faces:
            fj = dict(f)
            fl = f['flags']
            fj['flags'] = '0x%x' % fl
            fj['vis'] = ('x' if fl & 1 else '') + ('y' if fl & 2 else '') or 'always'
            fj['team_offset'] = bool(fl & 8)
            fj['debris_class'] = (fl >> 8) & 3
            if car is not None and 0 <= f['sprite'] < len(car.ccbs):
                c = car.ccbs[f['sprite']]
                fj['sprite_mode'] = c['pre0']
                fj['sprite_size'] = [c['width'], c['height']]
            faces.append(fj)
        j['faces'] = faces
        if self.orders is not None:
            j['orders_by_yaw8'] = self.orders
        return j


def find_models(pe):
    """Scan .data for every struct whose +0 is a known model draw function."""
    fns = draw_fns()
    _, va, vs, _, _ = pe.sec('.data')
    out = {}
    for a in range(va, va + vs - 0x44, 4):
        if pe.u32(a) in fns:
            nv = pe.s32(a + 0x2c)
            nf = pe.s32(a + 0x34)
            if 0 <= nv <= 256 and -1 <= nf <= 256:
                out[a] = Model(pe, a)
    return out


def users(pe, models):
    """Who references each model: BNO types, classes, vehicle defs, projectile types, code."""
    use = {a: [] for a in models}
    for b in bno_table(pe):
        m, part = b['model'], 0
        while m and m in models:
            use[m].append('BNO %d %s%s' % (b['id'], b['name'], '' if part == 0 else ' (part %d)' % part))
            m, part = models[m].next, part + 1
    for c, n in CLASSES.items():
        m = pe.u32(c + 0x14)
        if m in use:
            use[m].append('class %s (0x%06x+0x14)' % (n, c))
    for v, n in VEHICLES.items():
        for k, fname in VEH_FIELDS.items():
            m = pe.u32(v + k * 4)
            if m in use:
                use[m].append('vehicle %s def[0x%x] %s' % (n, k, fname))
    for i in range(PROJ_COUNT):
        e = PROJ_TABLE + i * PROJ_STRIDE
        for o, what in ((0x2c, 'model'), (0x30, 'shadow')):
            m = pe.u32(e + o)
            if m in use:
                use[m].append('projectile type %d %s (+0x%x)' % (i, what, o))
    for a, n in CODE_USERS.items():
        if a in use:
            use[a].append('code: ' + n)
    # parts reached through another model's next pointer
    for a, m in models.items():
        if m.next in use:
            use[m.next].append('next part of 0x%06x' % a)
    return use


def load(exe=None):
    pe = PE(exe or rfexe.find_exe())
    return pe, find_models(pe)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default=None, help='RFIRE.BIN (default: tools/rfexe.py lookup)')
    ap.add_argument('--car', default=os.path.join(ROOT, 'cd/ART/ART.CAR'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'out/models'))
    a = ap.parse_args()
    pe, models = load(a.exe)
    car = None
    if os.path.exists(a.car):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from car import Car
        car = Car(open(a.car, 'rb').read())
    use = users(pe, models)
    js = []
    for addr in sorted(models):
        j = models[addr].to_json(car)
        j['used_by'] = use[addr]
        js.append(j)
    bno = []
    for b in bno_table(pe):
        parts, m = [], b['model']
        while m:
            parts.append('0x%06x' % m)
            m = models[m].next if m in models else 0
        bno.append(dict(id=b['id'], name=b['name'], parts=parts, terrain_override=b['terrain'],
                        strength=b['strength'], flags='0x%x' % b['flags']))
    classes = {n: '0x%06x' % pe.u32(c + 0x14) for c, n in CLASSES.items()}
    os.makedirs(a.out, exist_ok=True)
    json.dump(dict(models=js, bno=bno, classes=classes), open(os.path.join(a.out, 'models.json'), 'w'), indent=1)
    real = [m for m in models.values() if not m.special]
    print('%d model parts (%d with geometry, %d special), %d faces, %d verts -> %s' % (
        len(models), len(real), len(models) - len(real), sum(len(m.faces) for m in real),
        sum(m.nverts for m in real), os.path.join(a.out, 'models.json')))
    unref = [hex(k) for k, v in use.items() if not v]
    if unref:
        print('unreferenced:', ' '.join(unref))


if __name__ == '__main__':
    main()
