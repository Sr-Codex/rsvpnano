#!/usr/bin/env python3
"""Build public-domain cover fixtures + Pillow reference thumbnails for test_covers.

Source: Dracula EPUB cover (Project Gutenberg, public domain) from
companion/testdata/conversion/Dracula-epub.epub. Reference thumbnails use an
independent implementation (Pillow box filter on the same centred "cover" crop).
"""
import io, struct, zipfile
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "test/fixtures/covers"
TW, TH = 96, 142

def crop_fill(im):
    w, h = im.size
    if w * TH > h * TW: cw, ch = h * TW // TH, h
    else: cw, ch = w, w * TH // TW
    x, y = (w - cw) // 2, (h - ch) // 2
    return im.crop((x, y, x + cw, y + ch))

def to_rgb(im):
    if im.mode in ("I;16", "I"):  # Pillow saturates 16-bit -> RGB; PNG semantics = high byte
        raw = im.tobytes("raw", "I;16B")
        return Image.frombytes("L", im.size, bytes(raw[0::2])).convert("RGB")
    return im.convert("RGB")

def rcov(im):
    t = crop_fill(to_rgb(im)).resize((TW, TH), Image.Resampling.BOX)
    px = b"".join(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) for r, g, b in (t.get_flattened_data() if hasattr(t, "get_flattened_data") else t.getdata()))
    return struct.pack("<IHHII", 0x31564352, TW, TH, 0, 0) + px

def exact_rcov(path):
    """Bit-exact port of covers::decodeThumbnail for lossless PNG input (independent check of the C++)."""
    im = Image.open(path)
    if im.mode == "P": src = im.convert("RGB"); q = lambda r, g, b: (r >> 3, g >> 2, b >> 3)
    elif im.mode in ("I;16", "I"):
        raw = im.tobytes("raw", "I;16B")  # big-endian 16-bit, C++ takes the high byte
        v8 = bytes(raw[0::2]); src = Image.frombytes("L", im.size, v8).convert("RGB"); q = lambda r, g, b: (r >> 3, g >> 2, b >> 3)
    else: src = im; q = lambda r, g, b: (r >> 3, g >> 2, b >> 3)
    w, h = src.size
    if w * TH > h * TW: cw, ch = max(1, h * TW // TH), h
    else: cw, ch = w, max(1, w * TH // TW)
    cx, cy = (w - cw) // 2, (h - ch) // 2
    sums = [[0, 0, 0, 0] for _ in range(TW * TH)]
    px = src.load()
    for y in range(cy, cy + ch):
        for x in range(cx, cx + cw):
            v = px[x, y]
            if src.mode == "L": r = g = b = v; a = 255
            elif src.mode == "LA": r = g = b = v[0]; a = v[1]
            elif src.mode == "RGBA": r, g, b, a = v
            else: r, g, b = v[:3]; a = 255
            if a < 255: r = (r * a + 255 * (255 - a)) // 255; g = (g * a + 255 * (255 - a)) // 255; b = (b * a + 255 * (255 - a)) // 255
            R, G, B = q(r, g, b)
            i = ((y - cy) * TH // ch) * TW + (x - cx) * TW // cw
            s = sums[i]; s[0] += R; s[1] += G; s[2] += B; s[3] += 1
    out = b""
    for R, G, B, n in sums:
        assert n, "exact reference expects no empty cells"
        out += struct.pack("<H", ((R + n // 2) // n) << 11 | ((G + n // 2) // n) << 5 | ((B + n // 2) // n))
    return struct.pack("<IHHII", 0x31564352, TW, TH, 0, 0) + out

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    z = zipfile.ZipFile(ROOT / "companion/testdata/conversion/Dracula-epub.epub")
    name = next(n for n in z.namelist() if n.endswith("_cover.jpg"))
    src = Image.open(io.BytesIO(z.read(name))).convert("RGB")
    base = src.resize((src.width // 2, src.height // 2), Image.Resampling.LANCZOS)  # keep repo small
    cases = {
        "dracula-baseline.jpg": lambda f: base.save(f, "JPEG", quality=85),
        "dracula-progressive.jpg": lambda f: base.save(f, "JPEG", quality=85, progressive=True),
        "dracula.png": lambda f: base.reduce(2).save(f, "PNG", optimize=True),
        "dracula-wide.jpg": lambda f: base.rotate(90, expand=True).save(f, "JPEG", quality=85),
        "dracula-tiny.png": lambda f: base.resize((40, 60)).save(f, "PNG"),
        "dracula-tiny.jpg": lambda f: base.resize((40, 60)).save(f, "JPEG", quality=90),
        "dracula-palette.png": lambda f: base.reduce(2).quantize(64).save(f, "PNG"),
        "dracula-gray.png": lambda f: base.reduce(2).convert("L").save(f, "PNG"),
        "dracula-rgba.png": lambda f: base.reduce(2).convert("RGBA").save(f, "PNG"),
        "dracula-16bit.png": lambda f: base.reduce(2).convert("I;16").save(f, "PNG"),
        "dracula-pal4bit.png": lambda f: base.reduce(2).quantize(16).save(f, "PNG", bits=4),
    }
    for fname, write in cases.items():
        p = OUT / fname
        with open(p, "wb") as fh: write(fh)
        (OUT / (fname.rsplit(".", 1)[0] + ".ref.rcov")).write_bytes(rcov(Image.open(p)))
        print(f"{fname:26} {Image.open(p).size} {p.stat().st_size // 1024} KB")
    for fname in ("dracula.png", "dracula-palette.png", "dracula-pal4bit.png", "dracula-gray.png",
                  "dracula-rgba.png", "dracula-16bit.png"):
        (OUT / (fname.rsplit(".", 1)[0] + ".exact.rcov")).write_bytes(exact_rcov(OUT / fname))
    # Pillow cannot write Adam7: dracula-interlaced.png is made with ImageMagick
    # (`magick dracula.png -interlace PNG dracula-interlaced.png`) and must be REJECTED cleanly.
    (OUT / "SOURCE.txt").write_text("Derived from the Project Gutenberg Dracula EPUB cover (public domain), "
                                    "via tools/make_cover_fixtures.py. *.ref.rcov = Pillow BOX-filter references.\n")

main()
