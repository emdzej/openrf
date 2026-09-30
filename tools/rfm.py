#!/usr/bin/env python3
"""Return Fire level map (.RFM) parser and overview renderer.

Format summary (see docs/rfm.md for details):
  0x00  char[4]  "WRL\\0" magic (checked with lstrcmpiA)
  0x04  u8[4]    54 4D 00 05 (constant, unused by the game)
  0x08  u16      width  in tiles (always 128)
  0x0A  u16      height in tiles (always 128)
  0x0C  u8[2]    01 01 (constant, unused)
  0x0E  u16,u16  DOS date, DOS time  (created)
  0x12  u16,u16  DOS date, DOS time  (modified)
  0x16  u8       number of players (1 or 2)
  0x17  char[41] author, NUL-terminated
  0x40  u8       "valid" flag (loader rejects map if 0)
  0x41  u8[3]    unknown (01 01 00 / 01 00 00)
  0x44  u32      tile data size (width*height = 0x4000)
  0x48  u32      offset of tile data (= size of header + chunks)
  0x4C  u32      0
  0x50  chunks   {char tag[4]; u32 size_incl_8_byte_header; data} until offset [0x48]
  [0x48] u8[w*h] tile bytes, row-major, row 0 = north
Each tile byte is an index (0..239; >=0xF0 is treated as 0) into a 240-entry
table in RFIRE.BIN @0x452858: (terrain, object, team, handler).
"""
import os, sys, struct, glob, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rfexe  # noqa: E402

# Both tables are read from RFIRE.BIN (tools/rfexe.py): the tile table @0x452858 (terrain, object, team,
# handler) and, from the BNO table @0x451438, (name, terrain_override (0xff = keep), strength, radar colours
# lo = team 0, hi = other). Two names carry a stray '.' in the executable ("BNO_WALL_V."); it is dropped.
TILE_TABLE = rfexe.tile_table()
OBJ_TABLE = [(n.rstrip('.'), t, s, r) for n, t, s, r in rfexe.obj_table()]

# Terrain id classes (terrain id = low 7 bits of the in-game cell word; it is
# also the index of the ground tile graphic).  Derived from FUN_00418a30
# (water test), FUN_0041cdb0/FUN_0041cd80 (road test) and the object table's
# terrain overrides.
def terrain_class(t):
    if t == 0: return 'grass'
    if t == 1: return 'shallow'
    if t == 2: return 'deep'
    if t == 3: return 'land3'
    if t < 52: return 'shore'        # water<->land transitions; water() says shallow
    if t < 73: return 'landedge'     # land-side transitions (>=0x34 is dry)
    if t < 84: return 'road'         # 0x49..0x53
    if t in (84, 85): return 'bridge_end_h'   # 0x54 west end, 0x55 east end
    if t in (86, 87): return 'bridge_end_v'   # 0x56 north end, 0x57 south end
    if t in (88, 89): return 'gate_floor'
    if t == 90: return 'pad0'
    if t == 91: return 'pad1'
    return 'underlay'                # 92..111: ground under buildings/objects

TERRAIN_RGB = {
    'grass': (70, 140, 50), 'shallow': (80, 150, 210), 'deep': (20, 50, 130),
    'land3': (150, 170, 80), 'shore': (200, 190, 120), 'landedge': (110, 150, 60),
    'road': (120, 120, 120), 'bridge_end_h': (140, 90, 40), 'bridge_end_v': (140, 90, 40),
    'gate_floor': (100, 100, 100), 'pad0': (255, 255, 0), 'pad1': (255, 0, 255),
    'underlay': (90, 110, 60),
}

def object_class(o):
    n = OBJ_TABLE[o][0]
    for key, cls in (('BUSH', 'veg'), ('PALM', 'veg'), ('TREE', 'veg'), ('CACTUS', 'veg'),
                     ('PLANTER', 'veg'), ('ROCK', 'rock'), ('FLAG', 'flag'), ('BNKR', 'bunker'),
                     ('BLDG', 'building'), ('FACT', 'building'), ('HOSP', 'building'),
                     ('PRISON', 'building'), ('OUTPOST', 'building'), ('FUEL', 'fuel'),
                     ('AMMO', 'ammo'), ('WALL', 'wall'), ('GATE', 'gate'), ('TOWER', 'turret'),
                     ('TENT', 'tent'), ('BRIGE', 'bridge')):
        if key in n:
            if key == 'TOWER' and 'WATCH' in n or 'WTOWER' in n: return 'watchtower'
            return cls
    return 'other'

OBJ_RGB = {
    'veg': (20, 90, 20), 'rock': (90, 80, 70), 'flag': (255, 255, 255), 'bunker': (230, 130, 0),
    'building': (200, 60, 60), 'fuel': (240, 220, 0), 'ammo': (200, 0, 200), 'wall': (40, 40, 40),
    'gate': (0, 0, 0), 'turret': (255, 40, 40), 'watchtower': (255, 150, 150), 'tent': (180, 160, 110),
    'bridge': (160, 110, 60), 'other': (0, 255, 255),
}
TEAM_RGB = {0: (255, 255, 0), 1: (255, 0, 255), 2: None}   # team 0 = player 1, team 1 = player 2 / CPU enemy

def dosdt(d, t):
    return '%04d-%02d-%02d %02d:%02d:%02d' % (1980 + (d >> 9), (d >> 5) & 15, d & 31,
                                             t >> 11, (t >> 5) & 63, (t & 31) * 2)

class RFM:
    def __init__(self, path):
        self.path = path
        d = self.raw = open(path, 'rb').read()
        if d[:4] != b'WRL\0':
            raise ValueError('bad magic')
        self.width, self.height = struct.unpack_from('<HH', d, 8)
        cd, ct, md, mt = struct.unpack_from('<4H', d, 0x0E)
        self.created, self.modified = dosdt(cd, ct), dosdt(md, mt)
        self.players = d[0x16]
        self.author = d[0x17:0x40].split(b'\0')[0].decode('latin-1')
        self.valid = d[0x40]
        self.hdr_unk = d[0x41:0x44].hex()
        self.data_size, self.data_off = struct.unpack_from('<II', d, 0x44)
        if self.data_size + self.data_off != len(d):
            raise ValueError('size mismatch')
        self.chunks = []
        p = 0x50
        while p < self.data_off:
            tag = d[p:p + 4].decode('latin-1'); n = struct.unpack_from('<I', d, p + 4)[0]
            self.chunks.append((tag, d[p + 8:p + n])); p += n
        self.name = None; self.level = None; self.edition = None; self.vehicles = None
        for tag, body in self.chunks:
            if tag == 'NAME':
                self.name = body.split(b'\0')[0].decode('latin-1')
            elif tag == 'LEVL':
                self.level = struct.unpack_from('<I', body)[0]
            elif tag == 'EDTN':
                self.edition = struct.unpack_from('<I', body)[0]
            elif tag == 'VHCL':
                # bytes: +0 ASV, +1 heli, +2 jeep, +3 tank, +4 AI-pool(?), +5 mines; 0xFF = default
                self.vehicles = dict(zip(('asv', 'heli', 'jeep', 'tank', 'unk4', 'mines'), body[:6]))
        self.tiles = d[self.data_off:self.data_off + self.data_size]
        self.decode()

    def decode(self):
        """Reproduce FUN_004322f0's cell build: 128x128 grid of (terrain, object, team)."""
        W, H = self.width, self.height
        ox, oy = (128 - W) >> 1, (128 - H) >> 1
        self.terrain = [2] * (128 * 128)       # out-of-map cells = deep water
        self.obj = [0] * (128 * 128)
        self.team = [0] * (128 * 128)
        self.pads = {0: [], 1: []}; self.flags = {0: [], 1: []}
        for y in range(H):
            for x in range(W):
                b = self.tiles[y * W + x]
                if b >= 0xF0: b = 0
                ter, ob, team, hnd = TILE_TABLE[b]
                i = (y + oy) * 128 + x + ox
                if ter == 0xFF:
                    self.terrain[i] = 0; continue
                self.terrain[i] = ter
                self.team[i] = team
                if ob:
                    self.obj[i] = ob
                    ov = OBJ_TABLE[ob][1]
                    if ov != 0xFF: self.terrain[i] = ov
                if hnd == 1: self.pads[team].append((x + ox, y + oy))
                if hnd == 2: self.flags[team].append((x + ox, y + oy))
        # bridge spans: 0x54 .. 0x55 horizontally, 0x56 .. 0x57 vertically
        for i in range(128 * 128):
            if self.terrain[i] == 0x54:
                j = i + 1
                while j < 128 * 128 and self.terrain[j] != 0x55:
                    self.obj[j] = 74; self.team[j] = self.team[i]; j += 1
            if self.terrain[i] == 0x56:
                j = i + 128
                while j < 128 * 128 and self.terrain[j] != 0x57:
                    self.obj[j] = 75; self.team[j] = self.team[i]; j += 128

    def objects(self):
        out = []
        for i, o in enumerate(self.obj):
            if o: out.append((i % 128, i // 128, OBJ_TABLE[o][0], self.team[i]))
        return out

    def summary(self):
        from collections import Counter
        c = Counter(n for _, _, n, _ in self.objects())
        return dict(file=os.path.basename(self.path), name=self.name, author=self.author,
                    players=self.players, level=self.level, size=[self.width, self.height],
                    created=self.created, modified=self.modified, vehicles=self.vehicles,
                    chunks=[t for t, _ in self.chunks], hdr_unk=self.hdr_unk,
                    home_pads=self.pads, flag_sites=self.flags, objects=dict(sorted(c.items())))

    def render(self, out, scale=6):
        from PIL import Image, ImageDraw
        img = Image.new('RGB', (128 * scale, 128 * scale))
        dr = ImageDraw.Draw(img)
        for i in range(128 * 128):
            x, y = (i % 128) * scale, (i // 128) * scale
            dr.rectangle([x, y, x + scale - 1, y + scale - 1],
                         fill=TERRAIN_RGB[terrain_class(self.terrain[i])])
            o = self.obj[i]
            if o:
                cls = object_class(o)
                m = 1
                tc = TEAM_RGB.get(self.team[i])
                if tc and cls not in ('veg', 'rock', 'bridge', 'wall'):
                    dr.rectangle([x, y, x + scale - 1, y + scale - 1], fill=tc)
                    m = 2 if scale >= 5 else 1
                dr.rectangle([x + m, y + m, x + scale - 1 - m, y + scale - 1 - m], fill=OBJ_RGB[cls])
        for team, lst in self.flags.items():
            for (x, y) in lst:
                dr.rectangle([x * scale - 2, y * scale - 2, x * scale + scale + 1, y * scale + scale + 1],
                             outline=(255, 255, 255))
        img.save(out)

def main(argv):
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    files = argv or sorted(glob.glob(os.path.join(root, 'cd', 'WORLDS', '*', '*', '*.RFM')))
    outdir = os.path.join(root, 'out', 'maps'); os.makedirs(outdir, exist_ok=True)
    summ = []
    for f in files:
        m = RFM(f)
        s = m.summary(); summ.append(s)
        base = os.path.splitext(os.path.basename(f))[0]
        pl = os.path.basename(os.path.dirname(os.path.dirname(f)))[0]
        lv = os.path.basename(os.path.dirname(f))[-1]
        m.render(os.path.join(outdir, '%s_%sP_L%s.png' % (base, pl, lv)))
        print('%s %dP L%s %-32s pads=%s flags=%s objs=%d' % (base, m.players, m.level, m.name,
              {k: len(v) for k, v in m.pads.items()}, {k: len(v) for k, v in m.flags.items()},
              len(m.objects())))
    with open(os.path.join(outdir, 'summary.json'), 'w') as fp:
        json.dump(summ, fp, indent=1)

if __name__ == '__main__':
    main(sys.argv[1:])
