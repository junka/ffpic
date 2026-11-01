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

import math
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
    # high bit depth: the output keeps 16 bit channels
    ("tenbit",    96, 96, "yuv420p10le", "gradient", ""),
    ("twelvebit", 96, 96, "yuv420p12le", "gradient", ""),
    # film grain: the libaom test vectors carry fixed grain parameters.
    # 96x100 leaves a partial 32 sample stripe, and the vectors differ in
    # overlap, clipping and chroma scaling from luma.
    ("grain1",    96, 100, "yuv420p", "gradient", ""),
    ("grain15",   96, 100, "yuv420p", "gradient", ""),
    ("grain16",   96, 100, "yuv420p", "gradient", ""),
    ("grain5_10", 96, 100, "yuv420p10le", "gradient", ""),
    # HDR: 10 bit PQ and HLG streams tone mapped to SDR sRGB.  4:4:4 keeps
    # the sample free of chroma upsampling differences, which the PQ curve
    # would amplify near white.
    ("pq10",      96, 96, "yuv444p10le", "gradient", ""),
    ("hlg10",     96, 96, "yuv444p10le", "gradient", ""),
    # an explicit BT.709 matrix/transfer, unlike the untagged samples
    ("bt709",     96, 96, "yuv444p", "gradient", ""),
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


def make_iloc_v2(items):
    """iloc version 2: 32-bit item IDs plus the construction method."""
    body = struct.pack(">BBH", 0x44, 0x00, len(items))
    for item_id, extents in items:
        body += struct.pack(">IHHH", item_id, 0, 0, len(extents))
        for offset, length in extents:
            body += struct.pack(">II", offset, length)
    return full_box(b"iloc", 2, b"\0\0\0", body)


def make_iinf(ids):
    body = struct.pack(">H", len(ids))
    for item_id in ids:
        body += full_box(b"infe", 2, b"\0\0\0",
                         struct.pack(">HH4s", item_id, 0, b"av01") + b"x\0")
    return full_box(b"iinf", 0, b"\0\0\0", body)


def make_iinf_v3(ids):
    """infe version 3: 32-bit item IDs."""
    body = struct.pack(">H", len(ids))
    for item_id in ids:
        body += full_box(b"infe", 3, b"\0\0\0",
                         struct.pack(">IH4s", item_id, 0, b"av01") + b"x\0")
    return full_box(b"iinf", 0, b"\0\0\0", body)


def make_iref(ref_type, from_id, to_id):
    child = box(ref_type, struct.pack(">HHH", from_id, 1, to_id))
    return full_box(b"iref", 0, b"\0\0\0", child)


def make_pitm(item_id):
    return full_box(b"pitm", 0, b"\0\0\0", struct.pack(">H", item_id))


def make_pitm_v1(item_id):
    return full_box(b"pitm", 1, b"\0\0\0", struct.pack(">I", item_id))


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


def make_transform(src_color, name, props):
    """Attach transformative properties to the colour item in ipma order."""
    color = open(src_color, "rb").read()
    ftyp_size = struct.unpack_from(">I", color, 0)[0]
    ftyp = color[:ftyp_size]
    _, _, boxes, hdlr = parse_meta(color)
    citems, _, _ = read_iloc(color, boxes[b"iloc"][0])
    cpayload = item_payload(color, citems[0][1])

    ipco = box(b"ipco", b"".join(box(t, p) for t, p in props))
    assoc = bytes(range(1, len(props) + 1))
    ipma = full_box(b"ipma", 0, b"\0\0\0",
                    struct.pack(">IHB", 1, 1, len(props)) + assoc)
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


def transform_pixels(data, w, h, bpp, props):
    """Apply the same transformative properties to a packed reference."""
    for typ, val in props:
        if typ == "irot":
            angle = val
            if angle == 0:
                continue
            nw, nh = (h, w) if angle != 2 else (w, h)
            out = bytearray(len(data))
            for y in range(nh):
                for x in range(nw):
                    if angle == 1:      # 90 CCW
                        sx, sy = w - 1 - y, x
                    elif angle == 2:    # 180
                        sx, sy = w - 1 - x, h - 1 - y
                    else:               # 270 CCW
                        sx, sy = y, h - 1 - x
                    out[(y * nw + x) * bpp:(y * nw + x + 1) * bpp] = \
                        data[(sy * w + sx) * bpp:(sy * w + sx + 1) * bpp]
            data, w, h = bytes(out), nw, nh
        elif typ == "imir":
            out = bytearray(len(data))
            for y in range(h):
                for x in range(w):
                    if val == 0:        # top and bottom exchanged
                        sx, sy = x, h - 1 - y
                    else:               # left and right exchanged
                        sx, sy = w - 1 - x, y
                    out[(y * w + x) * bpp:(y * w + x + 1) * bpp] = \
                        data[(sy * w + sx) * bpp:(sy * w + sx + 1) * bpp]
            data = bytes(out)
        elif typ == "clap":
            cw, ch, hoff, voff = val
            x0 = (w - cw) // 2 + hoff
            y0 = (h - ch) // 2 + voff
            out = bytearray()
            for y in range(ch):
                start = ((y0 + y) * w + x0) * bpp
                out += data[start:start + cw * bpp]
            data, w, h = bytes(out), cw, ch
    return data, w, h


def split_obus(data):
    """Return [(obu_type, raw bytes), ...] for a low overhead bitstream."""
    obus = []
    p = 0
    while p < len(data):
        hdr = data[p]
        typ = (hdr >> 3) & 0xF
        ext = hdr & 1
        q = p + 1 + (1 if ext else 0)
        size = 0
        shift = 0
        while True:
            b = data[q]
            q += 1
            size |= (b & 0x7F) << shift
            shift += 7
            if not (b & 0x80):
                break
        obus.append((typ, data[p:q + size]))
        p = q + size
    return obus


def make_config_obus(src, name):
    """Move the sequence header from the item payload into av1C configOBUs."""
    data = open(src, "rb").read()
    ftyp_size = struct.unpack_from(">I", data, 0)[0]
    ftyp = data[:ftyp_size]
    _, _, boxes, hdlr = parse_meta(data)
    citems, _, _ = read_iloc(data, boxes[b"iloc"][0])
    payload = item_payload(data, citems[0][1])

    obus = split_obus(payload)
    seq = b"".join(o for t, o in obus if t == 1)
    rest = b"".join(o for t, o in obus if t != 1)
    if not seq or seq == payload:
        raise SystemExit(f"{name}: no sequence header to move")

    # reuse the original av1C fixed fields
    pos = data.find(b"av1C")
    if pos < 4:
        raise SystemExit(f"{name}: no av1C property")
    cfg = data[pos + 4:pos + 8] + seq
    ipco = box(b"ipco", box(b"av1C", cfg))
    ipma = full_box(b"ipma", 0, b"\0\0\0",
                    struct.pack(">IHB", 1, 1, 1) + b"\x01")
    iprp = box(b"iprp", ipco + ipma)

    meta = b""
    for _ in range(4):
        data_off = len(ftyp) + len(meta) + 8
        iloc = make_iloc([(1, [(data_off, len(rest))])])
        meta = full_box(b"meta", 0, b"\0\0\0",
                        hdlr + make_pitm(1) + iloc + make_iinf([1]) + iprp)
        if data_off == len(ftyp) + len(meta) + 8:
            break
    return write_derived(name, ftyp, meta, rest)


def make_exif(orientation):
    """An Exif item payload: 4 byte TIFF offset, then a minimal TIFF."""
    tiff = b"II" + struct.pack("<H", 42) + struct.pack("<I", 8)
    tiff += struct.pack("<H", 1)
    tiff += struct.pack("<HHI", 0x0112, 3, 1)
    tiff += struct.pack("<H", orientation) + b"\x00\x00"
    tiff += struct.pack("<I", 0)
    return struct.pack(">I", 0) + tiff


def make_exif_be(orientation, padding=6):
    """Big-endian TIFF behind a non-zero tiff_header_offset."""
    tiff = b"MM" + struct.pack(">H", 42) + struct.pack(">I", 8)
    tiff += struct.pack(">H", 1)
    tiff += struct.pack(">HHI", 0x0112, 3, 1)
    tiff += struct.pack(">H", orientation) + b"\x00\x00"
    tiff += struct.pack(">I", 0)
    return struct.pack(">I", padding) + b"P" * padding + tiff


def make_iinf_meta(items):
    """infe entries for (item_id, type, content_type, protection_index)."""
    body = struct.pack(">H", len(items))
    for item_id, typ, ctype, prot in items:
        extra = b"x\0"
        if typ == b"mime":
            extra += ctype.encode() + b"\0\0"
        body += full_box(b"infe", 2, b"\0\0\0",
                         struct.pack(">HH4s", item_id, prot, typ) + extra)
    return full_box(b"iinf", 0, b"\0\0\0", body)


def make_iref_multi(refs):
    body = b""
    for typ, frm, to in refs:
        body += box(typ, struct.pack(">HHH", frm, 1, to))
    return full_box(b"iref", 0, b"\0\0\0", body)


def pack_items(ftyp, hdlr, boxes, payloads, name):
    """Write items 1..n in payload order and build the enclosing meta."""
    meta = b""
    for _ in range(4):
        data_off = len(ftyp) + len(meta) + 8
        items = []
        off = data_off
        for i, payload in enumerate(payloads):
            items.append((i + 1, [(off, len(payload))]))
            off += len(payload)
        meta = full_box(b"meta", 0, b"\0\0\0",
                        hdlr + make_pitm(1) + make_iloc(items) +
                        b"".join(boxes))
        if data_off == len(ftyp) + len(meta) + 8:
            break
    return write_derived(name, ftyp, meta, b"".join(payloads))


def make_metadata(src_color, src_thumb, name):
    """Exif and XMP describe the primary item, a thumbnail hangs off it."""
    color = open(src_color, "rb").read()
    ftyp_size = struct.unpack_from(">I", color, 0)[0]
    ftyp = color[:ftyp_size]
    _, _, boxes, hdlr = parse_meta(color)
    citems, _, _ = read_iloc(color, boxes[b"iloc"][0])
    cpayload = item_payload(color, citems[0][1])

    thumb = open(src_thumb, "rb").read()
    _, _, tboxes, _ = parse_meta(thumb)
    titems, _, _ = read_iloc(thumb, tboxes[b"iloc"][0])
    tpayload = item_payload(thumb, titems[0][1])

    items = [(1, b"av01", None, 0), (2, b"Exif", None, 0),
             (3, b"mime", "application/rdf+xml", 0), (4, b"av01", None, 0)]
    refs = [(b"cdsc", 2, 1), (b"cdsc", 3, 1), (b"thmb", 4, 1)]
    xmp = b'<x:xmpmeta xmlns:x="adobe:ns:meta/"/>'
    meta_boxes = [make_iinf_meta(items), make_iref_multi(refs)]
    return pack_items(ftyp, hdlr, meta_boxes,
                      [cpayload, make_exif(6), xmp, tpayload], name)


def make_metadata2(src_color, name):
    """Big-endian Exif with a non-zero TIFF offset, and a mime item that is
    not XMP: it must not be reported as the picture's XMP."""
    color = open(src_color, "rb").read()
    ftyp_size = struct.unpack_from(">I", color, 0)[0]
    ftyp = color[:ftyp_size]
    _, _, boxes, hdlr = parse_meta(color)
    citems, _, _ = read_iloc(color, boxes[b"iloc"][0])
    cpayload = item_payload(color, citems[0][1])

    items = [(1, b"av01", None, 0), (2, b"Exif", None, 0),
             (3, b"mime", "application/octet-stream", 0)]
    refs = [(b"cdsc", 2, 1), (b"cdsc", 3, 1)]
    meta_boxes = [make_iinf_meta(items), make_iref_multi(refs)]
    return pack_items(ftyp, hdlr, meta_boxes,
                      [cpayload, make_exif_be(8), b"not xmp"], name)


def make_protected(src_color, name):
    """Mark the primary item protected; the payload must not be decoded."""
    color = open(src_color, "rb").read()
    ftyp_size = struct.unpack_from(">I", color, 0)[0]
    ftyp = color[:ftyp_size]
    _, _, boxes, hdlr = parse_meta(color)
    citems, _, _ = read_iloc(color, boxes[b"iloc"][0])
    cpayload = item_payload(color, citems[0][1])

    ispe = full_box(b"ispe", 0, b"\0\0\0", struct.pack(">II", 100, 62))
    ipco = box(b"ipco", ispe)
    ipma = full_box(b"ipma", 0, b"\0\0\0",
                    struct.pack(">IHB", 1, 1, 1) + b"\x01")
    iprp = box(b"iprp", ipco + ipma)
    # one protection_info entry; its scheme is irrelevant to the reader
    ipro = full_box(b"ipro", 0, b"\0\0\0",
                    struct.pack(">H", 1) + box(b"sinf", b""))
    meta_boxes = [make_iinf_meta([(1, b"av01", None, 1)]), iprp, ipro]
    return pack_items(ftyp, hdlr, meta_boxes, [cpayload], name)


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


def make_pitm32(src_color, src_decoy, name):
    """Primary item id above 65535: pitm v1, iloc v2 and infe v3 carry 32-bit
    ids.  An unrelated av01 item is stored first, so only honouring pitm
    selects the colour picture."""
    color = open(src_color, "rb").read()
    ftyp_size = struct.unpack_from(">I", color, 0)[0]
    ftyp = color[:ftyp_size]
    _, _, boxes, hdlr = parse_meta(color)
    citems, _, _ = read_iloc(color, boxes[b"iloc"][0])
    cpayload = item_payload(color, citems[0][1])

    decoy = open(src_decoy, "rb").read()
    _, _, dboxes, _ = parse_meta(decoy)
    ditems, _, _ = read_iloc(decoy, dboxes[b"iloc"][0])
    dpayload = item_payload(decoy, ditems[0][1])

    primary = 70000
    payload = dpayload + cpayload
    meta = b""
    for _ in range(4):
        data_off = len(ftyp) + len(meta) + 8
        iloc = make_iloc_v2([(1, [(data_off, len(dpayload))]),
                             (primary, [(data_off + len(dpayload),
                                         len(cpayload))])])
        meta = full_box(b"meta", 0, b"\0\0\0",
                        hdlr + make_pitm_v1(primary) + iloc +
                        make_iinf_v3([1, primary]))
        if data_off == len(ftyp) + len(meta) + 8:
            break
    return write_derived(name, ftyp, meta, payload)


PQ_M1 = 2610.0 / 16384.0
PQ_M2 = 2523.0 / 4096.0 * 128.0
PQ_C1 = 3424.0 / 4096.0
PQ_C2 = 2413.0 / 4096.0 * 32.0
PQ_C3 = 2392.0 / 4096.0 * 32.0

BT2020_TO_XYZ = (
    (0.6369580, 0.1446169, 0.1688810),
    (0.2627002, 0.6779981, 0.0593017),
    (0.0000000, 0.0280727, 1.0609851),
)
XYZ_TO_SRGB = (
    (3.2404542, -1.5371385, -0.4985314),
    (-0.9692660, 1.8760108, 0.0415560),
    (0.0556434, -0.2040259, 1.0572252),
)
BT2020_LUMA = (0.2627002, 0.6779981, 0.0593017)


def pq_eotf(n):
    """ST 2084 EOTF; 1.0 is 10000 cd/m2."""
    if n <= 0.0:
        return 0.0
    p = n ** (1.0 / PQ_M2)
    if p <= PQ_C1:
        return 0.0
    return ((p - PQ_C1) / (PQ_C2 - PQ_C3 * p)) ** (1.0 / PQ_M1)


def hlg_inverse_oetf(x):
    if x <= 0.5:
        return x * x / 3.0
    return (math.exp((x - 0.55991073) / 0.17883277) + 0.28466892) / 12.0


def bt709_decode(v):
    if v < 0.081:
        return v / 4.5
    return ((v + 0.099) / 1.099) ** (1 / 0.45)


def bt709_reference(raw):
    out = bytearray(raw)
    for i in range(0, len(out), 4):
        for c in range(3):
            out[i + c] = round(
                srgb_encode(bt709_decode(out[i + c] / 255.0)) * 255)
    return bytes(out)


def hdr_reference(raw, w, h, kind):
    """Tone map ffmpeg's rgba64le decode of a PQ/HLG stream to SDR sRGB.

    The channel chain mirrors cs_hdr_apply(): the transfer decode gives
    cd/m2, BT.2020 becomes linear sRGB, the content peak sets the extended
    Reinhard white point and every channel is scaled by the same factor.
    """
    vals = struct.unpack("<%dH" % (w * h * 4), raw)
    lin = [0.0] * (w * h * 3)
    peak = 0.0
    for i in range(w * h):
        r = vals[4 * i] / 65535.0
        g = vals[4 * i + 1] / 65535.0
        b = vals[4 * i + 2] / 65535.0
        if kind == "pq":
            lr = pq_eotf(r) * 10000.0
            lg = pq_eotf(g) * 10000.0
            lb = pq_eotf(b) * 10000.0
        else:
            er = hlg_inverse_oetf(r)
            eg = hlg_inverse_oetf(g)
            eb = hlg_inverse_oetf(b)
            y = (BT2020_LUMA[0] * er + BT2020_LUMA[1] * eg +
                 BT2020_LUMA[2] * eb)
            s = 1000.0 * y ** 0.2 if y > 0.0 else 0.0
            lr, lg, lb = er * s, eg * s, eb * s
        X = (BT2020_TO_XYZ[0][0] * lr + BT2020_TO_XYZ[0][1] * lg +
             BT2020_TO_XYZ[0][2] * lb)
        Y = (BT2020_TO_XYZ[1][0] * lr + BT2020_TO_XYZ[1][1] * lg +
             BT2020_TO_XYZ[1][2] * lb)
        Z = (BT2020_TO_XYZ[2][0] * lr + BT2020_TO_XYZ[2][1] * lg +
             BT2020_TO_XYZ[2][2] * lb)
        lin[3 * i] = (XYZ_TO_SRGB[0][0] * X + XYZ_TO_SRGB[0][1] * Y +
                      XYZ_TO_SRGB[0][2] * Z)
        lin[3 * i + 1] = (XYZ_TO_SRGB[1][0] * X + XYZ_TO_SRGB[1][1] * Y +
                          XYZ_TO_SRGB[1][2] * Z)
        lin[3 * i + 2] = (XYZ_TO_SRGB[2][0] * X + XYZ_TO_SRGB[2][1] * Y +
                          XYZ_TO_SRGB[2][2] * Z)
        peak = max(peak, lin[3 * i], lin[3 * i + 1], lin[3 * i + 2])

    white = min(max(peak / 100.0, 1.0), 100.0)
    out = [0] * (w * h * 4)
    for i in range(w * h):
        lr = lin[3 * i] / 100.0
        lg = lin[3 * i + 1] / 100.0
        lb = lin[3 * i + 2] / 100.0
        m = max(lr, lg, lb)
        if m > 0.0:
            s = (m * (1.0 + m / (white * white)) / (1.0 + m)) / m
            lr, lg, lb = lr * s, lg * s, lb * s
        out[4 * i] = round(srgb_encode(lr) * 65535)
        out[4 * i + 1] = round(srgb_encode(lg) * 65535)
        out[4 * i + 2] = round(srgb_encode(lb) * 65535)
        out[4 * i + 3] = vals[4 * i + 3]
    return struct.pack("<%dH" % (w * h * 4), *out)


def main():
    os.makedirs(DATA, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name, w, h, pixfmt, kind, params in SAMPLES:
            src = os.path.join(tmp, f"{name}.raw")
            with open(src, "wb") as f:
                f.write(make_source(kind, w, h))
            avif = os.path.join(DATA, f"avif_{name}.avif")
            ref = os.path.join(DATA, f"avif_{name}.ref")
            wide = "10le" in pixfmt or "12le" in pixfmt

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
            if name.startswith("grain"):
                vector = name[5:].split("_")[0]
                cmd += ["-aom-params", "film-grain-test=" + vector]
            if name.startswith("pq"):
                cmd += ["-color_trc", "smpte2084",
                        "-color_primaries", "bt2020",
                        "-colorspace", "bt2020nc"]
            elif name.startswith("hlg"):
                cmd += ["-color_trc", "arib-std-b67",
                        "-color_primaries", "bt2020",
                        "-colorspace", "bt2020nc"]
            elif name.startswith("bt709"):
                cmd += ["-color_trc", "bt709", "-color_primaries", "bt709",
                        "-colorspace", "bt709"]
            cmd += ["-f", "avif", avif]
            run(cmd)
            # the reference: ffmpeg's own av1 decoder, packed to the depth
            # the picture claims
            pix_fmt = "rgba64le" if wide else "bgra"
            ref_cmd = ["ffmpeg", "-y", "-loglevel", "error"]
            if name.startswith("grain"):
                ref_cmd += ["-filmgrain", "1"]
            ref_cmd += ["-i", avif, "-pix_fmt", pix_fmt, "-f", "rawvideo",
                        ref]
            run(ref_cmd)
            size = os.path.getsize(ref)
            bpp = 8 if wide else 4
            if size != w * h * bpp:
                raise SystemExit(
                    f"{name}: reference is {size} bytes, expected {w * h * bpp}")
            if name.startswith("pq") or name.startswith("hlg"):
                with open(ref, "rb") as f:
                    raw = f.read()
                kind = "pq" if name.startswith("pq") else "hlg"
                with open(ref, "wb") as f:
                    f.write(hdr_reference(raw, w, h, kind))
            elif name.startswith("bt709"):
                with open(ref, "rb") as f:
                    raw = f.read()
                with open(ref, "wb") as f:
                    f.write(bt709_reference(raw))
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
            ("pitm32", make_pitm32(mixed, mono, "pitm32")),
            ("cfgobu", make_config_obus(mixed, "cfgobu")),
            ("meta", make_metadata(mixed, mono, "meta")),
            ("meta2", make_metadata2(mixed, "meta2")),
            ("prot", make_protected(mixed, "prot")),
        ]
        for name, path in derived:
            if name != "prot":      # the protected sample has no pixels to compare
                shutil.copyfile(mixed_ref,
                                os.path.join(DATA, f"avif_{name}.ref"))
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

        # The same profile on the 10 bit picture: the colour transform runs
        # on 16 bit samples.
        tenbit = os.path.join(DATA, "avif_tenbit.avif")
        tenbit_ref = os.path.join(DATA, "avif_tenbit.ref")
        path = make_icc_item(tenbit, icc, "icc10")
        raw = open(tenbit_ref, "rb").read()
        vals = list(struct.unpack("<%dH" % (len(raw) // 2), raw))
        for i in range(0, len(vals), 4):
            for c in range(3):
                v = (vals[i + c] / 65535.0) ** 1.8
                vals[i + c] = round(srgb_encode(v) * 65535)
        with open(os.path.join(DATA, "avif_icc10.ref"), "wb") as f:
            f.write(struct.pack("<%dH" % len(vals), *vals))
        print(f"icc10: {os.path.getsize(path)} bytes (derived)")

        # A 10 bit alpha plane next to the 10 bit colour item: the alpha
        # channel keeps 16 bit precision too.
        tw, th = 96, 96
        ramp10 = b"".join(struct.pack("<H", x * 1023 // (tw - 1))
                          for _ in range(th) for x in range(tw))
        ramp10_src = os.path.join(tmp, "alpha10.raw")
        with open(ramp10_src, "wb") as f:
            f.write(ramp10)
        ramp10_avif = os.path.join(tmp, "alpha10.avif")
        run(["ffmpeg", "-y", "-loglevel", "error",
             "-f", "rawvideo", "-pix_fmt", "gray10le", "-s", f"{tw}x{th}",
             "-r", "1", "-i", ramp10_src, "-frames:v", "1",
             "-c:v", "libaom-av1", "-usage", "allintra",
             "-still-picture", "1", "-pix_fmt", "gray10le",
             "-cpu-used", "1", "-crf", "0", "-aom-params", "lossless=1",
             "-f", "avif", ramp10_avif])
        path = make_alpha(tenbit, ramp10_avif, "alpha10")
        ref = bytearray(open(tenbit_ref, "rb").read())
        for y in range(th):
            for x in range(tw):
                v = x * 1023 // (tw - 1)
                a = (v * 65535 + 511) // 1023
                ref[(y * tw + x) * 8 + 6:(y * tw + x) * 8 + 8] = \
                    struct.pack("<H", a)
        with open(os.path.join(DATA, "avif_alpha10.ref"), "wb") as f:
            f.write(ref)
        print(f"alpha10: {os.path.getsize(path)} bytes (derived)")

        # irot, imir and clap rewrite the packed picture in the order ipma
        # associates them, so the reference frame goes through the same
        # steps.  rotimir also associates a pasp property, which must be
        # parsed but does not move pixels.
        mref = open(mixed_ref, "rb").read()
        tref = open(tenbit_ref, "rb").read()
        transforms = [
            ("irot90", mixed, mref, 100, 62, 4,
             [(b"irot", bytes([1]))], [("irot", 1)]),
            ("irot180", mixed, mref, 100, 62, 4,
             [(b"irot", bytes([2]))], [("irot", 2)]),
            ("irot270", mixed, mref, 100, 62, 4,
             [(b"irot", bytes([3]))], [("irot", 3)]),
            ("imir0", mixed, mref, 100, 62, 4,
             [(b"imir", bytes([0]))], [("imir", 0)]),
            ("imir1", mixed, mref, 100, 62, 4,
             [(b"imir", bytes([1]))], [("imir", 1)]),
            ("clap", mixed, mref, 100, 62, 4,
             [(b"clap", struct.pack(">8i", 60, 1, 40, 1, 4, 1, 2, 1))],
             [("clap", (60, 40, 4, 2))]),
            # the offsets are signed
            ("clapneg", mixed, mref, 100, 62, 4,
             [(b"clap", struct.pack(">8i", 60, 1, 40, 1, -4, 1, -2, 1))],
             [("clap", (60, 40, -4, -2))]),
            ("rotimir", mixed, mref, 100, 62, 4,
             [(b"irot", bytes([1])), (b"imir", bytes([0])),
              (b"pasp", struct.pack(">II", 4, 3))],
             [("irot", 1), ("imir", 0)]),
            # the same path on 16 bit samples
            ("irot90_10", tenbit, tref, 96, 96, 8,
             [(b"irot", bytes([1]))], [("irot", 1)]),
        ]
        for name, src, refbytes, w, h, bpp, props, ops in transforms:
            path = make_transform(src, name, props)
            ref, rw, rh = transform_pixels(refbytes, w, h, bpp, ops)
            with open(os.path.join(DATA, f"avif_{name}.ref"), "wb") as f:
                f.write(ref)
            print(f"{name}: {os.path.getsize(path)} bytes "
                  f"(derived, {rw}x{rh})")


if __name__ == "__main__":
    main()
