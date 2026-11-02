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


def encode(src, w, h, mono, params, out265, tmp, tags=None, depth=8,
           ref_pix="bgra"):
    # rgb sources get converted to 4:4:4 by the wrapper unless the target
    # format is pinned; the decoder output conversion only covers 4:2:0
    if mono:
        pix = ("gray10le" if depth == 10 else
               "gray12le" if depth == 12 else "gray")
    elif depth == 10:
        pix = "yuv420p10le"
    elif depth == 12:
        pix = "yuv420p12le"
    else:
        pix = "yuv420p"
    xparams = "keyint=1:min-keyint=1:aud=0:repeat-headers=1"
    if params:
        xparams += ":" + params
    cmd = ["ffmpeg", "-y", "-loglevel", "error",
           "-f", "rawvideo", "-pix_fmt", "rgb24" if not mono else "gray",
           "-s", f"{w}x{h}", "-r", "1",
           "-i", src]
    if tags:
        cmd += ["-color_primaries", tags[0], "-color_trc", tags[1],
                "-colorspace", tags[2]]
    cmd += ["-frames:v", "1", "-c:v", "libx265", "-preset", "medium",
            "-pix_fmt", pix,
            "-x265-params", xparams,
            "-f", "hevc", out265]
    run(cmd)
    # the reference: ffmpeg's own hevc decoder, converted to bgra
    run(["ffmpeg", "-y", "-loglevel", "error", "-f", "hevc", "-i", out265,
         "-pix_fmt", ref_pix, "-f", "rawvideo",
         os.path.join(tmp, "ref.raw")])


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


def split_cfg(nals, mono, bit_depth=8):
    """Split parameter sets into an hvcC record and a length-prefixed item."""
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
    hvcc.append(0xF8 | (bit_depth - 8))              # bitDepthLumaMinus8
    hvcc.append(0xF8 | (bit_depth - 8))              # bitDepthChromaMinus8
    hvcc += struct.pack(">H", 0)                     # avgFrameRate
    hvcc.append((0 << 6) | (1 << 3) | (0 << 2) | 3)  # lengthSizeMinusOne=3
    hvcc.append(3)                                   # numOfArrays
    hvcc += arrays

    payload = b"".join(struct.pack(">I", len(n)) + n for n in item_nals)
    return bytes(hvcc), payload


def wrap_heif(nals, w, h, mono, out_heic, colr=None, transforms=None,
              protected=False, bit_depth=8):
    hvcc, mdat_payload = split_cfg(nals, mono, bit_depth)

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
    infe = fullbox(b"infe", 2, 0, struct.pack(">H", 1),
                   struct.pack(">H", 1 if protected else 0),
                   b"hvc1", b"\0")
    iinf = fullbox(b"iinf", 0, 0, struct.pack(">H", 1), infe)
    hvcc_box = box(b"hvcC", hvcc)
    ispe = fullbox(b"ispe", 0, 0, struct.pack(">II", w, h))
    props = [hvcc_box, ispe]
    assoc = [1, 2]
    if colr:
        props.append(box(b"colr", b"nclx",
                         struct.pack(">HHHB", colr[0], colr[1], colr[2],
                                     0x80)))
        assoc.append(len(props))
    for typ, payload in transforms or ():
        props.append(box(typ, payload))
        assoc.append(len(props))
    ipco = box(b"ipco", *props)
    ipma = fullbox(b"ipma", 0, 0, struct.pack(">I", 1),
                   struct.pack(">H", 1), bytes([len(assoc)] + assoc))
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
    ipro = (fullbox(b"ipro", 0, 0, struct.pack(">H", 1))
            if protected else b"")
    meta_probe = fullbox(b"meta", 0, 0, hdlr, pitm, iinf, iprp, ipro,
                         make_iloc(0))
    mdat_data_offset = len(ftyp) + len(meta_probe) + 8
    iloc = make_iloc(mdat_data_offset)
    meta = fullbox(b"meta", 0, 0, hdlr, pitm, iinf, iprp, ipro, iloc)
    mdat = box(b"mdat", mdat_payload)

    with open(out_heic, "wb") as f:
        f.write(ftyp + meta + mdat)


def wrap_heif_alpha(color_nals, alpha_nals, w, h, prem, out_heic):
    """One colour item plus a monochrome alpha item linked by auxl (and prem).

    Item 1 is the colour picture and the primary item; item 2 holds the alpha
    plane and carries the auxC property that names it as alpha."""
    color_hvcc, color_payload = split_cfg(color_nals, False)
    alpha_hvcc, alpha_payload = split_cfg(alpha_nals, True)

    def box(t, *payloads):
        body = b"".join(payloads)
        return struct.pack(">I", 8 + len(body)) + t + body

    def fullbox(t, version, flags, *payloads):
        return box(t, bytes([version]) + flags.to_bytes(3, "big"), *payloads)

    ftyp = box(FTYP, b"heic", struct.pack(">I", 0), b"mif1", b"heic")

    hdlr = fullbox(b"hdlr", 0, 0, struct.pack(">I", 0), b"pict",
                   b"\0" * 12, b"\0")
    pitm = fullbox(b"pitm", 0, 0, struct.pack(">H", 1))
    infe1 = fullbox(b"infe", 2, 0, struct.pack(">H", 1), struct.pack(">H", 0),
                    b"hvc1", b"\0")
    infe2 = fullbox(b"infe", 2, 0, struct.pack(">H", 2), struct.pack(">H", 0),
                    b"hvc1", b"\0")
    iinf = fullbox(b"iinf", 0, 0, struct.pack(">H", 2), infe1, infe2)

    # ipco indices are 1-based: 1/2 the colour hvcC and ispe, 3/4 the alpha
    # hvcC and ispe, 5 the auxC that says the second item is alpha
    ipco = box(b"ipco",
               box(b"hvcC", color_hvcc),
               fullbox(b"ispe", 0, 0, struct.pack(">II", w, h)),
               box(b"hvcC", alpha_hvcc),
               fullbox(b"ispe", 0, 0, struct.pack(">II", w, h)),
               fullbox(b"auxC", 0, 0, b"urn:mpeg:hevc:2015:auxid:1\0"))
    ipma_body = struct.pack(">I", 2)
    ipma_body += struct.pack(">H", 1) + bytes([2, 1, 2])
    ipma_body += struct.pack(">H", 2) + bytes([3, 3, 4, 5])
    iprp = box(b"iprp", ipco, fullbox(b"ipma", 0, 0, ipma_body))

    def iref_entry(t, frm, tos):
        return box(t, struct.pack(">H", frm) + struct.pack(">H", len(tos)) +
                   b"".join(struct.pack(">H", to) for to in tos))

    refs = iref_entry(b"auxl", 2, [1])
    if prem:
        refs += iref_entry(b"prem", 1, [2])
    iref = fullbox(b"iref", 0, 0, refs)

    def make_iloc(offsets):
        body = bytes([0x44, 0x00]) + struct.pack(">H", 2)
        for item_id, off, length in ((1, offsets[0], len(color_payload)),
                                     (2, offsets[1], len(alpha_payload))):
            body += struct.pack(">H", item_id)
            body += struct.pack(">H", 0)   # construction method: file offset
            body += struct.pack(">H", 0)   # data reference index
            body += struct.pack(">H", 1)   # extent count
            body += struct.pack(">I", off)
            body += struct.pack(">I", length)
        return fullbox(b"iloc", 1, 0, body)

    meta_probe = fullbox(b"meta", 0, 0, hdlr, pitm, iinf, iprp, iref,
                         make_iloc((0, 0)))
    mdat_data_offset = len(ftyp) + len(meta_probe) + 8
    iloc = make_iloc((mdat_data_offset, mdat_data_offset + len(color_payload)))
    meta = fullbox(b"meta", 0, 0, hdlr, pitm, iinf, iprp, iref, iloc)
    mdat = box(b"mdat", color_payload + alpha_payload)

    with open(out_heic, "wb") as f:
        f.write(ftyp + meta + mdat)


def make_alpha(w, h):
    """Alpha plane with a soft gradient and a few fully opaque patches."""
    rnd = lcg(20261101)
    out = bytearray()
    for y in range(h):
        for x in range(w):
            v = 64 + (x * 160) // max(w - 1, 1)
            if (x // 12 + y // 12) % 3 == 0:
                v = 255
            v += next(rnd) % 16 - 8
            out.append(max(1, min(255, v)))
    return bytes(out)


def premultiply(rgb, alpha):
    out = bytearray()
    for i in range(0, len(rgb), 3):
        a = alpha[i // 3]
        for c in range(3):
            out.append((rgb[i + c] * a + 127) // 255)
    return bytes(out)


# linear BT.2020 -> XYZ (D65) and XYZ -> linear sRGB, the same matrices
# utils/colorspace.c uses.
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


def srgb_decode(x):
    return x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4


def srgb_encode(x):
    return 12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055


def bt2020_to_srgb(bgra):
    """Apply a BT.2020 primaries -> sRGB primaries transform in place."""
    out = bytearray(bgra)
    for i in range(0, len(bgra), 4):
        # the buffer is b, g, r; the matrix wants linear r, g, b
        lin = [srgb_decode(bgra[i + c] / 255.0) for c in (2, 1, 0)]
        xyz = [sum(BT2020_TO_XYZ[row][k] * lin[k] for k in range(3))
               for row in range(3)]
        rgb = [sum(XYZ_TO_SRGB[row][k] * xyz[k] for k in range(3))
               for row in range(3)]
        for c in range(3):
            v = max(0.0, min(1.0, srgb_encode(rgb[2 - c])))
            out[i + c] = int(v * 255.0 + 0.5)
    return bytes(out)


def decode_ref(stream, pix_fmt):
    r = subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "hevc",
                        "-i", stream, "-pix_fmt", pix_fmt, "-f", "rawvideo",
                        "-"], capture_output=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr.decode()[-3000:])
        raise SystemExit(f"reference decode failed: {stream}")
    return r.stdout


def make_color_samples(tmp):
    """BT.2020 primaries carried once by colr and once by the HEVC VUI.

    The decoder converts YUV with the stream matrix (709 in the colr case,
    2020nc in the VUI case) and then maps the BT.2020 primaries to sRGB; the
    reference applies the same primaries transform to ffmpeg's bgra frame."""
    w, h = 96, 96
    src = os.path.join(tmp, "color.raw")
    with open(src, "wb") as f:
        f.write(make_source("gradient", w, h, False))

    cases = [
        # name, VUI tags, colr (primaries, transfer, matrix)
        ("hevc_colr", ("bt709", "bt709", "bt709"), (9, 13, 9)),
        ("hevc_vui", ("bt2020", "iec61966-2-1", "bt2020nc"), None),
    ]
    for name, tags, colr in cases:
        out265 = os.path.join(tmp, f"{name}.265")
        # lossless keeps the two YUV->RGB paths close, so the tolerance only
        # has to cover the primaries transform rounding
        encode(src, w, h, False, "lossless=1", out265, tmp, tags=tags)
        with open(out265, "rb") as f:
            nals = split_annexb(f.read())
        heic = os.path.join(DATA, f"{name}.heic")
        wrap_heif(nals, w, h, False, heic, colr=colr)

        ref = bt2020_to_srgb(decode_ref(out265, "bgra"))
        if len(ref) != w * h * 4:
            raise SystemExit(f"{name}: colour reference has the wrong size")
        with open(os.path.join(DATA, f"{name}.ref"), "wb") as f:
            f.write(ref)
        print(f"{name}: {os.path.getsize(heic)} bytes heic, "
              f"{len(ref)} bytes ref")


def transform_pixels(data, w, h, props):
    """Apply irot/imir/clap to a packed BGRA reference, in ipma order."""
    for typ, val in props:
        if typ == b"irot":
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
                    out[(y * nw + x) * 4:(y * nw + x + 1) * 4] = \
                        data[(sy * w + sx) * 4:(sy * w + sx + 1) * 4]
            data, w, h = bytes(out), nw, nh
        elif typ == b"imir":
            out = bytearray(len(data))
            for y in range(h):
                for x in range(w):
                    if val == 0:        # top and bottom exchanged
                        sx, sy = x, h - 1 - y
                    else:               # left and right exchanged
                        sx, sy = w - 1 - x, y
                    out[(y * w + x) * 4:(y * w + x + 1) * 4] = \
                        data[(sy * w + sx) * 4:(sy * w + sx + 1) * 4]
            data = bytes(out)
        elif typ == b"clap":
            cw, ch, hoff, voff = val
            x0 = (w - cw) // 2 + hoff
            y0 = (h - ch) // 2 + voff
            out = bytearray()
            for y in range(ch):
                start = ((y0 + y) * w + x0) * 4
                out += data[start:start + cw * 4]
            data, w, h = bytes(out), cw, ch
    return data, w, h


def make_transform_samples(tmp):
    """irot, imir and clap associated with the primary item, one step each."""
    w, h = 96, 96
    src = os.path.join(tmp, "transform.raw")
    with open(src, "wb") as f:
        f.write(make_source("mixed", w, h, False))
    out265 = os.path.join(tmp, "transform.265")
    encode(src, w, h, False, "lossless=1", out265, tmp)
    with open(out265, "rb") as f:
        nals = split_annexb(f.read())
    base = decode_ref(out265, "bgra")

    cases = [
        ("hevc_irot90", [(b"irot", 1)]),
        ("hevc_imir0", [(b"imir", 0)]),
        ("hevc_clap", [(b"clap", (60, 40, 4, 2))]),
        ("hevc_rotimir", [(b"irot", 1), (b"imir", 0)]),
    ]
    for name, props in cases:
        encoded = []
        for typ, val in props:
            if typ == b"clap":
                cw, ch, hoff, voff = val
                encoded.append((typ, struct.pack(">8i", cw, 1, ch, 1,
                                                 hoff, 1, voff, 1)))
            else:
                encoded.append((typ, bytes([val])))
        heic = os.path.join(DATA, f"{name}.heic")
        wrap_heif(nals, w, h, False, heic, transforms=encoded)
        ref, _, _ = transform_pixels(base, w, h, props)
        with open(os.path.join(DATA, f"{name}.ref"), "wb") as f:
            f.write(ref)
        print(f"{name}: {os.path.getsize(heic)} bytes heic, "
              f"{len(ref)} bytes ref")


def make_protected_sample(tmp):
    """A primary item whose infe carries a protection index.

    The payload cannot be decrypted, so the decoder must hand back the
    zeroed container view at the ispe size instead of failing."""
    w, h = 96, 96
    src = os.path.join(tmp, "protected.raw")
    with open(src, "wb") as f:
        f.write(make_source("gradient", w, h, False))
    out265 = os.path.join(tmp, "protected.265")
    encode(src, w, h, False, "lossless=1", out265, tmp)
    with open(out265, "rb") as f:
        nals = split_annexb(f.read())

    heic = os.path.join(DATA, "hevc_protected.heic")
    wrap_heif(nals, w, h, False, heic, protected=True)
    with open(os.path.join(DATA, "hevc_protected.ref"), "wb") as f:
        f.write(b"\0" * (w * h * 4))
    print(f"hevc_protected: {os.path.getsize(heic)} bytes heic, "
          f"{w * h * 4} bytes ref")


def make_exif(orientation):
    """An Exif item payload: 4 byte TIFF offset, then a minimal TIFF."""
    tiff = b"II" + struct.pack("<H", 42) + struct.pack("<I", 8)
    tiff += struct.pack("<H", 1)
    tiff += struct.pack("<HHI", 0x0112, 3, 1)
    tiff += struct.pack("<H", orientation) + b"\x00\x00"
    tiff += struct.pack("<I", 0)
    return struct.pack(">I", 0) + tiff


def wrap_heif_meta(color_nals, w, h, with_pitm, out_heic):
    """Primary picture plus Exif, XMP and a thumbnail linked by iref.

    The thumbnail reuses the primary's payload through its own iloc entry.
    With with_pitm false there is no primary item, which exercises the
    fallback that must not treat the thumbnail as a picture."""
    hvcc, payload = split_cfg(color_nals, False)
    exif = make_exif(6)
    xmp = b'<x:xmpmeta xmlns:x="adobe:ns:meta/"/>'

    def box(t, *payloads):
        body = b"".join(payloads)
        return struct.pack(">I", 8 + len(body)) + t + body

    def fullbox(t, version, flags, *payloads):
        return box(t, bytes([version]) + flags.to_bytes(3, "big"), *payloads)

    def infe(item_id, typ, name, extra=b""):
        return fullbox(b"infe", 2, 0, struct.pack(">HH4s", item_id, 0, typ) +
                       name.encode() + b"\0" + extra)

    ftyp = box(FTYP, b"heic", struct.pack(">I", 0), b"mif1", b"heic")
    hdlr = fullbox(b"hdlr", 0, 0, struct.pack(">I", 0), b"pict",
                   b"\0" * 12, b"\0")
    pitm = fullbox(b"pitm", 0, 0, struct.pack(">H", 1)) if with_pitm else b""
    iinf = fullbox(b"iinf", 0, 0, struct.pack(">H", 4),
                   infe(1, b"hvc1", "primary"),
                   infe(2, b"Exif", "exif"),
                   infe(3, b"mime", "xmp",
                        b"application/rdf+xml\0\0"),
                   infe(4, b"hvc1", "thumb"))

    ipco = box(b"ipco", box(b"hvcC", hvcc),
               fullbox(b"ispe", 0, 0, struct.pack(">II", w, h)))
    ipma_body = struct.pack(">I", 2)
    ipma_body += struct.pack(">H", 1) + bytes([2, 1, 2])
    ipma_body += struct.pack(">H", 4) + bytes([2, 1, 2])
    iprp = box(b"iprp", ipco, fullbox(b"ipma", 0, 0, ipma_body))

    def iref_entry(t, frm, to):
        return box(t, struct.pack(">HHH", frm, 1, to))

    iref = fullbox(b"iref", 0, 0, iref_entry(b"cdsc", 2, 1),
                   iref_entry(b"cdsc", 3, 1), iref_entry(b"thmb", 4, 1))

    def make_iloc(offsets):
        body = bytes([0x44, 0x00]) + struct.pack(">H", 4)
        for item_id, off, length in ((1, offsets[0], len(payload)),
                                     (2, offsets[1], len(exif)),
                                     (3, offsets[2], len(xmp)),
                                     (4, offsets[0], len(payload))):
            body += struct.pack(">H", item_id)
            body += struct.pack(">H", 0)   # construction method: file offset
            body += struct.pack(">H", 0)   # data reference index
            body += struct.pack(">H", 1)   # extent count
            body += struct.pack(">I", off)
            body += struct.pack(">I", length)
        return fullbox(b"iloc", 1, 0, body)

    meta_probe = fullbox(b"meta", 0, 0, hdlr, pitm, iinf, iprp, iref,
                         make_iloc((0, 0, 0)))
    data_off = len(ftyp) + len(meta_probe) + 8
    iloc = make_iloc((data_off, data_off + len(payload),
                      data_off + len(payload) + len(exif)))
    meta = fullbox(b"meta", 0, 0, hdlr, pitm, iinf, iprp, iref, iloc)
    mdat = box(b"mdat", payload + exif + xmp)

    with open(out_heic, "wb") as f:
        f.write(ftyp + meta + mdat)


def make_metadata_samples(tmp):
    w, h = 96, 96
    src = os.path.join(tmp, "meta.raw")
    with open(src, "wb") as f:
        f.write(make_source("gradient", w, h, False))
    out265 = os.path.join(tmp, "meta.265")
    encode(src, w, h, False, "lossless=1", out265, tmp)
    with open(out265, "rb") as f:
        nals = split_annexb(f.read())
    ref = decode_ref(out265, "bgra")

    for name, with_pitm in (("hevc_meta", True),
                            ("hevc_meta_nopitm", False)):
        heic = os.path.join(DATA, f"{name}.heic")
        wrap_heif_meta(nals, w, h, with_pitm, heic)
        if with_pitm:
            with open(os.path.join(DATA, f"{name}.ref"), "wb") as f:
                f.write(ref)
        print(f"{name}: {os.path.getsize(heic)} bytes heic")


def make_wide_samples(tmp):
    """10 and 12 bit streams: the output keeps 16 bit channels.

    The colour samples go through 4:2:0, so the reference differs by
    swscale's chroma upsampling; the monochrome sample pins the sample
    scaling without that noise."""
    w, h = 96, 96
    rgb_src = os.path.join(tmp, "wide.raw")
    with open(rgb_src, "wb") as f:
        f.write(make_source("gradient", w, h, False))
    mono_src = os.path.join(tmp, "wide_mono.raw")
    with open(mono_src, "wb") as f:
        f.write(make_source("mixed", w, h, True))

    cases = (
        ("hevc_tenbit", 10, False, rgb_src),
        ("hevc_twelvebit", 12, False, rgb_src),
        ("hevc_tenbit_mono", 10, True, mono_src),
    )
    for name, depth, mono, src in cases:
        out265 = os.path.join(tmp, f"{name}.265")
        # lossless keeps the reconstruction itself out of the comparison;
        # the lossy high bit depth paths still lack the in-loop filters
        encode(src, w, h, mono, "lossless=1", out265, tmp, depth=depth,
               ref_pix="bgra64le")
        with open(out265, "rb") as f:
            nals = split_annexb(f.read())
        heic = os.path.join(DATA, f"{name}.heic")
        wrap_heif(nals, w, h, mono, heic, bit_depth=depth)

        ref = decode_ref(out265, "bgra64le")
        if len(ref) != w * h * 8:
            raise SystemExit(f"{name}: reference is {len(ref)} bytes")
        with open(os.path.join(DATA, f"{name}.ref"), "wb") as f:
            f.write(ref)
        print(f"{name}: {os.path.getsize(heic)} bytes heic, "
              f"{len(ref)} bytes ref")


def wrap_heif_iloc(color_nals, w, h, mode, out_heic):
    """Same picture stored through the iloc variants: several extents back
    to back with gaps, the meta idat box, and construction method 2 which
    the reader must refuse to follow."""
    hvcc, payload = split_cfg(color_nals, False)

    def box(t, *payloads):
        body = b"".join(payloads)
        return struct.pack(">I", 8 + len(body)) + t + body

    def fullbox(t, version, flags, *payloads):
        return box(t, bytes([version]) + flags.to_bytes(3, "big"), *payloads)

    ftyp = box(FTYP, b"heic", struct.pack(">I", 0), b"mif1", b"heic")
    hdlr = fullbox(b"hdlr", 0, 0, struct.pack(">I", 0), b"pict",
                   b"\0" * 12, b"\0")
    pitm = fullbox(b"pitm", 0, 0, struct.pack(">H", 1))
    infe = fullbox(b"infe", 2, 0, struct.pack(">HH4s", 1, 0, b"hvc1"),
                   b"primary\0")
    iinf = fullbox(b"iinf", 0, 0, struct.pack(">H", 1), infe)
    ipco = box(b"ipco", box(b"hvcC", hvcc),
               fullbox(b"ispe", 0, 0, struct.pack(">II", w, h)))
    ipma = fullbox(b"ipma", 0, 0, struct.pack(">I", 1),
                   struct.pack(">H", 1), bytes([2, 1, 2]))
    iprp = box(b"iprp", ipco, ipma)

    if mode == "extents":
        n = 3
        step = (len(payload) + n - 1) // n
        chunks = [payload[i * step:(i + 1) * step] for i in range(n)]
        chunks = [c for c in chunks if c]
        gap = b"PADPAD"
    elif mode == "idat":
        chunks = [payload]
        gap = b""
    else:   # method 2: the extent points at other items, not at bytes
        chunks = [payload]
        gap = b""

    def make_iloc(offsets, idat_len):
        method = {"extents": 0, "idat": 1, "method2": 2}[mode]
        body = bytes([0x44, 0x00]) + struct.pack(">H", 1)
        body += struct.pack(">H", 1)
        body += struct.pack(">H", method)
        body += struct.pack(">H", 0)
        body += struct.pack(">H", len(chunks))
        for off, chunk in zip(offsets, chunks):
            body += struct.pack(">I", off)
            body += struct.pack(">I", len(chunk))
        return fullbox(b"iloc", 1, 0, body)

    if mode == "idat":
        idat_body = payload
        idat = box(b"idat", idat_body)
        meta_probe = fullbox(b"meta", 0, 0, hdlr, pitm, iinf, iprp, idat,
                             make_iloc([0], len(idat_body)))
        data_off = len(ftyp) + len(meta_probe) + 8
        idat = box(b"idat", idat_body)
        iloc = make_iloc([0], len(idat_body))
        meta = fullbox(b"meta", 0, 0, hdlr, pitm, iinf, iprp, idat, iloc)
        mdat = box(b"mdat", b"")
    else:
        meta_probe = fullbox(b"meta", 0, 0, hdlr, pitm, iinf, iprp,
                             make_iloc([0] * len(chunks), 0))
        data_off = len(ftyp) + len(meta_probe) + 8
        offsets = []
        off = data_off
        for chunk in chunks:
            offsets.append(off)
            off += len(chunk) + len(gap)
        iloc = make_iloc(offsets, 0)
        meta = fullbox(b"meta", 0, 0, hdlr, pitm, iinf, iprp, iloc)
        mdat = box(b"mdat", gap.join(chunks) + gap)

    with open(out_heic, "wb") as f:
        f.write(ftyp + meta + mdat)


def make_iloc_samples(tmp):
    w, h = 96, 96
    src = os.path.join(tmp, "iloc.raw")
    with open(src, "wb") as f:
        f.write(make_source("gradient", w, h, False))
    out265 = os.path.join(tmp, "iloc.265")
    encode(src, w, h, False, "lossless=1", out265, tmp)
    with open(out265, "rb") as f:
        nals = split_annexb(f.read())
    ref = decode_ref(out265, "bgra")

    for name, mode in (("hevc_extents", "extents"),
                       ("hevc_idat", "idat"),
                       ("hevc_method2", "method2")):
        heic = os.path.join(DATA, f"{name}.heic")
        wrap_heif_iloc(nals, w, h, mode, heic)
        if mode != "method2":
            with open(os.path.join(DATA, f"{name}.ref"), "wb") as f:
                f.write(ref)
        print(f"{name}: {os.path.getsize(heic)} bytes heic")


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

    make_alpha_samples(tmp)
    make_color_samples(tmp)
    make_transform_samples(tmp)
    make_protected_sample(tmp)
    make_metadata_samples(tmp)
    make_wide_samples(tmp)
    make_iloc_samples(tmp)


def make_alpha_samples(tmp):
    """Colour item plus a monochrome alpha item, plain and premultiplied.

    Lossless coding keeps the two decoders in step, so the only thing the
    test measures is the alpha composition itself."""
    w, h = 96, 96
    alpha = make_alpha(w, h)
    color = make_source("gradient", w, h, False)

    alpha_src = os.path.join(tmp, "alpha.raw")
    with open(alpha_src, "wb") as f:
        f.write(alpha)
    alpha265 = os.path.join(tmp, "alpha.265")
    encode(alpha_src, w, h, True, "lossless=1", alpha265, tmp)
    with open(alpha265, "rb") as f:
        alpha_nals = split_annexb(f.read())
    alpha_ref = decode_ref(alpha265, "gray")
    if len(alpha_ref) != w * h:
        raise SystemExit(f"alpha reference is {len(alpha_ref)} bytes")

    for name, prem in (("hevc_alpha", False), ("hevc_alpha_prem", True)):
        rgb = premultiply(color, alpha) if prem else color
        color_src = os.path.join(tmp, f"{name}_color.raw")
        with open(color_src, "wb") as f:
            f.write(rgb)
        color265 = os.path.join(tmp, f"{name}_color.265")
        encode(color_src, w, h, False, "lossless=1", color265, tmp)
        with open(color265, "rb") as f:
            color_nals = split_annexb(f.read())
        color_ref = decode_ref(color265, "bgra")
        if len(color_ref) != w * h * 4:
            raise SystemExit(f"{name}: colour reference has the wrong size")

        heic = os.path.join(DATA, f"{name}.heic")
        wrap_heif_alpha(color_nals, alpha_nals, w, h, prem, heic)

        ref = bytearray(color_ref)
        for i in range(w * h):
            a = alpha_ref[i]
            ref[i * 4 + 3] = a
            if prem and a:
                for c in range(3):
                    v = (ref[i * 4 + c] * 255 + a // 2) // a
                    ref[i * 4 + c] = min(255, v)
        with open(os.path.join(DATA, f"{name}.ref"), "wb") as f:
            f.write(ref)
        print(f"{name}: {os.path.getsize(heic)} bytes heic, "
              f"{len(ref)} bytes ref")


if __name__ == "__main__":
    main()
