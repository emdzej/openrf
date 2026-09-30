#!/usr/bin/env python3
"""Scale the site's favicon (docs/public/favicon.png, the 64x64 skull sprite) to an app icon.

Nearest-neighbour, so the pixel art stays sharp (sips would blur it). Standard library only, so it runs
on CI runners without Pillow. Reads 8-bit non-interlaced RGBA/RGB PNGs, writes RGBA.

    tools/icon.py <size> <out.png> [in.png]
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise SystemExit(f'{path}: not a PNG')
    pos, idat, hdr = 8, b'', None
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR':
            hdr = struct.unpack('>IIBBBBB', body)
        elif kind == b'IDAT':
            idat += body
    w, h, depth, ctype, _, _, interlace = hdr
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise SystemExit(f'{path}: need an 8-bit non-interlaced RGB(A) PNG')
    bpp = 4 if ctype == 6 else 3
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line if bpp == 4 else bytearray(
            sum(([line[x * 3], line[x * 3 + 1], line[x * 3 + 2], 255] for x in range(w)), [])))
        prev = line
    return w, h, rows


def write_png(path, w, h, rows):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    size, out = int(sys.argv[1]), sys.argv[2]
    src = sys.argv[3] if len(sys.argv) > 3 else 'docs/public/favicon.png'
    w, h, rows = read_png(src)
    scaled = [bytearray(b''.join(bytes(rows[y * h // size][(x * w // size) * 4:(x * w // size) * 4 + 4])
                                 for x in range(size))) for y in range(size)]
    write_png(out, size, size, scaled)


if __name__ == '__main__':
    main()
