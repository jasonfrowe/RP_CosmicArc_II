#!/usr/bin/env python3
"""Build the Cosmic Arc II 5x7 pocket manual: screenshots, sprites, cover art, and PDF.

Usage:
    python3 tools/make_manual.py              # extract sprites, cover art, and print PDF
    python3 tools/make_manual.py --shots      # also capture all screenshots via rp6502-emu
    python3 tools/make_manual.py --no-pdf     # generate images only (skip Chrome PDF print)

Pure Python 3 standard library (no Pillow required). Uses Google Chrome headless for PDF.
"""

import argparse
import random
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "llvm-mos" / "release" / "RP_CosmicArc_II"
ROM = BUILD / "RP_CosmicArc_II.rp6502"
EMU = BUILD / "rp6502-emu"
ART = ROOT / "images"
MAN = ROOT / "docs" / "manual"
IMG = MAN / "img"
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"


# ---------------------------------------------------------------------------
# Pure-Python PNG reader/writer (supports 8-bit indexed, RGB, RGBA)
# ---------------------------------------------------------------------------

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png_rgba(path):
    """Read an 8-bit indexed, RGB, or RGBA PNG into (w, h, rgba_bytes)."""
    raw = Path(path).read_bytes()
    assert raw[:8] == b"\x89PNG\r\n\x1a\n", f"Not a PNG: {path}"
    pos = 8
    w = h = bpp = ctype = 0
    plte = None
    trns = None
    idat = bytearray()
    while pos < len(raw):
        length = struct.unpack(">I", raw[pos:pos + 4])[0]
        tag = raw[pos + 4:pos + 8]
        data = raw[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            w, h, bpp, ctype = struct.unpack(">IIBB", data[:10])
        elif tag == b"PLTE":
            plte = [tuple(data[i:i + 3]) for i in range(0, len(data), 3)]
        elif tag == b"tRNS":
            trns = list(data)
        elif tag == b"IDAT":
            idat.extend(data)
        elif tag == b"IEND":
            break

    assert bpp == 8, f"Unsupported bit depth {bpp} in {path}"
    ch = {2: 3, 3: 1, 6: 4}[ctype]
    stride = w * ch
    dec = zlib.decompress(bytes(idat))
    rows = []
    prev = bytearray(stride)
    idx = 0
    for _ in range(h):
        flt = dec[idx]
        idx += 1
        cur = bytearray(dec[idx:idx + stride])
        idx += stride
        if flt == 1:
            for i in range(ch, stride):
                cur[i] = (cur[i] + cur[i - ch]) & 0xFF
        elif flt == 2:
            for i in range(stride):
                cur[i] = (cur[i] + prev[i]) & 0xFF
        elif flt == 3:
            for i in range(stride):
                a = cur[i - ch] if i >= ch else 0
                cur[i] = (cur[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif flt == 4:
            for i in range(stride):
                a = cur[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                cur[i] = (cur[i] + _paeth(a, b, c)) & 0xFF
        rows.append(cur)
        prev = cur

    out = bytearray(w * h * 4)
    o = 0
    if ctype == 3:
        for row in rows:
            for p in row:
                r, g, b = plte[p]
                a = trns[p] if (trns is not None and p < len(trns)) else (0 if p == 0 else 255)
                out[o:o + 4] = bytes((r, g, b, a))
                o += 4
    elif ctype == 2:
        for row in rows:
            for x in range(0, stride, 3):
                out[o:o + 4] = bytes((row[x], row[x + 1], row[x + 2], 255))
                o += 4
    elif ctype == 6:
        for row in rows:
            out[o:o + stride] = row
            o += stride
    return w, h, out


def _chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png_rgba(path, w, h, rgba):
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw.extend(rgba[y * stride:(y + 1) * stride])
    png = b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + _chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def crop_rgba(w, h, rgba, x0, y0, cw, ch):
    out = bytearray(cw * ch * 4)
    for y in range(ch):
        src = ((y0 + y) * w + x0) * 4
        dst = y * cw * 4
        out[dst:dst + cw * 4] = rgba[src:src + cw * 4]
    return cw, ch, out


def scale_rgba(w, h, rgba, k):
    nw, nh = w * k, h * k
    out = bytearray(nw * nh * 4)
    for y in range(h):
        row = bytearray(nw * 4)
        for x in range(w):
            px = rgba[(y * w + x) * 4:(y * w + x + 1) * 4]
            for dx in range(k):
                pos = (x * k + dx) * 4
                row[pos:pos + 4] = px
        for dy in range(k):
            dst = (y * k + dy) * nw * 4
            out[dst:dst + nw * 4] = row
    return nw, nh, out


def blit_rgba(dw, dh, dst, sw, sh, src, dx, dy):
    for y in range(sh):
        ty = dy + y
        if ty < 0 or ty >= dh:
            continue
        for x in range(sw):
            tx = dx + x
            if tx < 0 or tx >= dw:
                continue
            si = (y * sw + x) * 4
            if src[si + 3] == 0:
                continue
            di = (ty * dw + tx) * 4
            dst[di:di + 4] = src[si:si + 4]


# ---------------------------------------------------------------------------
# 1. Capture live screenshots from rp6502-emu
# ---------------------------------------------------------------------------

def run_emu(script, save_dir=None, seed=42):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(script)
        path = f.name
    cmd = [str(EMU), str(ROM), "--mute", "--memseed", str(seed), "--script", path]
    if save_dir:
        cmd += ["--save-dir", str(save_dir)]
    subprocess.run(cmd, check=True)


def capture_shots():
    print("Capturing screenshots via rp6502-emu...")
    with tempfile.TemporaryDirectory() as sdir:
        # Voyage screens: Title, Slots, Sector, all 4 Starport tabs, Chart, Close-Up, Achievements
        run_emu(
            f'run 25\n'
            f'shot "{IMG}/title.png"\n'
            f'press z\nrun 2\nrelease z\nrun 12\n'
            f'shot "{IMG}/slots.png"\n'
            f'press z\nrun 2\nrelease z\nrun 18\n'
            f'shot "{IMG}/sector.png"\n'
            # Dock at Starport (item 0) and capture all 4 tabs
            f'press z\nrun 2\nrelease z\nrun 15\n'
            f'shot "{IMG}/port_trade.png"\n'
            f'press e\nrun 2\nrelease e\nrun 10\n'
            f'shot "{IMG}/port_bio.png"\n'
            f'press e\nrun 2\nrelease e\nrun 10\n'
            f'shot "{IMG}/port_yard.png"\n'
            f'press e\nrun 2\nrelease e\nrun 10\n'
            f'shot "{IMG}/port_bank.png"\n'
            # Undock with B (x)
            f'press x\nrun 2\nrelease x\nrun 12\n'
            # Warp twice to reveal a nice cluster on the Star Chart
            f'press s\nrun 2\nrelease s\nrun 5\n'
            f'press z\nrun 2\nrelease z\nrun 15\n'
            f'press s\nrun 2\nrelease s\nrun 5\n'
            f'press z\nrun 2\nrelease z\nrun 15\n'
            # Open Star Chart (R1 = e), move cursor to a neighbor so [A] WARP shows
            f'press e\nrun 2\nrelease e\nrun 15\n'
            f'shot "{IMG}/chart.png"\n'
            # Close-Up Circuit (R1 = e)
            f'press e\nrun 2\nrelease e\nrun 15\n'
            f'shot "{IMG}/closeup.png"\n'
            # Achievements (R1 = e)
            f'press e\nrun 2\nrelease e\nrun 15\n'
            f'shot "{IMG}/achieve.png"\n',
            save_dir=sdir,
            seed=42,
        )

    # Deep Space Combat (Quick Deep Space = 3rd item on Title)
    run_emu(
        f'run 20\n'
        f'press s\nrun 2\nrelease s\nrun 5\n'
        f'press s\nrun 2\nrelease s\nrun 5\n'
        f'press z\nrun 2\nrelease z\n'
        f'run 170\n'
        f'press z\nrun 10\nrelease z\nrun 10\n'
        f'shot "{IMG}/space_ark.png"\n'
        # Launch fighter (B = x) and wait for pirates/action
        f'press x\nrun 2\nrelease x\n'
        f'press d\nrun 25\nrelease d\n'
        f'press z\nrun 8\nrelease z\nrun 35\n'
        f'shot "{IMG}/space_fighter.png"\n',
        seed=77,
    )

    # Planetary Extraction (Quick Planet Run = 4th item on Title)
    run_emu(
        f'run 20\n'
        f'press s\nrun 2\nrelease s\nrun 4\n'
        f'press s\nrun 2\nrelease s\nrun 4\n'
        f'press s\nrun 2\nrelease s\nrun 4\n'
        f'press z\nrun 2\nrelease z\n'
        f'run 75\n'
        f'shot "{IMG}/planet_orbit.png"\n'
        # Launch shuttle (down) and fly right, activate tractor beam (z)
        f'press s\nrun 35\nrelease s\n'
        f'press d\nrun 45\nrelease d\n'
        f'press z\nrun 12\n'
        f'shot "{IMG}/planet_beam.png"\n'
        f'release z\nrun 5\n',
        seed=42,
    )

    # Crop HUD strips for numbered callout diagrams
    w, h, rgba = read_png_rgba(IMG / "space_ark.png")
    cw, ch, hud = crop_rgba(w, h, rgba, 0, 0, w, 16)
    sw, sh, hud2 = scale_rgba(cw, ch, hud, 2)
    write_png_rgba(IMG / "space_hud.png", sw, sh, hud2)

    w, h, rgba = read_png_rgba(IMG / "planet_beam.png")
    cw, ch, htop = crop_rgba(w, h, rgba, 0, 0, w, 16)
    sw, sh, htop2 = scale_rgba(cw, ch, htop, 2)
    write_png_rgba(IMG / "planet_hud_top.png", sw, sh, htop2)

    cw, ch, hbot = crop_rgba(w, h, rgba, 0, h - 16, w, 16)
    sw, sh, hbot2 = scale_rgba(cw, ch, hbot, 2)
    write_png_rgba(IMG / "planet_hud_bot.png", sw, sh, hbot2)


# ---------------------------------------------------------------------------
# 2. Extract crisp 4x sprite cutouts from images/*.png
# ---------------------------------------------------------------------------

def extract_sprites():
    print("Extracting crisp sprite cutouts...")
    cuts = [
        ("ark.png",       64, 24, 0,  "spr_ark.png"),
        ("saucer.png",    16, 12, 0,  "spr_saucer.png"),
        ("rock_l.png",    24, 24, 0,  "spr_rock_l.png"),
        ("rock_m.png",    16, 16, 0,  "spr_rock_m.png"),
        ("rock_s.png",    12, 12, 0,  "spr_rock_s.png"),
        ("raider.png",    16, 16, 0,  "spr_raider.png"),
        ("raider.png",    16, 16, 2,  "spr_raider_d.png"),
        ("gunship.png",   20, 20, 0,  "spr_gunship.png"),
        ("missile.png",   12, 12, 0,  "spr_missile.png"),
        ("pod.png",       12, 12, 0,  "spr_pod.png"),
        ("cargo.png",     12, 12, 0,  "spr_ore.png"),
        ("cargo.png",     12, 12, 1,  "spr_relic.png"),
        ("creatures.png", 12, 12, 0,  "spr_c0.png"),
        ("creatures.png", 12, 12, 2,  "spr_c1.png"),
        ("creatures.png", 12, 12, 4,  "spr_c2.png"),
        ("creatures.png", 12, 12, 6,  "spr_c3.png"),
        ("creatures.png", 12, 12, 8,  "spr_c4.png"),
        ("creatures.png", 12, 12, 10, "spr_c5.png"),
    ]
    for src_name, fw, fh, frame_idx, out_name in cuts:
        w, h, rgba = read_png_rgba(ART / src_name)
        cw, ch, sub = crop_rgba(w, h, rgba, frame_idx * fw, 0, fw, fh)
        sw, sh, big = scale_rgba(cw, ch, sub, 4)
        write_png_rgba(IMG / out_name, sw, sh, big)


# ---------------------------------------------------------------------------
# 3. Compose dramatic pixel-art Cover Scene (320x168 -> 640x336)
# ---------------------------------------------------------------------------

def make_cover():
    print("Composing cover artwork...")
    W, H = 240, 136
    buf = bytearray(W * H * 4)
    # Deep space gradient
    for y in range(H):
        for x in range(W):
            i = (y * W + x) * 4
            buf[i] = 6 + (y // 18)
            buf[i + 1] = 10 + (y // 16)
            buf[i + 2] = 28 + (y // 8)
            buf[i + 3] = 255

    # Starfield
    rng = random.Random(6502)
    star_cols = [(240, 248, 255), (140, 190, 255), (255, 220, 140), (120, 130, 170)]
    for _ in range(130):
        sx, sy = rng.randrange(W), rng.randrange(H)
        r, g, b = rng.choice(star_cols)
        i = (sy * W + sx) * 4
        buf[i:i + 4] = bytes((r, g, b, 255))

    def load_frame(name, fw, fh, idx, k=1):
        w, h, rgba = read_png_rgba(ART / name)
        cw, ch, sub = crop_rgba(w, h, rgba, idx * fw, 0, fw, fh)
        return scale_rgba(cw, ch, sub, k) if k > 1 else (cw, ch, sub)

    # Asteroids scattered
    for name, fw, fh, fi, px, py in [
        ("rock_l.png", 24, 24, 0, 14, 14),
        ("rock_m.png", 16, 16, 1, 198, 98),
        ("rock_s.png", 12, 12, 2, 118, 108),
        ("rock_s.png", 12, 12, 0, 148, 12),
    ]:
        sw, sh, s = load_frame(name, fw, fh, fi, 1)
        blit_rgba(W, H, buf, sw, sh, s, px, py)

    # Laser bolts from Ark toward pirates
    for lx in range(100, 148):
        ly = 56 - (lx - 100) // 3
        for dy in (0, 1):
            i = ((ly + dy) * W + lx) * 4
            buf[i:i + 4] = bytes((255, 240, 120, 255) if dy == 0 else (255, 160, 40, 255))

    # Cosmic Ark (hero, 64x24!) + Fighter Saucer underneath
    sw, sh, s = load_frame("ark.png", 64, 24, 0, 1)
    blit_rgba(W, H, buf, sw, sh, s, 28, 46)
    sw, sh, s = load_frame("saucer.png", 16, 12, 0, 1)
    blit_rgba(W, H, buf, sw, sh, s, 52, 82)

    # TIE-Fighter Raiders & Pirate Heavy Gunship swooping in
    sw, sh, s = load_frame("gunship.png", 20, 20, 0, 2)
    blit_rgba(W, H, buf, sw, sh, s, 174, 28)
    sw, sh, s = load_frame("raider.png", 16, 16, 2, 1)
    blit_rgba(W, H, buf, sw, sh, s, 150, 62)
    sw, sh, s = load_frame("raider.png", 16, 16, 0, 1)
    blit_rgba(W, H, buf, sw, sh, s, 186, 76)
    sw, sh, s = load_frame("missile.png", 12, 12, 2, 1)
    blit_rgba(W, H, buf, sw, sh, s, 134, 84)

    # Captured exotic creature + glowing pod in bottom-left accent
    sw, sh, s = load_frame("pod.png", 12, 12, 0, 1)
    blit_rgba(W, H, buf, sw, sh, s, 22, 106)

    bw, bh, big = scale_rgba(W, H, buf, 2)
    write_png_rgba(IMG / "cover.png", bw, bh, big)


# ---------------------------------------------------------------------------
# 4. Print 5x7 PDF via Chrome Headless
# ---------------------------------------------------------------------------

def build_pdf():
    html = MAN / "manual.html"
    out = MAN / "Cosmic_Arc_II_Manual_5x7.pdf"
    print(f"Printing {out.relative_to(ROOT)} via headless Chrome...")
    subprocess.run(
        [
            CHROME,
            "--headless=new",
            "--disable-gpu",
            "--no-pdf-header-footer",
            "--run-all-compositor-stages-before-draw",
            "--virtual-time-budget=4000",
            f"--print-to-pdf={out}",
            html.as_uri(),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    print(f"Created {out.relative_to(ROOT)} ({out.stat().st_size:,} bytes)")


def main():
    ap = argparse.ArgumentParser(description="Build Cosmic Arc II 5x7 manual assets and PDF.")
    ap.add_argument("--shots", action="store_true", help="capture fresh screenshots via rp6502-emu")
    ap.add_argument("--no-pdf", action="store_true", help="skip Chrome PDF generation")
    args = ap.parse_args()

    IMG.mkdir(parents=True, exist_ok=True)
    if args.shots:
        capture_shots()
    extract_sprites()
    make_cover()
    if not args.no_pdf:
        build_pdf()


if __name__ == "__main__":
    main()
