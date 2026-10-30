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
import shutil
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


def box(typ, payload):
    return struct.pack(">I4s", 8 + len(payload), typ) + payload


def full_box(typ, version, flags, payload):
    return box(typ, struct.pack(">B3s", version, flags) + payload)


def children(data, start, end):
    """Yield (offset, size, type) for the boxes in start..end."""
    p = start
    while p + 8 <= end:
        size, typ = struct.unpack_from(">I4s", data, p)
        if size < 8 or p + size > end:
            raise SystemExit(f"bad box {typ} at {p}")
        yield p, size, typ
        p += size


def parse_meta(data):
    """Return (meta offset/size, {type: (offset, size)}, hdlr box bytes)."""
    off = 0
    boxes = {}
    for p, size, typ in children(data, 0, len(data)):
        if typ == b"meta":
            off, msize = p, size
    if not off:
        raise SystemExit("no meta box")
    hdlr = None
    for p, size, typ in children(data, off + 12, off + msize):
        boxes[typ] = (p, size)
        if typ == b"hdlr":
            hdlr = data[p:p + size]
    return off, msize, boxes, hdlr


def read_iloc(data, off):
    """Return [(item_id, [(offset, length), ...]), ...] and the layout."""
    ver = data[off + 8]
    q = off + 12
    offset_size, length_size = data[q] >> 4, data[q] & 15
    q += 1
    base_size, index_size = data[q] >> 4, data[q] & 15
    q += 1
    count = struct.unpack_from(">H", data, q)[0]
    q += 2
    if ver != 0 or offset_size != 4 or length_size != 4:
        raise SystemExit("unsupported iloc layout")
    items = []
    for _ in range(count):
        item_id = struct.unpack_from(">H", data, q)[0]
        q += 2
        q += 2  # data_reference_index
        q += base_size
        extent_count = struct.unpack_from(">H", data, q)[0]
        q += 2
        extents = []
        for _ in range(extent_count):
            q += index_size
            extent_off = struct.unpack_from(">I", data, q)[0]
            q += 4
            extent_len = struct.unpack_from(">I", data, q)[0]
            q += 4
            extents.append((extent_off, extent_len))
        items.append((item_id, extents))
    return items, (offset_size, length_size), q - off


def item_payload(data, extents):
    return b"".join(data[off:off + length] for off, length in extents)


def make_iloc(items):
    """items: [(item_id, [(offset, length), ...]), ...]"""
    body = struct.pack(">BBH", 0x44, 0x00, len(items))
    for item_id, extents in items:
        body += struct.pack(">HHH", item_id, 0, len(extents))
        for offset, length in extents:
            body += struct.pack(">II", offset, length)
    return full_box(b"iloc", 0, b"\0\0\0", body)


def make_iinf(ids):
    body = struct.pack(">H", len(ids))
    for item_id in ids:
        body += full_box(b"infe", 2, b"\0\0\0",
                         struct.pack(">HH4s", item_id, 0, b"av01") + b"x\0")
    return full_box(b"iinf", 0, b"\0\0\0", body)


def make_iref(ref_type, from_id, to_id):
    child = box(ref_type, struct.pack(">HHH", from_id, 1, to_id))
    return full_box(b"iref", 0, b"\0\0\0", child)


def make_pitm(item_id):
    return full_box(b"pitm", 0, b"\0\0\0", struct.pack(">H", item_id))


def write_derived(name, ftyp, meta, payload):
    path = os.path.join(DATA, f"avif_{name}.avif")
    with open(path, "wb") as f:
        f.write(ftyp)
        f.write(meta)
        f.write(box(b"mdat", payload))
    return path


def make_two_extents(src, name):
    """Store the coded item as two extents; the decoder has to join them."""
    data = open(src, "rb").read()
    ftyp_size = struct.unpack_from(">I", data, 0)[0]
    ftyp = data[:ftyp_size]
    _, _, boxes, hdlr = parse_meta(data)
    items, _, _ = read_iloc(data, boxes[b"iloc"][0])
    if len(items) != 1 or len(items[0][1]) != 1:
        raise SystemExit("expected a single extent")
    payload = item_payload(data, items[0][1])
    half = len(payload) // 2
    keep = b"".join(data[boxes[t][0]:boxes[t][0] + boxes[t][1]]
                    for t in (b"iinf", b"iprp"))
    pitm = data[boxes[b"pitm"][0]:boxes[b"pitm"][0] + boxes[b"pitm"][1]]

    # the built meta has a fixed length, so the offsets converge after one
    # rebuild
    extents = [(0, half), (half, len(payload) - half)]
    for _ in range(4):
        meta = full_box(b"meta", 0, b"\0\0\0",
                        hdlr + pitm + make_iloc([(items[0][0], extents)]) +
                        keep)
        data_off = len(ftyp) + len(meta) + 8
        new = [(data_off, half), (data_off + half, len(payload) - half)]
        if new == extents:
            break
        extents = new
    return write_derived(name, ftyp, meta, payload)


def make_mif1(src, name):
    """Rewrite the major brand to mif1; avif stays a compatible brand."""
    data = bytearray(open(src, "rb").read())
    assert data[4:8] == b"ftyp"
    data[8:12] = b"mif1"
    path = os.path.join(DATA, f"avif_{name}.avif")
    open(path, "wb").write(data)
    return path


def make_aux_first(src_color, src_alpha, name):
    """No pitm, and the first item is an auxl-referenced alpha item: the
    decoder has to skip it and show the colour item."""
    color = open(src_color, "rb").read()
    ftyp_size = struct.unpack_from(">I", color, 0)[0]
    ftyp = color[:ftyp_size]
    _, _, boxes, hdlr = parse_meta(color)
    citems, _, _ = read_iloc(color, boxes[b"iloc"][0])
    cpayload = item_payload(color, citems[0][1])

    alpha = open(src_alpha, "rb").read()
    _, _, aboxes, _ = parse_meta(alpha)
    aitems, _, _ = read_iloc(alpha, aboxes[b"iloc"][0])
    apayload = item_payload(alpha, aitems[0][1])

    payload = apayload + cpayload
    meta = b""
    for _ in range(4):
        data_off = len(ftyp) + len(meta) + 8
        iloc = make_iloc([(1, [(data_off, len(apayload))]),
                          (2, [(data_off + len(apayload), len(cpayload))])])
        meta = full_box(b"meta", 0, b"\0\0\0",
                        hdlr + iloc + make_iinf([1, 2]) +
                        make_iref(b"auxl", 1, 2))
        if data_off == len(ftyp) + len(meta) + 8:
            break
    return write_derived(name, ftyp, meta, payload)


# sRGB colourants adapted to the D50 profile connection space
SRGB_D50 = (
    (0.4360413, 0.2224845, 0.0139202),
    (0.3851129, 0.7169688, 0.0970672),
    (0.1430458, 0.0606167, 0.7139126),
)


def srgb_encode(v):
    if v <= 0.0:
        return 0.0
    if v >= 1.0:
        return 1.0
    return 12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055


def build_icc(colorants, gamma):
    """Minimal ICC v4 matrix/TRC profile with a parametric type 0 curve."""
    tags = []
    for sig, xyz in zip((b"rXYZ", b"gXYZ", b"bXYZ"), colorants):
        tags.append((sig, struct.pack(
            ">4s4x3i", b"XYZ ", *[int(round(v * 65536)) for v in xyz])))
    # type, reserved, function type 0, reserved, then the gamma parameter
    para = struct.pack(">4sIHH", b"para", 0, 0, 0) + \
        struct.pack(">i", int(round(gamma * 65536)))
    for sig in (b"rTRC", b"gTRC", b"bTRC"):
        tags.append((sig, para))

    header = bytearray(128)
    header[8:12] = bytes((0x04, 0x30, 0, 0))
    header[12:16] = b"mntr"
    header[16:20] = b"RGB "
    header[20:24] = b"XYZ "
    header[36:40] = b"acsp"
    header[68:80] = struct.pack(">3i", int(0.9642 * 65536), 65536,
                                int(0.8249 * 65536))

    table_off = 128 + 4 + 12 * len(tags)
    entries = b""
    body = b""
    for sig, blob in tags:
        entries += sig + struct.pack(">II", table_off + len(body), len(blob))
        body += blob
        if len(body) % 4:
            body += b"\0" * (4 - len(body) % 4)
    total = 128 + 4 + len(entries) + len(body)
    header[0:4] = struct.pack(">I", total)
    return bytes(header) + struct.pack(">I", len(tags)) + entries + body


def make_icc_item(src_color, icc, name):
    """Attach a colr 'prof' property to the colour item through ipma."""
    color = open(src_color, "rb").read()
    ftyp_size = struct.unpack_from(">I", color, 0)[0]
    ftyp = color[:ftyp_size]
    _, _, boxes, hdlr = parse_meta(color)
    citems, _, _ = read_iloc(color, boxes[b"iloc"][0])
    cpayload = item_payload(color, citems[0][1])

    ipco = box(b"ipco", box(b"colr", b"prof" + icc))
    ipma = full_box(b"ipma", 0, b"\0\0\0",
                    struct.pack(">IHB", 1, 1, 1) + b"\x01")
    iprp = box(b"iprp", ipco + ipma)

    meta = b""
    for _ in range(4):
        data_off = len(ftyp) + len(meta) + 8
        iloc = make_iloc([(1, [(data_off, len(cpayload))])])
        meta = full_box(b"meta", 0, b"\0\0\0",
                        hdlr + make_pitm(1) + iloc + make_iinf([1]) + iprp)
        if data_off == len(ftyp) + len(meta) + 8:
            break
    return write_derived(name, ftyp, meta, cpayload)


def make_alpha(src_color, src_alpha, name):
    """Colour item plus a monochrome alpha item referenced by auxl."""
    color = open(src_color, "rb").read()
    ftyp_size = struct.unpack_from(">I", color, 0)[0]
    ftyp = color[:ftyp_size]
    _, _, boxes, hdlr = parse_meta(color)
    citems, _, _ = read_iloc(color, boxes[b"iloc"][0])
    cpayload = item_payload(color, citems[0][1])

    alpha = open(src_alpha, "rb").read()
    _, _, aboxes, _ = parse_meta(alpha)
    aitems, _, _ = read_iloc(alpha, aboxes[b"iloc"][0])
    apayload = item_payload(alpha, aitems[0][1])

    payload = cpayload + apayload
    meta = b""
    for _ in range(4):
        data_off = len(ftyp) + len(meta) + 8
        iloc = make_iloc([(1, [(data_off, len(cpayload))]),
                          (2, [(data_off + len(cpayload), len(apayload))])])
        meta = full_box(b"meta", 0, b"\0\0\0",
                        hdlr + make_pitm(1) + iloc + make_iinf([1, 2]) +
                        make_iref(b"auxl", 2, 1))
        if data_off == len(ftyp) + len(meta) + 8:
            break
    return write_derived(name, ftyp, meta, payload)


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

        # Derived containers that exercise the reader rather than the codec:
        # a generic HEIF major brand, an item split into two extents, and an
        # auxl-referenced alpha item stored before the colour item.
        mixed = os.path.join(DATA, "avif_mixed.avif")
        mixed_ref = os.path.join(DATA, "avif_mixed.ref")
        mono = os.path.join(DATA, "avif_mono.avif")
        derived = [
            ("mif1", make_mif1(mixed, "mif1")),
            ("extents", make_two_extents(mixed, "extents")),
            ("aux", make_aux_first(mixed, mono, "aux")),
        ]
        for name, path in derived:
            shutil.copyfile(mixed_ref, os.path.join(DATA, f"avif_{name}.ref"))
            print(f"{name}: {os.path.getsize(path)} bytes (derived)")

        # A real alpha item: a lossless monochrome ramp encoded like the other
        # samples, merged next to the mixed colour item and referenced by
        # auxl.  The reference is the mixed frame with the ramp in alpha.
        aw, ah = 100, 62
        ramp = bytes(x * 255 // (aw - 1) for _ in range(ah) for x in range(aw))
        ramp_src = os.path.join(tmp, "alpha.raw")
        with open(ramp_src, "wb") as f:
            f.write(ramp)
        ramp_avif = os.path.join(tmp, "alpha.avif")
        run(["ffmpeg", "-y", "-loglevel", "error",
             "-f", "rawvideo", "-pix_fmt", "gray", "-s", f"{aw}x{ah}",
             "-r", "1", "-i", ramp_src, "-frames:v", "1",
             "-c:v", "libaom-av1", "-usage", "allintra",
             "-still-picture", "1", "-pix_fmt", "gray",
             "-cpu-used", "1", "-crf", "0", "-aom-params", "lossless=1",
             "-f", "avif", ramp_avif])
        path = make_alpha(mixed, ramp_avif, "alpha")
        ref = bytearray(open(mixed_ref, "rb").read())
        for y in range(ah):
            for x in range(aw):
                ref[(y * aw + x) * 4 + 3] = ramp[y * aw + x]
        with open(os.path.join(DATA, "avif_alpha.ref"), "wb") as f:
            f.write(ref)
        print(f"alpha: {os.path.getsize(path)} bytes (derived)")

        # A gamma 1.8 matrix/TRC profile attached as a colr property.  The
        # reference is the mixed frame with the curve applied to each channel,
        # because the decoder has to turn the assumed gamma 1.8 source space
        # into sRGB.
        icc = build_icc(SRGB_D50, 1.8)
        with open(os.path.join(DATA, "icc_gamma18.icc"), "wb") as f:
            f.write(icc)
        path = make_icc_item(mixed, icc, "icc")
        ref = bytearray(open(mixed_ref, "rb").read())
        for i in range(0, len(ref), 4):
            for c in range(3):
                ref[i + c] = round(srgb_encode((ref[i + c] / 255.0) ** 1.8) * 255)
        with open(os.path.join(DATA, "avif_icc.ref"), "wb") as f:
            f.write(ref)
        print(f"icc: {os.path.getsize(path)} bytes (derived)")


if __name__ == "__main__":
    main()
