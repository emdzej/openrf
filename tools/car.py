#!/usr/bin/env python3
"""Decode Return Fire (Win95) ART/ART.CAR sprite archive ("CCBA") and ART/TRANS.TBL.

ART.CAR is a relocatable dump of 3DO-style Cel Control Blocks (CCBs) plus pixel
data, converted by the PC port to 8-bit indexed pixels.  See docs/car.md.

Usage:
  python3 tools/car.py [--car cd/ART/ART.CAR] [--out out/car] [--raw] [--trans cd/ART/TRANS.TBL]

Writes out/car/NNNN.png (RGBA), out/car/sheet.png (labelled contact sheet),
out/car/index.json (per-sprite metadata), and out/car/palette.png.
"""
import argparse
import json
import os
import struct
import sys

from PIL import Image, ImageDraw

CCB_SIZE = 0x44
HDR_SIZE = 0x10

# Windows 20 static colours (slots 0-9, 246-255 of the DirectDraw palette are
# PC_RESERVED and hold these at runtime; they are zero on disk).
WIN_STATIC_LO = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128),
                 (128, 0, 128), (0, 128, 128), (192, 192, 192), (192, 220, 192), (166, 202, 240)]
WIN_STATIC_HI = [(255, 251, 240), (160, 160, 164), (128, 128, 128), (255, 0, 0), (0, 255, 0),
                 (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]

# PC-port draw modes, stored in CCB.PRE0 (see FUN_004262f0 dispatcher)
MODE_NAMES = {
    0: 'normal',          # colour 0 transparent (opaque if flags&0x20 BGND)
    1: 'shade27',         # dest = darken[4][dest] where src!=0  (shadow)
    2: 'shade29',         # dest = darken[2][dest] where src!=0
    3: 'blend50',         # dest = blend[src][dest]
    4: 'blend50',
    5: 'lightmask',       # dest = brighten[src][dest]  (src = level 0..31, +3*(src+1) RGB)
    0xd: 'shadowspans',   # span-list shadow mask, dest = darken[4][dest]
    0x10: 'keyblend',     # set at runtime: src in PRE1 bytes (+0x0b) blended, else opaque
    0x11: 'plutremap',    # dest = PLUT[src]
    0x12: 'plutremapblend',
}

# CCB ranges patched by the loader FUN_004095f0 at runtime
RUNTIME_KEYBLEND = {**{i: bytes([0x7d, 0x7c, 0x7b, 0x7a]) for i in range(524, 579)},
                    **{i: bytes([0x0a, 0x7a, 0x7c, 0x80]) for i in range(452, 457)}}
RUNTIME_SURFACES = set(range(1981, 1987))  # SOURCEPTR redirected to a 128x128 runtime buffer


class Car:
    def __init__(self, data):
        self.d = data
        magic, self.size, self.count, self.shift_off = struct.unpack_from('<4sIII', data, 0)
        if magic != b'CCBA':
            raise ValueError('bad magic %r' % magic)
        if self.size != len(data):
            print('warning: header size %d != file size %d' % (self.size, len(data)), file=sys.stderr)
        self.ccbs = []
        for i in range(self.count):
            f = struct.unpack_from('<IIIIiiiiiiiiIIIII', data, HDR_SIZE + i * CCB_SIZE)
            c = dict(zip(('flags', 'next', 'source', 'plut', 'xpos', 'ypos', 'hdx', 'hdy', 'vdx',
                          'vdy', 'hddx', 'hddy', 'pixc', 'pre0', 'pre1', 'width', 'height'), f))
            c['wshift'], c['hshift'] = struct.unpack_from('<ii', data, self.shift_off + i * 8)
            self.ccbs.append(c)
        # Palette block: pointed to by every normal CCB's PLUT.
        self.pal_off = self.ccbs[0]['plut']
        self.bgrx = [tuple(data[self.pal_off + i * 4 + k] for k in (2, 1, 0)) for i in range(256)]
        lp = self.pal_off + 0x400
        ver, num = struct.unpack_from('<HH', data, lp)
        assert ver == 0x300 and num == 256, (ver, num)
        self.logpal_flags = [data[lp + 4 + i * 4 + 3] for i in range(256)]
        pal = [tuple(data[lp + 4 + i * 4:lp + 7 + i * 4]) for i in range(256)]
        for i in range(10):
            pal[i] = WIN_STATIC_LO[i]
            pal[246 + i] = WIN_STATIC_HI[i]
        self.pal = pal  # DirectDraw palette; sprite pixel values index this

    def pixels(self, i):
        c = self.ccbs[i]
        return self.d[c['source']:c['source'] + c['width'] * c['height']]

    def spans(self, i):
        """Mode 0xd shadow mask -> list of (row, x0, x1) in sprite pixels (inclusive)."""
        c = self.ccbs[i]
        s, w, h = c['source'], c['width'], c['height']
        offs = struct.unpack_from('<%dh' % h, self.d, s)
        out = []
        for r, o in enumerate(offs):
            if not o:
                continue
            p = s + o
            while True:
                x0, x1 = self.d[p], self.d[p + 1]
                out.append((r, x0 * w >> 8, x1 * w >> 8))
                p += 2
                if self.d[p] == 0:
                    break
        return out

    def render(self, i, raw=False):
        c = self.ccbs[i]
        w, h, mode = c['width'], c['height'], c['pre0']
        if mode == 0xd and not raw:
            im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
            px = im.load()
            for r, x0, x1 in self.spans(i):
                for x in range(x0, min(x1, w - 1) + 1):
                    px[x, r] = (0, 0, 0, 128)
            return im
        src = self.pixels(i)
        if mode == 0x11 and not raw:
            plut = self.d[c['plut']:c['plut'] + 16]
            src = bytes(plut[v] if v < 16 else v for v in src)
        opaque = bool(c['flags'] & 0x20) and mode == 0
        alpha = 255
        if not raw:
            alpha = {1: 96, 2: 64, 3: 160, 4: 160}.get(mode, 255)
        buf = bytearray()
        for v in src:
            if v == 0 and not opaque:
                buf += b'\0\0\0\0'
            elif not raw and mode == 5:   # light mask: value = brighten level (+3*(v+1) RGB)
                buf += bytes((255, 255, 255, min(255, 8 * (v + 1))))
            elif not raw and mode in (1, 2):
                buf += bytes((0, 0, 0, alpha))
            else:
                buf += bytes(self.pal[v]) + bytes((alpha,))
        return Image.frombytes('RGBA', (w, h), bytes(buf))


def describe_trans(path):
    t = open(path, 'rb').read()
    ok = len(t) == 0x14004 and t[0] == 1
    return {'size': len(t), 'valid_flag': t[0], 'ok': ok,
            'blend': (4, 0x10000), 'darken': (0x10004, 0x2000), 'brighten': (0x12004, 0x2000)}


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument('--car', default=os.path.join(root, 'cd/ART/ART.CAR'))
    ap.add_argument('--trans', default=os.path.join(root, 'cd/ART/TRANS.TBL'))
    ap.add_argument('--out', default=os.path.join(root, 'out/car'))
    ap.add_argument('--raw', action='store_true', help='ignore draw modes, dump raw palette colours')
    a = ap.parse_args()

    car = Car(open(a.car, 'rb').read())
    os.makedirs(a.out, exist_ok=True)

    seen = {}
    meta = []
    images = []
    modes = {}
    for i, c in enumerate(car.ccbs):
        key = (c['source'], c['width'], c['height'])
        alias = seen.setdefault(key, i)
        im = car.render(i, a.raw)
        im.save(os.path.join(a.out, '%04d.png' % i))
        images.append(im)
        m = dict(index=i, width=c['width'], height=c['height'], mode=c['pre0'],
                 mode_name=MODE_NAMES.get(c['pre0'], '?'), flags='0x%08x' % c['flags'],
                 bgnd=bool(c['flags'] & 0x20), pixc='0x%08x' % c['pixc'],
                 source='0x%x' % c['source'], plut='0x%x' % c['plut'],
                 shift=[c['wshift'], c['hshift']])
        if alias != i:
            m['alias_of'] = alias
        if i in RUNTIME_KEYBLEND:
            m['runtime_mode'] = 0x10
            m['runtime_pre1'] = RUNTIME_KEYBLEND[i].hex()
        if i in RUNTIME_SURFACES:
            m['runtime_surface'] = True
        meta.append(m)
        modes[m['mode_name']] = modes.get(m['mode_name'], 0) + 1
    json.dump(meta, open(os.path.join(a.out, 'index.json'), 'w'), indent=1)

    # palette swatch
    pim = Image.new('RGB', (16 * 12, 16 * 12))
    dr = ImageDraw.Draw(pim)
    for k, col in enumerate(car.pal):
        dr.rectangle([(k % 16) * 12, (k // 16) * 12, (k % 16) * 12 + 11, (k // 16) * 12 + 11], fill=col)
    pim.save(os.path.join(a.out, 'palette.png'))

    # contact sheet with index labels
    W, pad, lab = 1600, 3, 9
    x = y = rowh = 0
    pos = []
    for im in images:
        cw = max(im.width, 24)
        if x + cw > W:
            x, y, rowh = 0, y + rowh + lab + pad, 0
        pos.append((x, y))
        x += cw + pad
        rowh = max(rowh, im.height)
    sheet = Image.new('RGBA', (W, y + rowh + lab + pad), (60, 0, 60, 255))
    dr = ImageDraw.Draw(sheet)
    for i, (im, (px, py)) in enumerate(zip(images, pos)):
        sheet.alpha_composite(im, (px, py + lab))
        dr.text((px, py - 1), str(i), fill=(255, 255, 0, 255))
    sheet.convert('RGB').save(os.path.join(a.out, 'sheet.png'))

    uniq = len({(c['source'], c['width'], c['height']) for c in car.ccbs})
    sizes = {}
    for c in car.ccbs:
        sizes[(c['width'], c['height'])] = sizes.get((c['width'], c['height']), 0) + 1
    print('ART.CAR: %d bytes, %d CCBs (%d unique source rects), shift table @0x%x, palette @0x%x'
          % (car.size, car.count, uniq, car.shift_off, car.pal_off))
    print('modes:', ', '.join('%s=%d' % kv for kv in sorted(modes.items(), key=lambda kv: -kv[1])))
    print('BGND(opaque):', sum(1 for c in car.ccbs if c['flags'] & 0x20))
    print('top sizes:', ', '.join('%dx%d=%d' % (w, h, n) for (w, h), n in
                                  sorted(sizes.items(), key=lambda kv: -kv[1])[:10]))
    if os.path.exists(a.trans):
        print('TRANS.TBL:', describe_trans(a.trans))
    print('wrote %d PNGs + sheet.png, index.json, palette.png to %s' % (car.count, a.out))


if __name__ == '__main__':
    main()
