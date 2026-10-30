#!/usr/bin/env python3
"""Generate real AVIF still samples and their reference decodes.

Each sample is a gradient or noise image encoded with libaom-av1 through
ffmpeg's AVIF muxer, and paired with a BGRA reference frame produced by
ffmpeg's own AV1 decoder (dav1d).  Regenerate with

    python3 tests/gen_avif_samples.py

Requires ffmpeg with libaom-av1.  The samples cover the decode scenarios
the in-house AV1 decoder implements: loop filtering, tiles, odd sizes that
need the conformance window crop, coarse quantization, lossless coding,
4:4:4 and 4:0:0 monochrome.
"""

import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")

SAMPLES = [
    # name,      width, height, pixfmt,    source,    aom params
    ("gradient",  96, 96, "yuv420p", "gradient", ""),
    ("noise",     96, 96, "yuv420p", "noise",    ""),
    ("mixed",    100, 62, "yuv420p", "mixed",    ""),
    ("tiles",    256, 192, "yuv420p", "noise",    "tile-columns=1:tile-rows=1"),
    ("highqp",    96, 96, "yuv420p", "gradient", "qp=42"),
    ("lossless",  64, 64, "yuv420p", "noise",    "lossless=1"),
    ("yuv444",    96, 96, "yuv444p", "gradient", ""),
    ("mono",      96, 96, "gray",    "mixed",    ""),
]


def lcg(seed):
    state = seed
    while True:
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        yield state


def make_source(kind, w, h):
    """Return raw rgb24 pixel bytes."""
    rnd = lcg(20261029)
    out = bytearray()
    for y in range(h):
        for x in range(w):
            r = (x * 255) // max(w - 1, 1)
            g = (y * 255) // max(h - 1, 1)
            b = 255 - (x * 255) // max(w - 1, 1)
            if kind == "noise":
                r, g, b = next(rnd) % 256, next(rnd) % 256, next(rnd) % 256
            elif kind == "mixed":
                b = (b + (y * 255) // max(h - 1, 1)) // 2
                if (x // 8 + y // 8) % 2:
                    r = next(rnd) % 256
            out += bytes((r, g, b))
    return bytes(out)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr[-3000:])
        raise SystemExit(f"command failed: {' '.join(cmd)}")
    return r


def main():
    os.makedirs(DATA, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name, w, h, pixfmt, kind, params in SAMPLES:
            src = os.path.join(tmp, f"{name}.raw")
            with open(src, "wb") as f:
                f.write(make_source(kind, w, h))
            avif = os.path.join(DATA, f"avif_{name}.avif")
            ref = os.path.join(DATA, f"avif_{name}.ref")

            cmd = ["ffmpeg", "-y", "-loglevel", "error",
                   "-f", "rawvideo", "-pix_fmt", "rgb24",
                   "-s", f"{w}x{h}", "-r", "1",
                   "-i", src,
                   "-frames:v", "1", "-c:v", "libaom-av1",
                   "-usage", "allintra",
                   "-still-picture", "1",
                   "-pix_fmt", pixfmt,
                   "-cpu-used", "1"]
            if name == "highqp":
                cmd += ["-crf", "42", "-qp", "42"]
            elif name == "lossless":
                cmd += ["-crf", "0", "-aom-params", "lossless=1"]
            else:
                cmd += ["-crf", "20"]
            if params:
                for kv in params.split(":"):
                    k, v = kv.split("=")
                    cmd += ["-" + k, v]
            cmd += ["-f", "avif", avif]
            run(cmd)
            # the reference: ffmpeg's own av1 decoder, converted to bgra
            run(["ffmpeg", "-y", "-loglevel", "error", "-i", avif,
                 "-pix_fmt", "bgra", "-f", "rawvideo", ref])
            size = os.path.getsize(ref)
            if size != w * h * 4:
                raise SystemExit(
                    f"{name}: reference is {size} bytes, expected {w * h * 4}")
            print(f"{name}: {os.path.getsize(avif)} bytes, "
                  f"ref {size} bytes")


if __name__ == "__main__":
    main()
