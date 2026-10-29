#!/usr/bin/env python3
"""Generate real HEVC still samples and their reference decodes.

Each sample is a gradient or noise image encoded with libx265 through
ffmpeg, wrapped into a minimal HEIF container, and paired with a BGRA
reference frame produced by ffmpeg's own hevc decoder.  Regenerate with

    python3 tests/gen_hevc_samples.py

Requires ffmpeg with libx265.  The samples cover the decode scenarios the
decoder implements: SAO on and off, tiles, wavefront parallel processing,
a CTB size of 16, an odd size that needs the conformance window crop,
coarse quantization, lossless coding and 4:0:0 monochrome.  4:2:2/4:4:4
and 10-bit are not covered because the output conversion is not
implemented for them yet.
"""

import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")

FTYP = b"ftyp"
SAMPLES = [
    # name,      width, height, mono, source,             x265 params
    ("gradient",  96, 96, False, "gradient", ""),
    ("noise",     96, 96, False, "noise",    ""),
    ("odd",      100, 62, False, "mixed",    ""),
    ("ctb16",     96, 96, False, "noise",    "ctu=16"),
    ("tiles",    128, 96, False, "noise",    "tiles=2x2"),
    ("wpp",      128, 96, False, "noise",    "wpp=1"),
    ("nosao",     96, 96, False, "gradient", "no-sao=1"),
    ("highqp",    96, 96, False, "gradient", "qp=42"),
    ("lossless",  64, 64, False, "noise",    "lossless=1"),
    ("mono",      96, 96, True,  "mixed",    ""),
]


def lcg(seed):
    state = seed
    while True:
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        yield state


def make_source(kind, w, h, mono):
    """Return raw rgb24 or gray8 pixel bytes."""
    rnd = lcg(20261029)
    out = bytearray()
    if mono:
        for y in range(h):
            for x in range(w):
                v = (x * 255) // max(w - 1, 1)
                n = (y * 255) // max(h - 1, 1)
                if kind == "mixed":
                    v = (v + n) // 2
                p = next(rnd)
                if kind == "mixed" and (x // 8 + y // 8) % 2:
                    p = 128 + (p % 64) - 32
                out.append(max(0, min(255, v + (p % 32) - 16 if kind != "gradient" else v)))
        return bytes(out)
    for y in range(h):
        for x in range(w):
            r = (x * 255) // max(w - 1, 1)
            g = (y * 255) // max(h - 1, 1)
            b = 255 - (x * 255) // max(w - 1, 1)
            if kind == "noise":
                r, g, b = next(rnd) % 256, next(rnd) % 256, next(rnd) % 256
            elif kind == "mixed":
                b = (b + (y * 255) // max(h - 1, 1)) // 2
            if kind == "mixed" and (x // 8 + y // 8) % 2:
                r = next(rnd) % 256
            out += bytes((r, g, b))
    return bytes(out)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr[-3000:])
        raise SystemExit(f"command failed: {' '.join(cmd)}")
    return r


def encode(src, w, h, mono, params, out265, tmp):
    # rgb sources get converted to 4:4:4 by the wrapper unless the target
    # format is pinned; the decoder output conversion only covers 4:2:0
    pix = "gray" if mono else "yuv420p"
    xparams = "keyint=1:min-keyint=1:aud=0:repeat-headers=1"
    if params:
        xparams += ":" + params
    run(["ffmpeg", "-y", "-loglevel", "error",
         "-f", "rawvideo", "-pix_fmt", "rgb24" if not mono else "gray",
         "-s", f"{w}x{h}", "-r", "1",
         "-i", src,
         "-frames:v", "1", "-c:v", "libx265", "-preset", "medium",
         "-pix_fmt", pix,
         "-x265-params", xparams,
         "-f", "hevc", out265])
    # the reference: ffmpeg's own hevc decoder, converted to bgra
    run(["ffmpeg", "-y", "-loglevel", "error", "-f", "hevc", "-i", out265,
         "-pix_fmt", "bgra", "-f", "rawvideo", os.path.join(tmp, "ref.raw")])


def split_annexb(data):
    # simpler: find all 0001 or 000001 start codes, nal ends before the next one
    nals = []
    positions = []
    i = 0
    while i < len(data) - 3:
        if data[i:i + 3] == b"\x00\x00\x01":
            positions.append((i, 3))
            i += 3
        elif data[i:i + 4] == b"\x00\x00\x00\x01":
            positions.append((i, 4))
            i += 4
        else:
            i += 1
    for n, (pos, sclen) in enumerate(positions):
        s = pos + sclen
        e = positions[n + 1][0] if n + 1 < len(positions) else len(data)
        nals.append(data[s:e])
    return nals


def wrap_heif(nals, w, h, mono, out_heic):
    cfg_nals = {32: [], 33: [], 34: []}   # vps, sps, pps
    item_nals = []
    for nal in nals:
        t = (nal[0] >> 1) & 0x3F
        if t in cfg_nals:
            cfg_nals[t].append(nal)
        elif t in (35, 39, 40):   # aud and sei stay in the item data
            item_nals.append(nal)
        elif t < 32:
            item_nals.append(nal)

    arrays = b""
    for t in (32, 33, 34):
        if not cfg_nals[t]:
            continue
        a = bytearray()
        a.append((1 << 7) | (1 << 6) | t)   # completeness, needed, type
        a += struct.pack(">H", len(cfg_nals[t]))
        for nal in cfg_nals[t]:
            a += struct.pack(">H", len(nal)) + nal
        arrays += bytes(a)

    sps = cfg_nals[33][0]
    hvcc = bytearray()
    hvcc.append(1)                                   # configurationVersion
    hvcc += sps[3:15]                                # profile_tier_level
    hvcc += struct.pack(">H", 0xF000)                # min_spatial_segmentation
    hvcc.append(0xFC)                                # parallelismType
    hvcc.append(0xFC | (0 if mono else 1))           # chromaFormat
    hvcc.append(0xF8)                                # bitDepthLumaMinus8
    hvcc.append(0xF8)                                # bitDepthChromaMinus8
    hvcc += struct.pack(">H", 0)                     # avgFrameRate
    hvcc.append((0 << 6) | (1 << 3) | (0 << 2) | 3)  # lengthSizeMinusOne=3
    hvcc.append(3)                                   # numOfArrays
    hvcc += arrays

    mdat_payload = b"".join(struct.pack(">I", len(n)) + n for n in item_nals)

    def box(t, *payloads):
        body = b"".join(payloads)
        return struct.pack(">I", 8 + len(body)) + t + body

    def fullbox(t, version, flags, *payloads):
        return box(t, bytes([version]) + flags.to_bytes(3, "big"), *payloads)

    # the parser reads offset 8 as the brand, so put heic there
    ftyp = box(FTYP, b"heic", struct.pack(">I", 0), b"mif1", b"heic")

    hdlr = fullbox(b"hdlr", 0, 0, struct.pack(">I", 0), b"pict",
                   b"\0" * 12, b"\0")
    pitm = fullbox(b"pitm", 0, 0, struct.pack(">H", 1))
    infe = fullbox(b"infe", 2, 0, struct.pack(">H", 1), struct.pack(">H", 0),
                   b"hvc1", b"\0")
    iinf = fullbox(b"iinf", 0, 0, struct.pack(">H", 1), infe)
    hvcc_box = box(b"hvcC", bytes(hvcc))
    ispe = fullbox(b"ispe", 0, 0, struct.pack(">II", w, h))
    ipco = box(b"ipco", hvcc_box, ispe)
    ipma = fullbox(b"ipma", 0, 0, struct.pack(">I", 1),
                   struct.pack(">H", 1), bytes([2, 1, 2]))
    iprp = box(b"iprp", ipco, ipma)

    # iloc v1, 4-byte offsets/lengths, no base offset: filled below
    def make_iloc(mdat_data_offset):
        # the parser reads construction_method right after the item id
        body = bytes([0x44, 0x00]) + struct.pack(">H", 1)
        body += struct.pack(">H", 1)   # item id
        body += struct.pack(">H", 0)   # construction method: file offset
        body += struct.pack(">H", 0)   # data reference index
        body += struct.pack(">H", 1)   # extent count
        body += struct.pack(">I", mdat_data_offset)
        body += struct.pack(">I", len(mdat_payload))
        return fullbox(b"iloc", 1, 0, body)

    # meta size depends on the offset inside iloc; sizes are stable because
    # the offset field is fixed width, so compute once
    meta_probe = fullbox(b"meta", 0, 0, hdlr, pitm, iinf, iprp, make_iloc(0))
    mdat_data_offset = len(ftyp) + len(meta_probe) + 8
    iloc = make_iloc(mdat_data_offset)
    meta = fullbox(b"meta", 0, 0, hdlr, pitm, iinf, iprp, iloc)
    mdat = box(b"mdat", mdat_payload)

    with open(out_heic, "wb") as f:
        f.write(ftyp + meta + mdat)


def main():
    os.makedirs(DATA, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="ffpic_hevc_")
    for name, w, h, mono, kind, params in SAMPLES:
        src = os.path.join(tmp, f"{name}.raw")
        pix = "gray" if mono else "rgb24"
        with open(src, "wb") as f:
            f.write(make_source(kind, w, h, mono))
        out265 = os.path.join(tmp, f"{name}.265")
        encode(src, w, h, mono, params, out265, tmp)

        with open(out265, "rb") as f:
            data = f.read()
        nals = split_annexb(data)
        if not nals:
            raise SystemExit(f"{name}: no nals found")

        heic = os.path.join(DATA, f"hevc_{name}.heic")
        wrap_heif(nals, w, h, mono, heic)

        ref = os.path.join(DATA, f"hevc_{name}.ref")
        with open(os.path.join(tmp, "ref.raw"), "rb") as f:
            refdata = f.read()
        want = w * h * 4
        if len(refdata) != want:
            raise SystemExit(f"{name}: reference is {len(refdata)} bytes, "
                             f"expected {want}")
        with open(ref, "wb") as f:
            f.write(refdata)
        print(f"{name}: {os.path.getsize(heic)} bytes heic, "
              f"{len(refdata)} bytes ref")


if __name__ == "__main__":
    main()
