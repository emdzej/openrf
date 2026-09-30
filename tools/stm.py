#!/usr/bin/env python3
"""Return Fire .STM movie decoder.

STM = AVI pre-chewed by Silent Software into fixed-size "super-blocks" that the
game streams from CD with a reader thread (FUN_00432a40) and plays with
ICDecompressEx (Cinepak) + a DirectSound ring buffer (FUN_00424b70 et al.).
See docs/stm.md for the byte layout.

Usage:
    python3 tools/stm.py [files...]          # default: cd/TITLE/*.STM
    python3 tools/stm.py --avi-only ...      # stop after writing out/stm/<name>.avi
    python3 tools/stm.py --info ...          # just print header/chunk summary

Output: out/stm/<name>.avi  (lossless remux: original cvid + PCM)
        out/stm/<name>.mp4  (ffmpeg transcode, H.264 + AAC)
"""
import glob
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FFMPEG = "/opt/homebrew/bin/ffmpeg"

HDR_SIZE = 0x9A0  # game reads exactly this many bytes into 0x458900


class STM:
    def __init__(self, path):
        self.path = path
        d = self.data = open(path, "rb").read()
        self.data_start, self.chunk_size, self.nchunks = struct.unpack_from("<3I", d, 0)
        # --- audio AVISTREAMINFOA at 0x0C (140 bytes) ---
        self.a_type = d[0x0C:0x10]
        (self.a_scale, self.a_rate, self.a_start, self.a_length, self.a_initial,
         self.a_sugbuf, self.a_quality, self.a_samplesize) = struct.unpack_from("<8I", d, 0x20)
        self.a_name = d[0x58:0x98].split(b"\0")[0].decode("latin1")
        self.wfx = d[0x98:0x98 + 16]  # WAVEFORMAT/PCMWAVEFORMAT (16 bytes)
        (self.w_tag, self.w_ch, self.w_rate, self.w_bps, self.w_align,
         self.w_bits) = struct.unpack_from("<HHIIHH", d, 0x98)
        self.a_total_bytes, self.a_ring_bytes = struct.unpack_from("<2I", d, 0xAC)
        # --- video AVISTREAMINFOA at 0xB8 ---
        self.v_type, self.v_handler = d[0xB8:0xBC], d[0xBC:0xC0]
        (self.v_scale, self.v_rate, self.v_start, self.v_length, self.v_initial,
         self.v_sugbuf, self.v_quality, self.v_samplesize) = struct.unpack_from("<8I", d, 0xCC)
        self.v_rect = struct.unpack_from("<4i", d, 0xEC)
        self.v_name = d[0x104:0x144].split(b"\0")[0].decode("latin1")
        self.bih_in = d[0x144:0x144 + 40]  # BITMAPINFOHEADER of the cvid stream
        _, self.width, self.height, _, self.in_bpp = struct.unpack_from("<IiiHH", d, 0x144)
        self.bih_out = d[0x56C:0x56C + 40]  # 8-bit top-down output DIB for ICDecompressEx
        self.palette = d[0x594:0x594 + 1024]  # RGBQUAD[256] -> ICM_DECOMPRESS_SET_PALETTE
        self.last_frame, self.max_frame_bytes, self.out_image_bytes = struct.unpack_from("<3I", d, 0x994)
        assert self.a_type == b"auds" and self.v_type == b"vids" and self.v_handler == b"cvid"
        self._parse_chunks()

    def _parse_chunks(self):
        d = self.data
        self.audio = bytearray()
        self.frames = {}  # frame number -> (keyframe, cvid bytes)
        self.chunk_info = []
        for i in range(self.nchunks):
            base = self.data_start + i * self.chunk_size
            _nx, _pv, a_off, a_cnt, v_off, v_cnt, _r6, _r7 = struct.unpack_from("<8I", d, base)
            if a_off and a_cnt:
                n = a_cnt * self.a_samplesize
                assert a_off + n <= self.chunk_size
                self.audio += d[base + a_off: base + a_off + n]
            nf = 0
            p = v_off
            while p:
                nxt, flags, _pad, fnum = struct.unpack_from("<IBBH", d, base + p)
                fr = base + p + 8
                size = (d[fr + 1] << 16) | (d[fr + 2] << 8) | d[fr + 3]  # cvid frame length (BE24)
                if nxt:
                    assert 0 <= nxt - p - 8 - size <= 3, (i, hex(p), nxt, size)  # padded to 4
                assert fnum not in self.frames
                self.frames[fnum] = (flags & 1, d[fr:fr + size])
                nf += 1
                p = nxt
            self.chunk_info.append((i, a_off, a_cnt, v_off, v_cnt, nf))

    def info(self):
        n = len(self.frames)
        print(f"{os.path.basename(self.path)}: data@{self.data_start:#x} chunk={self.chunk_size} x{self.nchunks}")
        print(f"  audio '{self.a_name}': tag={self.w_tag} {self.w_ch}ch {self.w_rate}Hz {self.w_bits}bit "
              f"len={self.a_length} blocks ({self.a_length / (self.a_rate / self.a_scale):.3f}s) "
              f"total={self.a_total_bytes} got={len(self.audio)} ring={self.a_ring_bytes}")
        print(f"  video '{self.v_name}': {self.width}x{self.height} in_bpp={self.in_bpp} "
              f"{self.v_rate}/{self.v_scale} fps len={self.v_length} ({self.v_length * self.v_scale / self.v_rate:.3f}s) "
              f"records={n} last#={max(self.frames)} hdr_last={self.last_frame} "
              f"keys={sum(k for k, _ in self.frames.values())} maxbytes={self.max_frame_bytes}")

    # --- AVI writer (lossless remux) ---
    def write_avi(self, out):
        nframes = max(self.v_length, max(self.frames) + 1)
        fps_num, fps_den = self.v_rate, self.v_scale
        a_bytes_per_frame = self.w_bps * fps_den / fps_num

        def chunk(tag, payload):
            b = tag + struct.pack("<I", len(payload)) + payload
            return b + (b"\0" if len(payload) & 1 else b"")

        def lst(tag, payload):
            return b"LIST" + struct.pack("<I", len(payload) + 4) + tag + payload

        # movi: one video frame followed by the audio for that frame period
        movi = bytearray(b"movi")
        idx = bytearray()
        apos = 0
        max_v = max(len(f) for _, f in self.frames.values())
        max_a = 0
        for n in range(nframes):
            key, fb = self.frames.get(n, (0, b""))  # missing number => repeat previous (null frame)
            off = len(movi)
            movi += chunk(b"00dc", fb)
            idx += b"00dc" + struct.pack("<III", 0x10 if key else 0, off, len(fb))
            aend = int(round((n + 1) * a_bytes_per_frame)) // self.w_align * self.w_align
            if n == nframes - 1:
                aend = len(self.audio)
            aend = min(aend, len(self.audio))
            if aend > apos:
                ab = bytes(self.audio[apos:aend])
                off = len(movi)
                movi += chunk(b"01wb", ab)
                idx += b"01wb" + struct.pack("<III", 0x10, off, len(ab))
                max_a = max(max_a, len(ab))
                apos = aend
        if apos < len(self.audio):
            ab = bytes(self.audio[apos:])
            off = len(movi)
            movi += chunk(b"01wb", ab)
            idx += b"01wb" + struct.pack("<III", 0x10, off, len(ab))

        w, h = self.width, self.height
        avih = struct.pack("<14I", int(1e6 * fps_den / fps_num), self.w_bps + max_v * fps_num,
                           0, 0x10, nframes, 0, 2, max(max_v, max_a), w, h, 0, 0, 0, 0)
        strh_v = struct.pack("<4s4sIHHIIIIIIIIhhhh", b"vids", b"cvid", 0, 0, 0, 0,
                             fps_den, fps_num, 0, nframes, max_v, 0xFFFFFFFF, 0, 0, 0, w, h)
        # cvid input header: keep original but force 24 bpp (RF.STM stores 40 = "cvid greyscale" hint)
        bih = bytearray(self.bih_in)
        struct.pack_into("<H", bih, 14, 24)
        strl_v = lst(b"strl", chunk(b"strh", strh_v) + chunk(b"strf", bytes(bih)))
        nblocks = len(self.audio) // self.w_align
        strh_a = struct.pack("<4s4sIHHIIIIIIIIhhhh", b"auds", b"\0\0\0\0", 0, 0, 0, 0,
                             self.w_align, self.w_bps, 0, nblocks, max_a, 0xFFFFFFFF, self.w_align,
                             0, 0, 0, 0)
        strl_a = lst(b"strl", chunk(b"strh", strh_a) + chunk(b"strf", self.wfx + b"\0\0"))
        hdrl = lst(b"hdrl", chunk(b"avih", avih) + strl_v + strl_a)
        body = b"AVI " + hdrl + lst(b"movi", bytes(movi[4:])) + chunk(b"idx1", bytes(idx))
        with open(out, "wb") as f:
            f.write(b"RIFF" + struct.pack("<I", len(body)) + body)
        return nframes


def main(argv):
    avi_only = "--avi-only" in argv
    info_only = "--info" in argv
    files = [a for a in argv if not a.startswith("--")] or sorted(glob.glob(os.path.join(ROOT, "cd/TITLE/*.STM")))
    outdir = os.path.join(ROOT, "out/stm")
    os.makedirs(outdir, exist_ok=True)
    for fn in files:
        s = STM(fn)
        s.info()
        if info_only:
            continue
        name = os.path.splitext(os.path.basename(fn))[0].lower()
        avi = os.path.join(outdir, name + ".avi")
        s.write_avi(avi)
        if avi_only:
            continue
        mp4 = os.path.join(outdir, name + ".mp4")
        subprocess.run([FFMPEG, "-v", "error", "-y", "-i", avi,
                        "-c:v", "libx264", "-crf", "16", "-pix_fmt", "yuv420p",
                        "-c:a", "aac", "-b:a", "160k", mp4], check=True)
        print("  ->", os.path.relpath(mp4, ROOT))


if __name__ == "__main__":
    main(sys.argv[1:])
