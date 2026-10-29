#!/usr/bin/env python3
"""Generate real JPEG samples and their reference decodes.

Each sample is a gradient or noise image encoded with Pillow at various
subsampling, progressive and restart-marker settings, and paired with a
BGRA reference frame produced by ffmpeg's own mjpeg decoder.  Regenerate
with

    python3 tests/gen_jpg_samples.py

Requires Pillow and ffmpeg.
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")

SAMPLES = [
    # name,        width, height, mono, source,    quality, subsampling, kwargs
    ("base_420",   96, 96, False, "gradient", 85, 2, {}),
    ("prog_420",   96, 96, False, "gradient", 85, 2, {"progressive": True}),
    ("yuv444",     96, 96, False, "noise",    90, 0, {}),
    ("gray",       96, 96, True,  "gradient", 85, 0, {}),
    ("restart",    96, 96, False, "noise",    80, 2,
     {"restart_marker_blocks": 2}),
    ("odd",       100, 62, False, "mixed",    85, 2, {}),
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
    for y in range(h):
        for x in range(w):
            v = (x * 255) // max(w - 1, 1)
            n = (y * 255) // max(h - 1, 1)
            if kind == "mixed":
                v = (v + n) // 2
            p = next(rnd)
            if kind == "noise":
                if mono:
                    out.append(p % 256)
                else:
                    out += bytes((p % 256, next(rnd) % 256, next(rnd) % 256))
            else:
                if mono:
                    out.append(max(0, min(255, v)))
                else:
                    g = (y * 255) // max(h - 1, 1)
                    b = 255 - (x * 255) // max(w - 1, 1)
                    if kind == "mixed" and (x // 8 + y // 8) % 2:
                        p = 128 + (p % 64) - 32
                    out += bytes((max(0, min(255, v + (p % 32) - 16
                                             if kind != "gradient" else v)),
                                  g, b))
    return bytes(out)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr[-3000:])
        raise SystemExit(f"command failed: {' '.join(cmd)}")
    return r


def main():
    from PIL import Image

    os.makedirs(DATA, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="ffpic_jpg_")
    for name, w, h, mono, kind, quality, subsampling, kwargs in SAMPLES:
        src = os.path.join(tmp, f"{name}.raw")
        with open(src, "wb") as f:
            f.write(make_source(kind, w, h, mono))

        img = Image.frombytes("L" if mono else "RGB", (w, h),
                              open(src, "rb").read())
        jpg = os.path.join(DATA, f"jpg_{name}.jpg")
        save = dict(quality=quality, subsampling=subsampling)
        save.update(kwargs)
        img.save(jpg, **save)

        if "restart_marker_blocks" in kwargs:
            with open(jpg, "rb") as f:
                data = f.read()
            if not any(data[i] == 0xFF and 0xD0 <= data[i + 1] <= 0xD7
                       for i in range(len(data) - 1)):
                raise SystemExit(f"{name}: no restart markers in output")

        ref = os.path.join(DATA, f"jpg_{name}.ref")
        raw = os.path.join(tmp, "ref.raw")
        run(["ffmpeg", "-y", "-loglevel", "error", "-i", jpg,
             "-pix_fmt", "bgra", "-f", "rawvideo", raw])
        refdata = open(raw, "rb").read()
        want = w * h * 4
        if len(refdata) != want:
            raise SystemExit(f"{name}: reference is {len(refdata)} bytes, "
                             f"expected {want}")
        with open(ref, "wb") as f:
            f.write(refdata)
        print(f"{name}: {os.path.getsize(jpg)} bytes jpg, "
              f"{len(refdata)} bytes ref")


if __name__ == "__main__":
    main()
