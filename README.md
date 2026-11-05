### For on-hands learning, with no third-party image codec dependency

For all kinds of images, we treat the data as two parts. `format meta` and `raw coded data`.
The format meta is typically a file header (or a set of ISOBMFF boxes for HEIF-style containers)
describing layout, colour, and geometry.  The raw coded data is quantized, transformed, and
entropy coded pixel payload.

There is no secret in image compression tech.  With knowledge of these two parts, we can decode
an image all by ourselves — that is exactly what this repository does: every decoder is hand
written against the ISO / RFC specification it targets.

A suggested learning order:

```
bmp -> pnm (pbm/pgm/ppm/pam) -> gif -> tga -> png -> tiff -> jpeg -> webp -> heif -> avif
```

Each step piles one more layer of complexity on the previous.  GIF/TGA/PNM can be read before
JPEG as they do not require DCT or entropy coding.

#### Supported File Formats

`Status` column convention:

| Value | Meaning |
|:-----|:--------|
| Full   | Reads pixels end-to-end and produces a decoded frame |
| Container + header only | Parses the file container and codestream headers, but does **not** reconstruct pixels |
| Partial | Produces pixels for a subset of the input space only |
| Skeleton | Probes and parses a handful of fields, no pixel output; may still have known correctness risks |
| Probe-only | Detects the format and walks the XML tree; does **not** rasterize |

| Name  |          Status          | Notes |
|:-----:|:------------------------:|:------|
|  GIF  |          Partial          | Graphics Interchange Format: static decode fully correct — animated frames are queued for downstream consumers but do **not** honour disposal methods (spec 89a methods 2/3/4), do **not** expose per-frame `duration_ms` from the delay-time field, do **not** skip transparent-colour indices, and do **not** de-interlace interlaced frames |
|  PNG  |            Full           | Portable Network Graphics (deflate + predictor filter) |
|  BMP  |            Full           | Microsoft BMP image (also used as the bundled writer) |
|  PBM  |            Full           | Portable BitMap (black/white) |
|  PGM  |            Full           | Portable GrayMap |
|  PPM  |            Full           | Portable PixelMap |
|  PAM  |            Full           | Portable Arbitrary Map |
|  TGA  |            Full           | Truevision Targa |
| TIFF  |            Full           | Tag Image File Format (little-endian II only) |
|  JPG  |            Full           | JPEG: baseline, sequential and progressive, 8-bit, DC and AC Huffman tables |
|  ICO  |            Full           | Windows icon directory + embedded BMP |
|  EXR  |            Full           | OpenEXR half/float pixel channels |
| WEBP  |            Full           | Google WebP: lossy (VP8 bitstream) + lossless (VP8L substream; meta Huffman and compressed alpha plane paths are intentionally unimplemented) |
| HEIF  |            Full           | HEIF / HEVC: still image (H.265 intra) plus animated-track decode from `moov`/`stbl`; animated sequences must be all-intra (limits under Known Limitations below) |
| AVIF  |            Full           | AVIF: still image plus animated-track decode from `moov`/`stbl` (all-intra only); built-in AV1 intra decoder (8/10/12-bit, film grain, PQ/HLG tone map with CLLI/MDCV metadata forwarding, irot/imir/clap, alpha plane, Exif/XMP/thumbnail; uniformly spaced multi-tile items supported, non-uniform layouts explicitly -ENOTSUP) |
|  JP2  | Container + header only  | JPEG 2000: parses JP2H codestream header markers (SIZ/COD/QCD/etc.) but does **not** run the DWT or entropy decoder — metadata only |
|  PSD  |          Partial         | Adobe PhotoShop: file header + layer info + **uncompressed (compression 0)** channel/image data only; RLE (`compression 1`) not implemented |
|  BPG  |          Skeleton        | Bellard's BPG: reads the file header, width, height and payload length only — no HEVC decode path wired in; `extension_present_flag` branches into an unimplemented `while(1)` stub |
|  SVG  |         Probe-only        | SVG 1.1: a hand-rolled XML lexer that builds an attribute tree; **no rasterizer** — used only for format detection and structure dump |

Everything above the line is actively used by the test suite; everything below ships so you can
see what a codec scaffold looks like before diving into the real thing.

[Format reference (legacy)](http://www.martinreddy.net/gfx/2d-hi.html)

#### Encoding Capability

Three encoders ship:

- **BMP** (`BMP_encode`) — always writes a 32-bit uncompressed RGB/BGR DIB (BI_RGB, top-down) with a 54-byte BITMAPINFOHEADER.
- **JPEG** (`JPG_encode`) — baseline sequential (SOF0), 8-bit, three components (YCbCr 4:2:0),
  standard JPEG Annex-K Huffman tables, quantized FDCT on 8×8 blocks, no restart markers.
- **PNG** (`PNG_encode`) — filter 0–4 adaptive (picks the row predictor with the smallest
  absolute-sum row delta), LZ77 + stored block or **fixed Huffman** deflate only (dynamic
  Huffman encoding is not implemented), colour type 2 / 6 only, no interlace.

Invoke either one through the `transcode` helper:

```
./transcode --codec jpg   some_input.png     # -> some_input.png_transcode.JPG
./transcode --codec bmp   some_input.jpg     # -> some_input.jpg_transcode.BMP
./transcode --codec png   some_input.jpg     # -> some_input.jpg_transcode.PNG
```

There is no encoder for HEIF, AVIF, WEBP, TIFF, GIF, EXR, JP2, PSD, or SVG — those are
decode-only in this tree.

#### Quick Start / Build

Prerequisites:

| Dependency                | Mandatory? | Where it comes from | Notes |
|:--------------------------|:----------:|:--------------------|:------|
| C11 compiler + CMake ≥ 3.11 |     Yes     | system toolchain    | |
| SDL 3 (headers + lib)     |     Yes     | System package **or** auto-fetched via FetchContent | CMake tries `find_package(SDL3 CONFIG QUIET)` first; if that misses it clones `libsdl-org/SDL` release-3.4.18 and builds a static copy automatically. |
| POSIX `xxd`               |  Optional   | system (usually from vim-common) | Required only for OpenCL / Vulkan backend: it embeds `.cl` and `.comp` files as C arrays at build time. |
| OpenCL runtime + headers  |  Optional   | system (e.g. `opencl-headers`, vendor ICD) | If found at configure time the OpenCL DCT backend is compiled in. |
| Vulkan SDK + `glslc`      |  Optional   | system (`vulkan-sdk`, `shaderc`) | If found, the Vulkan DCT backend is compiled in; `glslc` is required to compile the SPIR-V shader before `xxd` embeds it. |

Out-of-source build only — CMake aborts immediately if you run it from the repository root.

```bash
mkdir -p build && cd build

# minimal (SDL3 will be fetched automatically if missing)
cmake ..
make -j$(nproc)

# optional toggles
cmake -DFFPIC_ENABLE_SANITIZERS=ON  ..     # ASan + UBSan instrumentation
cmake -DBUILD_TESTING=ON             ..     # compile and register the ctest test suite
```

After build, the main binaries land in the build directory:

| Binary   | What it does | Invocation |
|:--------:|:------------:|:-----------|
| `picinfo` | Probes the format, prints all container/header metadata, optionally skips pixel decode | `./picinfo [--skip_decode \| -s] path/to/file` |
| `sdlshow` | Decodes one image and displays it in a SDL3 window — small images (<180×60) are centred inside a 480×360 canvas, larger images use their native dimensions; animations run to exhaustion | `./sdlshow path/to/file` |
| `transbmp` | Decodes one image and writes it to a 32-bit BMP (same directory, same name) | `./transbmp path/to/file` |
| `transcode` | Decodes one image and re-encodes it through a named encoder (see Encoding Capability above) | `./transcode --codec <bmp\|jpg\|png> path/to/file` |

Run the unit test suite with ctest (only after `-DBUILD_TESTING=ON`; the registered suite is
13 tests, including `test_tile_map` — a math-only unit test for HEVC CT-address remapping that
does **not** link libffpic and therefore does not exercise the actual decode path):

```bash
cd build && ctest --output-on-failure
```

#### Known Limitations

This is a teaching project, so gaps are documented in public rather than hidden:

**Container / format layer**

- HEIF and AVIF both support **animated tracks**: `moov`/`stbl` (stco/co64, stsz, stsc,
  stts) are parsed and the first track with an `hvc1`/`hev1` (HEIF) or `av01` (AVIF)
  sample entry is decoded frame-by-frame; per-frame timing comes from the `stts` sample delta
  field.  Limits: `edts` edit lists, `sdtp` sample dependency flags and multi-track grids
  are ignored, variable-frame-rate files have not been verified in practice, and **all
  animated sequences must be all-intra** — any P/B or AV1 inter frame returns `-ENOTSUP`
  from the decoder (`coding/av1.c:525` for AV1, HEVC P/B slices are never entered).
  Progressive download (`iden`, `iovl`) is still absent — every item payload must be present
  up-front in `mdat`.
- **AVIF** / AV1 is intra-frame only: no inter-frame reference, no reference picture queue,
  no `ref_frame_mvs`-style motion information.  The AV1 sequence header is parsed faithfully
  but the decoder ignores every syntax element that touches inter prediction.
  Uniformly spaced multi-tile items (e.g. `avif_tiles.heic` in the test suite) are fully
  supported — the decoder splits `tile_group` payloads per `mi_row_starts[]` / `mi_col_starts[]`
  and reconstructs each SB-range independently — but **non-uniform** tile layouts are an
  explicit `-ENOTSUP` at `coding/av1.c:653` (range-decoder-driven tile width CDF is
  unimplemented).  Multiple items (`grid`, `auxl` references) still reconstruct one at a time
  with no item-wide assembly.
  **OBU_METADATA** items of type `CLLI` (3) and `MDCV` (2) are parsed in the production
  decoder (`coding/av1.c:1312–1346`, metadata-type enum corrected to spec values in
  `format/avif.h:64–69`) and their numeric fields are forwarded through
  `avif_color_profile` → `cs_profile_set_hdr` to adjust the base brightness and Reinhard
  normalization of the PQ/HLG tone mapper.  No real-world sample ships a type-5 OBU, so the
  path is exercised only on hand-crafted bitstreams.
- **HEVC** is intra-frame only: only IDR and non-IDR intra-coded slices are decoded.  No
  inter-slice motion compensation, no lossless mode, no SEI parsing.  Motion-vector machinery
  (`sps_max_num_ref_pic_in_sps`, `inter_slice_flag`, etc.) is parsed from headers but never
  drives the reconstructor.  The CT-address remapping tables
  (`CtbAddrRsToTs`, `CtbAddrTsToRs`, `TileId`, `MinTbAddrZs`) are built correctly in
  `calc_pps_params`, exercised by `test_tile_map` (`tests/test_tile_map.c`), and have been run
  end-to-end on a real x265-encoded multi-tile bitstream in the CI suite
  (`tests/data/hevc_tiles_real.265`).  The in-loop **deblocking filter**
  (`coding/hevc.c:7276`) and **SAO filter** are implemented and invoked in spec order
  (`deblock → SAO` inside `inloop_filter` at `coding/hevc.c:7428`).  Simplifications: with no
  inter slice the boundary strength is always derived as B_S = 2, chroma filtering uses the
  3-tap weak path only, and the cross-slice boundary condition (spec `SlcAddrRs` comparison)
  is still a FIXME; loop filtering across tile and PCM boundaries is correctly gated.
- **GIF** static (single-frame) and **WEBP** lossy (VP8) decode fully cover the specified
  input space.  Remaining gaps in the animated / lossless paths are called out in the format
  table above (GIF: disposal methods, transparent-colour skip, de-interlace, per-frame
  duration; WEBP / VP8L: meta Huffman groups and the compressed alpha substream).
- **WEBP** lossy (VP8) alpha blending is also not implemented — a file with an alpha chunk
  (`ALPH`) is decoded with the alpha plane ignored.
- **JP2**, **PSD**, **BPG**, and **SVG** are only partially supported; see the format table for
  the exact scope of each.

**Scope — intentionally out of range**

- Inter-frame prediction is out of scope for both codecs that support it natively:
  HEVC P/B slices and AV1 inter frames (including inter-frames inside animated AVIF tracks)
  hit an explicit `-ENOTSUP` return path.  A still single frame is always intra by
  definition and is unaffected.

**Encoding**

- BMP, baseline sequential JPEG and PNG (filter-adaptive, fixed-Huffman deflate) are
  implemented — see the Encoding Capability section above.
- The JPEG encoder is 8-bit YCbCr 4:2:0 only, no restart markers, no progressive, no
  arithmetic coding.  The PNG encoder ships without dynamic Huffman trees, which makes
  large uniform-colour blocks ~10× larger than `libpng` output in the worst case.

If any of the above is on your to-do list, the relevant source file is usually the only place
you need to change.
