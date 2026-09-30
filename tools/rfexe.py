#!/usr/bin/env python3
"""Shared reader for the original game executable (RFIRE.BIN on the CD root = Ghidra's rfire_game.exe,
Win32 PE, image base 0x400000). Tools read the tables they need from it at run time instead of
embedding copies (OpenRF's source tree contains no data copied from the executable).

Lookup order: $OPENRF_EXE, <root>/cd/RFIRE.BIN (case-insensitive), <root>/rfire_game.exe.

    from rfexe import Exe
    pe = Exe()            # or Exe(path)
    pe.u32(0x451438 + 4)  # VA reads; .bss (beyond the raw data) reads as zero
"""
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE_BASE = 0x400000
EXPECTED_SIZE = 431616          # keep in sync with EXE_SIZE / EXE_CRC32 in src/exe.h
EXPECTED_CRC32 = 0x64c49a1b


def _ci_join(base, rel):
    """Join rel (with '/') to base, matching every component case-insensitively."""
    cur = base
    for part in rel.split('/'):
        try:
            names = os.listdir(cur)
        except OSError:
            return os.path.join(cur, part)
        m = next((n for n in names if n.lower() == part.lower()), part)
        cur = os.path.join(cur, m)
    return cur


def find_exe(data_root=None):
    if os.environ.get('OPENRF_EXE'):
        return os.environ['OPENRF_EXE']
    cands = []
    if data_root:
        cands.append(_ci_join(data_root, 'RFIRE.BIN'))
    cands += [_ci_join(os.path.join(ROOT, 'cd'), 'RFIRE.BIN'), os.path.join(ROOT, 'rfire_game.exe')]
    for c in cands:
        if os.path.exists(c):
            return c
    raise SystemExit('RFIRE.BIN not found: put the extracted CD in %s/cd (RFIRE.BIN is on the CD root) '
                     'or set OPENRF_EXE' % ROOT)


class Exe:
    def __init__(self, path=None, check=True):
        self.path = path or find_exe()
        self.d = open(self.path, 'rb').read()
        if check and (len(self.d) != EXPECTED_SIZE or zlib.crc32(self.d) != EXPECTED_CRC32):
            raise SystemExit('%s: unsupported version (size %d, CRC-32 %08x; expected %d, %08x)' % (
                self.path, len(self.d), zlib.crc32(self.d), EXPECTED_SIZE, EXPECTED_CRC32))
        pe = struct.unpack_from('<I', self.d, 0x3c)[0]
        n = struct.unpack_from('<H', self.d, pe + 6)[0]
        opt = struct.unpack_from('<H', self.d, pe + 20)[0]
        self.secs = []          # (name, va, vsize, raw offset, raw size)
        for i in range(n):
            o = pe + 24 + opt + i * 40
            name = self.d[o:o + 8].rstrip(b'\0').decode()
            vs, va, rs, ro = struct.unpack_from('<IIII', self.d, o + 8)
            self.secs.append((name, IMAGE_BASE + va, vs, ro, rs))

    def sec(self, name):
        return next(s for s in self.secs if s[0] == name)

    def is_data(self, a):
        _, va, vs, _, _ = self.sec('.data')
        return va <= a < va + vs

    def off(self, a):
        """File offset of VA a (None if it is not backed by raw data)."""
        for _, va, vs, ro, rs in self.secs:
            if va <= a < va + max(vs, rs):
                return ro + (a - va) if a - va < rs else None
        return None

    def mapped(self, a):
        return any(va <= a < va + max(vs, rs) for _, va, vs, _, rs in self.secs)

    def rd(self, a, n):
        """n bytes at VA a; bytes past a section's raw data read as zero. Unmapped VAs raise ValueError."""
        for _, va, vs, ro, rs in self.secs:
            if va <= a < va + max(vs, rs):
                r = a - va
                if r + n <= rs:
                    return self.d[ro + r:ro + r + n]
                return (self.d[ro + r:ro + rs] if r < rs else b'') + b'\0' * (n - max(0, rs - r))
        raise ValueError('VA 0x%x not mapped' % a)

    def u8(self, a): return self.rd(a, 1)[0]
    def s8(self, a): return struct.unpack('<b', self.rd(a, 1))[0]
    def u16(self, a): return struct.unpack('<H', self.rd(a, 2))[0]
    def u32(self, a): return struct.unpack('<I', self.rd(a, 4))[0]
    def s32(self, a): return struct.unpack('<i', self.rd(a, 4))[0]
    def cstr(self, a): return self.rd(a, 64).split(b'\0')[0].decode('latin-1')


_default = None


def default():
    """Process-wide shared instance."""
    global _default
    if _default is None:
        _default = Exe()
    return _default


# ---- tables used by several tools (read from the exe, never copied) ----
TILE_TABLE_VA, TILE_COUNT = 0x452858, 240          # {u8 terrain, u8 object, u8 team, u8 handler}
BNO_TABLE_VA, BNO_COUNT, BNO_STRIDE = 0x451438, 91, 0x38


def tile_table(pe=None):
    pe = pe or default()
    raw = pe.rd(TILE_TABLE_VA, TILE_COUNT * 4)
    return [tuple(raw[i * 4:i * 4 + 4]) for i in range(TILE_COUNT)]


def obj_table(pe=None):
    """[(name, terrain_override (0xff keep), strength, radar colours)] from the BNO table."""
    pe = pe or default()
    out = []
    for i in range(BNO_COUNT):
        b = BNO_TABLE_VA + i * BNO_STRIDE
        out.append((pe.cstr(pe.u32(b + 4)), pe.u8(b + 0x10), pe.u8(b + 0x11), pe.u32(b + 0x34)))
    return out
