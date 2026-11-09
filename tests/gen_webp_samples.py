#!/usr/bin/env python3
"""Generate lossless WebP samples and their reference decodes.

The VP8L encoder picks its tools per image, so these samples are built to
make it reach for the paths that are easy to get wrong: the predictor and
subtract-green transforms without a palette, the colour cache, and the
colour transform.  Each .webp is paired with a BGRA reference frame
produced by ffmpeg's own WebP decoder.  Regenerate with

    python3 tests/gen_webp_samples.py

Requires Pillow (built with WebP support) and ffmpeg.
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")

# name, width, height, source
SAMPLES = [
    # 400 pixels of noise cannot fit a palette, so the encoder falls back to
    # the predictor/subtract-green transforms.
    ("lossless_pred", 40, 32, "ramp"),
    # Pixels drawn from many distinct colours spread over the image: a
    # palette does not fit and backward references do not help, which is
    # what makes the encoder use a colour cache.
    ("lossless_cache", 32, 24, "manycolours"),
    # Correlated channels invite the colour transform, and the repeated
    # lower half keeps the predictor busy.
    ("lossless_cross", 40, 32, "correlated"),
]


def lcg(seed):
    state = seed
    while True:
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        # take the high bits: the low ones of an LCG cycle far too quickly
        yield (state >> 16) & 0xff


def make_source(kind, w, h):
    """Return raw rgb24 pixel bytes."""
    rnd = lcg(20261109)
    if kind == "manycolours":
        table = [bytes((next(rnd), next(rnd), next(rnd))) for _ in range(400)]
    out = bytearray()
    for y in range(h):
        for x in range(w):
            if kind == "ramp":
                # smooth enough for the predictor, and far more colours
                # than a palette can hold
                base = ((x * 251) // max(w - 1, 1) +
                        (y * 253) // max(h - 1, 1)) // 2
                out += bytes((base, min(255, base + (y % 5)),
                              min(255, base + (x % 7))))
            elif kind == "manycolours":
                idx = ((next(rnd) << 8) | next(rnd)) % len(table)
                out += table[idx]
            else:
                # the channels come from the same ramp but with different
                # slopes, which is what the colour transform is for
                v = (x * 255) // max(w - 1, 1)
                out += bytes((v, min(255, 40 + v // 2), min(255, 70 + v // 4)))
    if kind == "correlated":
        # repeat the top half below, so the predictor has something to work on
        row = w * 3
        top = bytes(out[:row * (h // 2)])
        out[row * (h // 2):] = top * 2
    return bytes(out)


def make_bad_canvas(name, w, h, canvas_w, canvas_h, kind):
    """A container whose VP8X canvas is larger than the image it carries.

    The payload is a real lossless image, so only the canvas disagrees; the
    reader has to refuse it rather than hand out a picture whose rows run past
    the decoded pixels."""
    from PIL import Image

    img = Image.frombytes("RGB", (w, h), make_source(kind, w, h))
    tmp = os.path.join(DATA, f"webp_{name}_tmp.webp")
    img.save(tmp, lossless=True, quality=100, method=4, exact=True)
    raw = open(tmp, "rb").read()
    os.remove(tmp)
    # lift the VP8L chunk out of the file Pillow wrote
    off = raw.index(b"VP8L")
    size = int.from_bytes(raw[off + 4:off + 8], "little")
    vp8l = raw[off:off + 8 + size]
    if size % 2:
        vp8l += b"\x00"        # RIFF chunks are padded to an even size
    vp8x = (b"VP8X" + (10).to_bytes(4, "little") + b"\x00\x00\x00\x00" +
            (canvas_w - 1).to_bytes(3, "little") +
            (canvas_h - 1).to_bytes(3, "little"))
    body = vp8x + vp8l
    with open(os.path.join(DATA, f"webp_{name}.webp"), "wb") as f:
        f.write(b"RIFF" + (4 + len(body)).to_bytes(4, "little") + b"WEBP" +
                body)
    print(f"{name}: canvas {canvas_w}x{canvas_h} over a {w}x{h} payload "
          f"({len(body)} byte payload area)")


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr[-3000:])
        raise SystemExit(f"command failed: {' '.join(cmd)}")
    return r


def main():
    from PIL import Image

    os.makedirs(DATA, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="ffpic_webp_")
    for name, w, h, kind in SAMPLES:
        img = Image.frombytes("RGB", (w, h), make_source(kind, w, h))

        webp = os.path.join(DATA, f"webp_{name}.webp")
        img.save(webp, lossless=True, quality=100, method=4, exact=True)

        ref = os.path.join(DATA, f"webp_{name}.ref")
        raw = os.path.join(tmp, "ref.raw")
        run(["ffmpeg", "-y", "-loglevel", "error", "-i", webp,
             "-pix_fmt", "bgra", "-f", "rawvideo", raw])
        refdata = open(raw, "rb").read()
        want = w * h * 4
        if len(refdata) != want:
            raise SystemExit(f"{name}: reference is {len(refdata)} bytes, "
                             f"expected {want}")
        with open(ref, "wb") as f:
            f.write(refdata)
        print(f"{name}: {os.path.getsize(webp)} bytes webp, "
              f"{len(refdata)} bytes ref")

    make_bad_canvas("lossless_badcanvas", 24, 16, 64, 64, "manycolours")


if __name__ == "__main__":
    main()
