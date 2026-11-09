#!/usr/bin/env python3
"""Generate HEIF/AVIF derived-image fixtures: 'iden' and 'iovl'.

Regenerate with

    python3 tests/gen_derived_samples.py

The fixtures are deliberately small and lossless:

  hevc_iden.heic / avif_iden.avif
      an identity derived image whose referenced item is a solid 24x24
      picture.  The decoded output must equal that picture.

  hevc_iovl.heic / avif_iovl.avif
      a 40x36 overlay canvas with a semi-transparent fill.  Child A is an
      opaque 24x24 solid placed at (2,3); child B is a 24x24 solid with a
      constant alpha plane placed at (12,10), so the children overlap.  The
      alpha plane exercises source-over composition both over A and over the
      canvas fill.

References are produced with ffmpeg's own decoders and the same source-over
formula the C decoder uses.
"""

import os
import struct
import subprocess
import tempfile

import gen_avif_samples as A
import gen_hevc_samples as H

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")

W = 24
HGT = 24
CANVAS_W = 40
CANVAS_H = 36
FILL = (0, 90, 30, 128)          # straight R, G, B, A
CHILD_A = (200, 40, 20)          # opaque
CHILD_B = (20, 60, 200)          # alpha 128
ALPHA_B = 128
OFF_A = (2, 3)
OFF_B = (12, 10)


def box(typ, *payloads):
    payload = b"".join(payloads)
    return struct.pack(">I4s", 8 + len(payload), typ) + payload


def full_box(typ, version, flags, *payloads):
    payload = b"".join(payloads)
    return box(typ, struct.pack(">B3s", version, flags.to_bytes(3, "big")) +
               payload)


def infe(item_id, typ):
    return full_box(b"infe", 2, 0,
                    struct.pack(">HH4s", item_id, 0, typ) + b"x\0")


def iinf(items):
    body = struct.pack(">H", len(items))
    for item_id, typ in items:
        body += infe(item_id, typ)
    return full_box(b"iinf", 0, 0, body)


def iref_entry(typ, frm, tos):
    body = struct.pack(">HH", frm, len(tos))
    for to in tos:
        body += struct.pack(">H", to)
    return box(typ, body)


def iref(entries):
    body = b""
    for typ, frm, tos in entries:
        body += iref_entry(typ, frm, tos)
    return full_box(b"iref", 0, 0, body)


def pitm(item_id):
    return full_box(b"pitm", 0, 0, struct.pack(">H", item_id))


def hdlr():
    return full_box(b"hdlr", 0, 0, struct.pack(">I", 0) + b"pict" +
                    b"\0" * 12 + b"\0")


def mdat_write(path, ftyp, meta_boxes, items):
    """Write items as payloads [(item_id, bytes), ...].

    Items with no payload keep an iloc entry with no extents (derived image
    items have no coded payload of their own)."""
    probe = build_meta(ftyp, meta_boxes, items, 0)
    data_off = len(ftyp) + len(probe) + 8
    meta = build_meta(ftyp, meta_boxes, items, data_off)
    payload = b"".join(p for _, p in items)
    with open(path, "wb") as f:
        f.write(ftyp + meta + box(b"mdat", payload))


def build_meta(ftyp, meta_boxes, items, data_off):
    """HEIF iloc v1 with 4 byte offsets/lengths and construction method 0."""
    body = bytes([0x44, 0x00]) + struct.pack(">H", len(items))
    off = data_off
    for item_id, payload in items:
        body += struct.pack(">H", item_id)
        body += struct.pack(">H", 0)       # construction method
        body += struct.pack(">H", 0)       # data reference index
        if payload:
            body += struct.pack(">H", 1)
            body += struct.pack(">II", off, len(payload))
            off += len(payload)
        else:
            body += struct.pack(">H", 0)
    iloc = full_box(b"iloc", 1, 0, body)
    return full_box(b"meta", 0, 0, hdlr() + b"".join(meta_boxes) +
                    iloc)


def heif_iprp(primary_ispe, children, alpha_items):
    props = []
    assoc = {}

    def add(item_id, prop):
        props.append(prop)
        assoc.setdefault(item_id, []).append(len(props))

    add(1, full_box(b"ispe", 0, 0,
                    struct.pack(">II", *primary_ispe)))
    for item_id, hvcc, ispe in children:
        add(item_id, box(b"hvcC", hvcc))
        add(item_id, full_box(b"ispe", 0, 0,
                              struct.pack(">II", *ispe)))
    for item_id, hvcc, ispe in alpha_items:
        add(item_id, box(b"hvcC", hvcc))
        add(item_id, full_box(b"ispe", 0, 0,
                              struct.pack(">II", *ispe)))
        add(item_id, full_box(b"auxC", 0, 0,
                              b"urn:mpeg:hevc:2015:auxid:1\0"))
    ipco = box(b"ipco", b"".join(props))
    body = struct.pack(">I", len(assoc))
    for item_id, indexes in assoc.items():
        body += struct.pack(">HB", item_id, len(indexes))
        body += bytes(indexes)
    ipma = full_box(b"ipma", 0, 0, body)
    return box(b"iprp", ipco, ipma)


def heif_encode_solid(tmp, name, rgb, mono=False):
    src = os.path.join(tmp, name + ".raw")
    if mono:
        with open(src, "wb") as f:
            f.write(bytes([rgb]) * (W * HGT))
    else:
        with open(src, "wb") as f:
            f.write(bytes(rgb) * (W * HGT))
    out265 = os.path.join(tmp, name + ".265")
    H.encode(src, W, HGT, mono, "lossless=1", out265, tmp,
             ref_pix="gray" if mono else "bgra")
    with open(out265, "rb") as f:
        nals = H.split_annexb(f.read())
    hvcc, payload = H.split_cfg(nals, mono)
    ref = H.decode_ref(out265, "gray" if mono else "bgra")
    return hvcc, payload, ref


def heif_build_iden(tmp):
    hvcc, cpayload, cref = heif_encode_solid(tmp, "heif_a", CHILD_A)
    xh, xp, xref = heif_encode_solid(tmp, "heif_alpha", ALPHA_B, mono=True)
    cref = bytearray(cref)
    for i in range(W * HGT):
        cref[i * 4 + 3] = xref[i]
    ftyp = box(b"ftyp", b"heic", struct.pack(">I", 0), b"mif1", b"heic")
    iprp = heif_iprp((W, HGT), [(2, hvcc, (W, HGT))],
                     [(3, xh, (W, HGT))])
    boxes = [pitm(1), iinf([(1, b"iden"), (2, b"hvc1"), (3, b"hvc1")]),
             iref([(b"dimg", 1, [2]), (b"auxl", 3, [1])]), iprp]
    path = os.path.join(DATA, "hevc_iden.heic")
    mdat_write(path, ftyp, boxes,
               [(1, b""), (2, cpayload), (3, xp)])
    with open(os.path.join(DATA, "hevc_iden.ref"), "wb") as f:
        f.write(cref)

    # Without a primary item the no-primary path must hand the iden item to
    # the queue and skip the item it references.
    boxes = [iinf([(1, b"iden"), (2, b"hvc1"), (3, b"hvc1")]),
             iref([(b"dimg", 1, [2]), (b"auxl", 3, [1])]), iprp]
    mdat_write(os.path.join(DATA, "hevc_iden_nopitm.heic"), ftyp, boxes,
               [(1, b""), (2, cpayload), (3, xp)])
    return path


def iovl_payload(flags=0, off_a=OFF_A, off_b=OFF_B):
    fill = struct.pack(">4H", FILL[0] << 8, FILL[1] << 8,
                       FILL[2] << 8, FILL[3] << 8)
    head = bytes([0, flags]) + fill
    if flags & 1:
        head += struct.pack(">II", CANVAS_W, CANVAS_H)
        head += struct.pack(">ii", off_a[0], off_a[1])
        head += struct.pack(">ii", off_b[0], off_b[1])
    else:
        head += struct.pack(">HH", CANVAS_W, CANVAS_H)
        head += struct.pack(">hh", off_a[0], off_a[1])
        head += struct.pack(">hh", off_b[0], off_b[1])
    return head


def heif_build_iovl(tmp):
    ah, ap, aref = heif_encode_solid(tmp, "heif_a", CHILD_A)
    bh, bp, bref = heif_encode_solid(tmp, "heif_b", CHILD_B)
    xh, xp, xref = heif_encode_solid(tmp, "heif_alpha", ALPHA_B, mono=True)
    bref = bytearray(bref)
    for i in range(W * HGT):
        bref[i * 4 + 3] = xref[i]
    ftyp = box(b"ftyp", b"heic", struct.pack(">I", 0), b"mif1", b"heic")
    boxes = [pitm(1), iinf([(1, b"iovl"), (2, b"hvc1"), (3, b"hvc1"),
                            (4, b"hvc1")]),
             iref([(b"dimg", 1, [2, 3]), (b"auxl", 4, [3])]),
             heif_iprp((CANVAS_W, CANVAS_H),
                       [(2, ah, (W, HGT)), (3, bh, (W, HGT))],
                       [(4, xh, (W, HGT))])]
    path = os.path.join(DATA, "hevc_iovl.heic")
    mdat_write(path, ftyp, boxes,
               [(1, iovl_payload()), (2, ap), (3, bp), (4, xp)])
    ref = compose(aref, bref)
    with open(os.path.join(DATA, "hevc_iovl.ref"), "wb") as f:
        f.write(ref)
    return path


def heif_build_iovl32(tmp):
    """Same canvas, but flags&1 selects 32 bit fields.  Child A starts
    outside the canvas (negative offset) and child B runs past the right and
    bottom edges, so both clips are exercised."""
    off_a = (-4, -3)
    off_b = (30, 20)
    ah, ap, aref = heif_encode_solid(tmp, "heif_a", CHILD_A)
    bh, bp, bref = heif_encode_solid(tmp, "heif_b", CHILD_B)
    xh, xp, xref = heif_encode_solid(tmp, "heif_alpha", ALPHA_B, mono=True)
    bref = bytearray(bref)
    for i in range(W * HGT):
        bref[i * 4 + 3] = xref[i]
    ftyp = box(b"ftyp", b"heic", struct.pack(">I", 0), b"mif1", b"heic")
    boxes = [pitm(1), iinf([(1, b"iovl"), (2, b"hvc1"), (3, b"hvc1"),
                            (4, b"hvc1")]),
             iref([(b"dimg", 1, [2, 3]), (b"auxl", 4, [3])]),
             heif_iprp((CANVAS_W, CANVAS_H),
                       [(2, ah, (W, HGT)), (3, bh, (W, HGT))],
                       [(4, xh, (W, HGT))])]
    path = os.path.join(DATA, "hevc_iovl32.heic")
    mdat_write(path, ftyp, boxes,
               [(1, iovl_payload(flags=1, off_a=off_a, off_b=off_b)),
                (2, ap), (3, bp), (4, xp)])
    ref = compose(aref, bref, off_a, off_b)
    with open(os.path.join(DATA, "hevc_iovl32.ref"), "wb") as f:
        f.write(ref)
    return path


def avif_encode_solid(tmp, name, rgb, mono=False):
    src = os.path.join(tmp, name + ".raw")
    if mono:
        with open(src, "wb") as f:
            f.write(bytes([rgb]) * (W * HGT))
    else:
        with open(src, "wb") as f:
            f.write(bytes(rgb) * (W * HGT))
    avif = os.path.join(tmp, name + ".avif")
    A.run(["ffmpeg", "-y", "-loglevel", "error",
           "-f", "rawvideo", "-pix_fmt", "gray" if mono else "rgb24",
           "-s", f"{W}x{HGT}", "-r", "1", "-i", src, "-frames:v", "1",
           "-c:v", "libaom-av1", "-usage", "allintra",
           "-still-picture", "1", "-pix_fmt", "gray" if mono else "yuv420p",
           "-cpu-used", "1", "-crf", "0", "-aom-params", "lossless=1",
           "-f", "avif", avif])
    data = open(avif, "rb").read()
    _, _, boxes, _ = A.parse_meta(data)
    items, _, _ = A.read_iloc(data, boxes[b"iloc"][0])
    payload = A.item_payload(data, items[0][1])
    pos = data.find(b"av1C")
    if pos < 4:
        raise SystemExit("no av1C property in the encoded sample")
    size = struct.unpack_from(">I", data, pos - 4)[0]
    av1c = data[pos - 4:pos - 4 + size]
    r = subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", avif,
                        "-pix_fmt", "gray" if mono else "bgra",
                        "-f", "rawvideo", "-"], capture_output=True)
    if r.returncode != 0:
        raise SystemExit(r.stderr.decode()[-3000:])
    return payload, r.stdout, av1c


def avif_iprp(sizes, alpha_items=(), av1c=None):
    """One ispe property per item plus the av1C of the coded items and an
    auxC for the alpha items, associated through a version 0 ipma."""
    props = []
    assoc = {}

    def add(item_id, prop):
        props.append(prop)
        assoc.setdefault(item_id, []).append(len(props))

    for item_id, (w, h) in sizes:
        if av1c and item_id in av1c:
            add(item_id, av1c[item_id])
        add(item_id, full_box(b"ispe", 0, 0, struct.pack(">II", w, h)))
    for item_id in alpha_items:
        add(item_id, full_box(
            b"auxC", 0, 0,
            b"urn:mpeg:mpegB:cicp:systems:auxiliary:alpha\0"))
    body = struct.pack(">I", len(assoc))
    for item_id, indexes in assoc.items():
        body += struct.pack(">HB", item_id, len(indexes))
        body += bytes(indexes)
    return box(b"iprp", box(b"ipco", b"".join(props)),
               full_box(b"ipma", 0, 0, body))


def avif_build(path, typ, items, refs, sizes=None, alpha_items=(), av1c=None):
    ftyp = box(b"ftyp", b"avif", struct.pack(">I", 0), b"mif1", b"mif1",
               b"miaf", b"MA1B")

    def meta_at(data_off):
        extents = []
        off = data_off
        for item_id, p in items:
            if p:
                extents.append((item_id, [(off, len(p))]))
                off += len(p)
            else:
                extents.append((item_id, []))
        return full_box(b"meta", 0, 0,
                        hdlr() + pitm(1) + A.make_iloc(extents) +
                        iinf([(i + 1, typ[i]) for i in range(len(typ))]) +
                        (avif_iprp(sizes, alpha_items, av1c)
                         if sizes else b"") +
                        iref(refs))

    probe = meta_at(0)
    data_off = len(ftyp) + len(probe) + 8
    m = meta_at(data_off)
    with open(path, "wb") as f:
        f.write(ftyp + m + box(b"mdat", b"".join(p for _, p in items)))


def avif_build_iden(tmp):
    cpayload, cref, cav1c = avif_encode_solid(tmp, "avif_a", CHILD_A)
    xp, xref, xav1c = avif_encode_solid(tmp, "avif_alpha", ALPHA_B,
                                        mono=True)
    cref = bytearray(cref)
    for i in range(W * HGT):
        cref[i * 4 + 3] = xref[i]
    path = os.path.join(DATA, "avif_iden.avif")
    avif_build(path, [b"iden", b"av01", b"av01"],
               [(1, b""), (2, cpayload), (3, xp)],
               [(b"dimg", 1, [2]), (b"auxl", 3, [1])],
               sizes=[(1, (W, HGT)), (2, (W, HGT)), (3, (W, HGT))],
               alpha_items=(3,), av1c={2: cav1c, 3: xav1c})
    with open(os.path.join(DATA, "avif_iden.ref"), "wb") as f:
        f.write(cref)
    return path


def avif_build_iovl(tmp):
    ap, aref, aav1c = avif_encode_solid(tmp, "avif_a", CHILD_A)
    bp, bref, bav1c = avif_encode_solid(tmp, "avif_b", CHILD_B)
    xp, xref, xav1c = avif_encode_solid(tmp, "avif_alpha", ALPHA_B,
                                        mono=True)
    bref = bytearray(bref)
    for i in range(W * HGT):
        bref[i * 4 + 3] = xref[i]
    path = os.path.join(DATA, "avif_iovl.avif")
    avif_build(path, [b"iovl", b"av01", b"av01", b"av01"],
               [(1, iovl_payload()), (2, ap), (3, bp), (4, xp)],
               [(b"dimg", 1, [2, 3]), (b"auxl", 4, [3])],
               sizes=[(1, (CANVAS_W, CANVAS_H)), (2, (W, HGT)),
                      (3, (W, HGT)), (4, (W, HGT))],
               alpha_items=(4,),
               av1c={2: aav1c, 3: bav1c, 4: xav1c})
    ref = compose(aref, bref)
    with open(os.path.join(DATA, "avif_iovl.ref"), "wb") as f:
        f.write(ref)
    return path


def over(src, dst):
    """Straight-alpha source-over of two BGRA pixels."""
    sa = src[3]
    da = dst[3]
    if sa == 0:
        return dst
    if sa == 255:
        return (src[0], src[1], src[2], 255)
    da2 = (da * (255 - sa) + 127) // 255
    oa = sa + da2
    return ((src[0] * sa + dst[0] * da2 + oa // 2) // oa,
            (src[1] * sa + dst[1] * da2 + oa // 2) // oa,
            (src[2] * sa + dst[2] * da2 + oa // 2) // oa,
            oa)


def compose(aref, bref, off_a=OFF_A, off_b=OFF_B):
    """Blit A then B onto the canvas, matching the decoder's clipping."""
    canvas = bytearray([FILL[2], FILL[1], FILL[0], FILL[3]] *
                       (CANVAS_W * CANVAS_H))

    def blit(img, dx, dy):
        for y in range(HGT):
            cy = dy + y
            if cy < 0 or cy >= CANVAS_H:
                continue
            for x in range(W):
                cx = dx + x
                if cx < 0 or cx >= CANVAS_W:
                    continue
                si = (y * W + x) * 4
                di = (cy * CANVAS_W + cx) * 4
                src = tuple(img[si:si + 4])
                dst = tuple(canvas[di:di + 4])
                canvas[di:di + 4] = bytes(over(src, dst))

    blit(aref, *off_a)
    blit(bref, *off_b)
    return bytes(canvas)


def main():
    os.makedirs(DATA, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name, path in [
                ("hevc_iden", heif_build_iden(tmp)),
                ("hevc_iovl", heif_build_iovl(tmp)),
                ("hevc_iovl32", heif_build_iovl32(tmp)),
                ("avif_iden", avif_build_iden(tmp)),
                ("avif_iovl", avif_build_iovl(tmp)),
        ]:
            print(f"{name}: {os.path.getsize(path)} bytes")


if __name__ == "__main__":
    main()
