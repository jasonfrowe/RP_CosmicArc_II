#!/usr/bin/env python3
"""Generate 4bpp indexed PNG sprites, tiles, and src/gen/assets.h for Cosmic Arc II (RP6502).

Standard library only (no Pillow needed): writes clean non-interlaced 8-bit
indexed PNGs (values 0..15) compatible with tools/png2bin.py.
"""

import math
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMG = ROOT / "images"
GEN = ROOT / "src" / "gen"


def rgb555_a(r, g, b, opaque=True):
    """RP6502 16-bit color: BBBBB GGGGG A RRRRR."""
    c = (((b >> 3) & 31) << 11) | (((g >> 3) & 31) << 6) | ((r >> 3) & 31)
    if opaque:
        c |= (1 << 5)
    return c


def write_indexed_png(path, rows, palette_rgb):
    h = len(rows)
    w = len(rows[0])
    pal = list(palette_rgb) + [(0, 0, 0)] * (16 - len(palette_rgb))
    plte = b"".join(bytes(c) for c in pal[:16])

    raw = bytearray()
    for r in rows:
        raw.append(0)
        raw.extend(r)

    def chunk(tag, data):
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"PLTE", plte)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)


HEX_MAP = {c: i for i, c in enumerate(".123456789ABCDEF")}


def parse_frame(text, w, h):
    lines = [ln.strip() for ln in text.strip().splitlines() if ln.strip()]
    assert len(lines) == h, f"Expected {h} rows, got {len(lines)}"
    out = []
    for y, ln in enumerate(lines):
        assert len(ln) == w, f"Row {y}: expected width {w}, got {len(ln)}: {ln!r}"
        out.append([HEX_MAP[ch] for ch in ln])
    return out


def hstack_frames(frames):
    h = len(frames[0])
    out = [[] for _ in range(h)]
    for f in frames:
        for y in range(h):
            out[y].extend(f[y])
    return out


# ---------------------------------------------------------------------------
# Palettes (16 entries each; index 0 is transparent except PAL_SPACE_BG)
# ---------------------------------------------------------------------------

PALETTES = {
    "PAL_HUD": [
        (0, 0, 0),       # 0 transparent
        (12, 18, 36),    # 1 C_PANEL (deep cockpit navy)
        (248, 250, 255), # 2 C_WHITE
        (255, 212, 56),  # 3 C_GOLD
        (64, 224, 255),  # 4 C_CYAN
        (64, 236, 108),  # 5 C_GREEN
        (255, 68, 72),   # 6 C_RED
        (255, 148, 44),  # 7 C_ORANGE
        (216, 104, 255), # 8 C_MAGENTA
        (172, 186, 212), # 9 C_GREY (brushed steel)
        (64, 80, 114),   # 10 C_DIM (dark steel)
        (24, 38, 72),    # 11 C_FRAME (recessed glass well)
        (36, 148, 160),  # 12 C_TEAL (dark cyan trim)
        (44, 112, 200),  # 13 C_BLUE (bevel mid)
        (32, 128, 64),   # 14 C_DARKGREEN (bar shadow)
        (168, 36, 44),   # 15 C_DARKRED (bar shadow)
    ],
    "PAL_ARK": [
        (0, 0, 0),
        (48, 58, 86),
        (92, 108, 148),
        (148, 168, 212),
        (220, 232, 255),
        (24, 32, 54),
        (255, 210, 64),
        (255, 244, 168),
        (48, 224, 255),
        (255, 72, 88),
        (80, 248, 128),
        (255, 160, 40),
        (120, 48, 64),
        (32, 112, 148),
        (180, 120, 32),
        (255, 255, 255),
    ],
    "PAL_ROCK": [
        (0, 0, 0),       # 0 transparent
        (96, 66, 48),    # 1 shadow rock (brighter, clearly visible on black)
        (152, 110, 76),  # 2 mid-dark crag
        (204, 162, 118), # 3 bright tan facet
        (248, 224, 184), # 4 sunlit cream ridge
        (52, 34, 26),    # 5 deep crater / outline
        (255, 255, 255), # 6 boom core white
        (255, 240, 112), # 7 boom yellow
        (255, 148, 36),  # 8 boom orange
        (220, 56, 32),   # 9 boom red
        (128, 40, 32),   # A boom dark red
        (108, 100, 116), # B smoke grey
        (172, 168, 184), # C mineral glint / smoke
        (112, 232, 255), # D laser cyan
        (255, 255, 255), # E laser core
        (255, 224, 80),  # F aim gold
    ],
    "PAL_SAUCER": [
        (0, 0, 0),
        (40, 96, 136),
        (80, 168, 216),
        (152, 224, 255),
        (240, 252, 255),
        (255, 216, 64),
        (255, 72, 96),
        (80, 248, 128),
        (64, 224, 255),
        (168, 112, 255),
        (255, 255, 255),
        (255, 160, 48),
        (24, 52, 80),
        (192, 64, 224),
        (64, 140, 255),
        (255, 244, 160),
    ],
    "PAL_PIRATE": [
        (0, 0, 0),       # 0 transparent
        (88, 16, 32),    # 1 dark crimson armor
        (176, 32, 52),   # 2 mid crimson
        (248, 76, 88),   # 3 bright crimson highlight
        (255, 176, 136), # 4 hot edge
        (40, 46, 64),    # 5 obsidian steel
        (88, 98, 126),   # 6 mid steel
        (156, 168, 198), # 7 bright steel
        (255, 224, 64),  # 8 thruster yellow
        (255, 112, 24),  # 9 thruster orange
        (36, 196, 96),   # A pod green
        (132, 255, 172), # B pod bright
        (20, 88, 52),    # C pod dark
        (255, 255, 255), # D white core
        (216, 64, 255),  # E plasma violet
        (64, 232, 255),  # F glowing alien core cyan
    ],
    "PAL_CREATURES": [
        (0, 0, 0),
        (40, 180, 80),
        (128, 248, 144),
        (232, 160, 40),
        (255, 224, 112),
        (232, 72, 112),
        (255, 152, 176),
        (144, 80, 232),
        (208, 152, 255),
        (48, 192, 232),
        (152, 240, 255),
        (240, 96, 208),
        (140, 92, 48),
        (216, 164, 72),
        (255, 255, 255),
        (24, 24, 40),
    ],
    "PAL_SPACE_BG": [
        (0, 0, 0),
        (255, 255, 255),
        (128, 184, 255),
        (255, 224, 128),
        (96, 120, 160),
        (44, 32, 72),
        (28, 48, 76),
        (68, 40, 88),
        (20, 20, 36),
        (180, 210, 255),
        (100, 130, 190),
        (220, 190, 110),
        (64, 88, 120),
        (255, 160, 160),
        (160, 255, 220),
        (255, 255, 255),
    ],
}

WORLDS = [
    ("VERDA", (10, 22, 40), (24, 64, 52), (56, 176, 84), (96, 68, 44), (58, 40, 28), (112, 128, 148), (176, 192, 216)),
    ("RUBRA", (28, 12, 18), (76, 32, 32), (192, 76, 52), (120, 44, 34), (68, 24, 22), (128, 112, 116), (192, 168, 172)),
    ("GLACI", (12, 24, 48), (48, 88, 136), (168, 224, 255), (84, 136, 188), (44, 76, 120), (136, 168, 196), (216, 240, 255)),
    ("UMBRA", (16, 10, 34), (52, 32, 88), (136, 84, 204), (76, 44, 116), (40, 22, 68), (112, 104, 144), (176, 168, 216)),
    ("ARENA", (28, 20, 14), (96, 68, 36), (224, 176, 84), (156, 104, 48), (92, 56, 28), (136, 120, 104), (208, 188, 160)),
    ("TOXIS", (12, 24, 16), (44, 76, 36), (152, 216, 56), (84, 108, 36), (44, 60, 22), (108, 124, 104), (168, 192, 160)),
]

for idx, (name, sky, hill, top, soil, rock, tow_d, tow_l) in enumerate(WORLDS):
    PALETTES[f"PAL_WORLD_{idx}"] = [
        (0, 0, 0),
        sky, hill, top, soil, rock, tow_d, tow_l,
        (255, 255, 255), (168, 208, 255), (255, 80, 64), (255, 224, 80),
        (40, 44, 56), (255, 160, 64), (80, 240, 180), (255, 255, 255),
    ]


# ---------------------------------------------------------------------------
# Sprite Builders
# ---------------------------------------------------------------------------

def make_ark():
    frames = []
    for phase in range(2):
        grid = [[0] * 64 for _ in range(32)]

        def hspan(y, x0, x1, c):
            for x in range(max(0, x0), min(64, x1)):
                grid[y][x] = c

        dome = [(3, 28, 36, 7), (4, 25, 39, 7), (5, 23, 41, 6),
                (6, 21, 43, 6), (7, 20, 44, 0xE), (8, 18, 46, 2)]
        for y, x0, x1, c in dome:
            hspan(y, x0, x1, c)
        for x in (24, 28, 32, 36, 40):
            grid[6][x] = 8
            grid[6][x + 1] = 0xF

        hull_up = [(9, 14, 50, 3), (10, 10, 54, 4), (11, 6, 58, 3),
                   (12, 3, 61, 2), (13, 1, 63, 3)]
        for y, x0, x1, c in hull_up:
            hspan(y, x0, x1, c)

        hspan(14, 0, 64, 4)
        hspan(15, 0, 64, 1)
        hspan(16, 1, 63, 2)
        light_cols = [8, 9, 0xA, 6]
        for i in range(15):
            lx = 3 + i * 4
            c = light_cols[(i + phase * 2) % 4]
            grid[15][lx] = c
            grid[15][lx + 1] = c

        hull_dn = [(17, 3, 61, 3), (18, 6, 58, 2), (19, 10, 54, 2),
                   (20, 14, 50, 1), (21, 18, 46, 1), (22, 20, 44, 5)]
        for y, x0, x1, c in hull_dn:
            hspan(y, x0, x1, c)
        for x in (16, 24, 40, 48):
            grid[11][x] = 1
            grid[12][x] = 1
            grid[18][x] = 1
            grid[19][x] = 1

        hspan(23, 20, 26, 2)
        hspan(23, 26, 38, 5)
        hspan(23, 38, 44, 2)
        hspan(24, 21, 25, 3)
        hspan(24, 25, 39, 5)
        hspan(24, 39, 43, 3)
        hspan(25, 22, 25, 1)
        hspan(25, 39, 42, 1)
        grid[24][25] = 8 if phase == 0 else 6
        grid[24][38] = 8 if phase == 0 else 6
        frames.append(grid)
    return frames


def _poly_rock(size, radii, n_frames, craters):
    """Build jagged, faceted, tumbling 4bpp asteroids with dark outlines & rocky crags."""
    frames = []
    cx = cy = (size - 1) / 2.0
    n_v = len(radii)
    for f in range(n_frames):
        rot = f * (math.pi * 2.0 / n_frames)
        grid = [[0] * size for _ in range(size)]
        inside = [[False] * size for _ in range(size)]
        for y in range(size):
            for x in range(size):
                dx = x - cx
                dy = y - cy
                ang = (math.atan2(dy, dx) - rot) % (2.0 * math.pi)
                pos = ang / (2.0 * math.pi) * n_v
                i0 = int(pos) % n_v
                i1 = (i0 + 1) % n_v
                t = pos - int(pos)
                r_lim = radii[i0] * (1.0 - t) + radii[i1] * t
                if math.hypot(dx, dy) <= r_lim:
                    inside[y][x] = True

        # Shade interior with directional facets, crevices, and dark outline
        for y in range(size):
            for x in range(size):
                if not inside[y][x]:
                    continue
                # Check if edge pixel -> dark crevice outline (5) or ridge highlight (4)
                edge = False
                for ox, oy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    nx, ny = x + ox, y + oy
                    if nx < 0 or nx >= size or ny < 0 or ny >= size or not inside[ny][nx]:
                        edge = True
                        break
                dx = x - cx
                dy = y - cy
                # Light from top-left (-0.7, -0.7)
                dot = -(dx * 0.72 + dy * 0.70) / (size * 0.45)
                # Craggy noise
                crag = (((x * 7 + y * 13 + f * 5) ^ (x * 3 - y * 5)) & 7) / 14.0 - 0.25
                val = dot + crag
                if edge:
                    c = 3 if val > 0.25 else 5
                else:
                    if val > 0.45:
                        c = 4
                    elif val > 0.05:
                        c = 3
                    elif val > -0.35:
                        c = 2
                    else:
                        c = 1
                    # Subtle diagonal ridge / fracture line
                    rx = dx * math.cos(-rot) - dy * math.sin(-rot)
                    ry = dx * math.sin(-rot) + dy * math.cos(-rot)
                    if abs(rx + ry * 0.6) < 0.75 and size >= 12:
                        c = 1 if c > 1 else 5
                    # Impact craters (with lit rim on bottom-right, shadow on top-left)
                    for crx, cry, crr in craters:
                        dc = math.hypot(rx - crx, ry - cry)
                        if dc < crr:
                            c = 5 if (rx - crx + ry - cry) < 0 else 1
                        elif dc < crr + 1.0 and (rx - crx + ry - cry) > 0.4:
                            c = 4
                grid[y][x] = c
        frames.append(grid)
    return frames


def make_rock_l():
    # 24x24 bold craggy 10-vertex asteroid, 4 tumbling frames
    radii = [10.6, 8.0, 11.2, 9.6, 7.6, 11.0, 9.0, 10.4, 7.4, 9.8]
    craters = [(-3.2, -2.5, 2.3), (3.4, 2.6, 2.0), (-1.8, 3.8, 1.5)]
    return _poly_rock(24, radii, 4, craters)


def make_rock_m():
    # 16x16 jagged 9-vertex asteroid, 4 tumbling frames
    radii = [7.4, 5.6, 7.6, 6.2, 7.4, 5.4, 7.2, 6.4, 5.6]
    craters = [(-2.0, -1.4, 1.5), (2.2, 1.8, 1.3)]
    return _poly_rock(16, radii, 4, craters)


def make_rock_s():
    # 12x12 bright sharp rocky chunk, 4 tumbling frames (clearly visible!)
    radii = [5.2, 3.8, 5.5, 4.1, 5.2, 3.6, 4.9]
    craters = [(-1.0, -0.8, 1.0)]
    return _poly_rock(12, radii, 4, craters)


def make_saucer():
    base = [
        ".....444444.....",
        "....34444443....",
        "...2333333332...",
        "..122222222221..",
        ".12333333333321.",
        "1222222222222221",
        "1CCCCCCCCCCCCCC1",
        ".12222222222221.",
        "..112222222211..",
        "....11CCCC11....",
        ".....1....1.....",
        "................",
    ]
    lights = [5, 6, 7, 4]
    frames = []
    for f in range(4):
        g = parse_frame("\n".join(base), 16, 12)
        for i, lx in enumerate((2, 5, 8, 11, 13)):
            g[5][lx] = lights[(i + f) % 4]
        frames.append(g)
    return frames


def make_raiders():
    """16x16 TIE-Fighter style twin-wing interceptor: 3 orientations (H, V, Diag)."""
    # Frame 0: Horizontal travel (solar panels top & bottom, central cockpit pod + struts)
    t_horiz = parse_frame("""
    ..577777777775..
    .56221111112265.
    ..566666666665..
    .......57.......
    ......5676......
    .....562365.....
    ....562FF365....
    ....562FD365....
    ....562FF365....
    .....562365.....
    ......5676......
    .......57.......
    ..566666666665..
    .56221111112265.
    ..577777777775..
    ................
    """, 16, 16)

    # Frame 1: Vertical travel (solar panels left & right, central cockpit pod + struts)
    t_vert = parse_frame("""
    ................
    .56..........65.
    5625........5265
    5725........5275
    5715...56...5175
    5715..5676..5175
    5715.562365.5175
    5716562FF3656175
    5716562FD3656175
    5715.562365.5175
    5715..5676..5175
    5715...56...5175
    5725........5275
    5625........5265
    .56..........65.
    ................
    """, 16, 16)

    # Frame 2: Diagonal travel (angled twin solar panels flanking central cockpit pod)
    t_diag = parse_frame("""
    ........576.....
    .......56265....
    ......562165....
    .....562165.....
    ....562165.56...
    ....5665..5676..
    .....55..562365.
    ........562FF365
    .......562FD365.
    .56...562365.55.
    5625...5676.5665
    57265...56.56126
    .561265...561265
    ..561265.561265.
    ...5626556265...
    ....576..576....
    """, 16, 16)

    return [t_horiz, t_vert, t_diag]


def make_gunships():
    """20x20 armored alien heavy gunship cruiser (symmetric octagonal hull, 2 animated frames)."""
    frames = []
    for ph in range(2):
        core = "F" if ph == 0 else "D"
        ring = "E" if ph == 0 else "F"
        pod  = "8" if ph == 0 else "9"
        txt = f"""
        ......56777765......
        ....562333333265....
        ...56234422443265...
        ..5623211551123265..
        .562321567765123265.
        .52321562{ring}{ring}26512325.
        56221562{ring}{core}{core}{ring}26512265
        56{pod}2156{ring}{core}DD{core}{ring}6512{pod}65
        56{pod}2156{ring}{core}DD{core}{ring}6512{pod}65
        56221562{ring}{core}{core}{ring}26512265
        .52321562{ring}{ring}26512325.
        .562321567765123265.
        ..5623211551123265..
        ...56234422443265...
        ....562333333265....
        ......56777765......
        ........5665........
        ........{pod}88{pod}........
        .........99.........
        ....................
        """
        frames.append(parse_frame(txt, 20, 20))
    return frames


def make_missiles():
    """12x12 seeker missile: 3 base orientations (R, U, UR) -> 8-way via HFLIP/VFLIP."""
    m_right = parse_frame("""
    ............
    ............
    ............
    ....56......
    .98567233...
    98D56723443.
    98D56723443.
    .98567233...
    ....56......
    ............
    ............
    ............
    """, 12, 12)
    m_up = parse_frame("""
    .....33.....
    ....3443....
    ....2332....
    ....6776....
    ...567765...
    ...567765...
    ....5665....
    .....8D.....
    .....98.....
    .....99.....
    ............
    ............
    """, 12, 12)
    m_ur = parse_frame("""
    .........33.
    ........3443
    .......2343.
    ......6732..
    .....5672...
    ....5676....
    ...8D55.....
    ..988.......
    ..99........
    ............
    ............
    ............
    """, 12, 12)
    return [m_right, m_up, m_ur]


def make_pods():
    """12x12 pulsing emerald energy salvage pod, 2 frames."""
    p0 = parse_frame("""
    ............
    ...CCCCCC...
    ..CAAAAAAB..
    .CAABBBBBAC.
    .CABDDDDDAC.
    .CABD..DDAC.
    .CABD..DDAC.
    .CABDDDDDAC.
    .CAABBBBBAC.
    ..CAAAAAAB..
    ...CCCCCC...
    ............
    """, 12, 12)
    p1 = parse_frame("""
    ............
    ...AAAAAA...
    ..ABBBBBBD..
    .ABBDDDDDBC.
    .ABDDFFDDAC.
    .ABDF..FDAC.
    .ABDF..FDAC.
    .ABDDFFDDAC.
    .ABBDDDDDBC.
    ..ABBBBBBD..
    ...AAAAAA...
    ............
    """, 12, 12)
    return [p0, p1]


def make_shots():
    f0 = parse_frame("""
    ...DE...
    ...DE...
    ...DE...
    ...DE...
    ...DE...
    ...DE...
    ...DE...
    ...DE...
    """, 8, 8)
    f1 = parse_frame("""
    ......DE
    .....DE.
    ....DE..
    ...DE...
    ..DE....
    .DE.....
    DE......
    D.......
    """, 8, 8)
    f2 = parse_frame("""
    ........
    ........
    ........
    DDDDDDDD
    EEEEEEEE
    ........
    ........
    ........
    """, 8, 8)
    f3 = parse_frame("""
    ED......
    .ED.....
    ..ED....
    ...ED...
    ....ED..
    .....ED.
    ......ED
    .......D
    """, 8, 8)
    f4 = parse_frame("""
    ........
    ........
    ...EE...
    ..EDDE..
    ..EDDE..
    ...EE...
    ........
    ........
    """, 8, 8)
    f5 = parse_frame("""
    ...FF...
    ...FF...
    ........
    FF....FF
    FF....FF
    ........
    ...FF...
    ...FF...
    """, 8, 8)
    return [f0, f1, f2, f3, f4, f5]


def make_boom():
    b0 = parse_frame("""
    ................
    ................
    ................
    ................
    ......7777......
    .....766667.....
    ....76666667....
    ....76666667....
    ....76666667....
    ....76666667....
    .....766667.....
    ......7777......
    ................
    ................
    ................
    ................
    """, 16, 16)
    b1 = parse_frame("""
    ................
    .......88.......
    ....88.77.88....
    ...8777667778...
    ..876666666678..
    ..766666666667..
    .87666666666678.
    .87666666666678.
    ..766666666667..
    ..876666666678..
    ...8777667778...
    ....88.77.88....
    .......88.......
    ................
    ................
    ................
    """, 16, 16)
    b2 = parse_frame("""
    ....99....99....
    ..9988....8899..
    ..987788887789..
    .98778877887789.
    9878899889988789
    .8789A9889A9878.
    .889A997799A988.
    ..99A987789A99..
    ..99A987789A99..
    .889A997799A988.
    .8789A9889A9878.
    9878899889988789
    .98778877887789.
    ..987788887789..
    ..9988....8899..
    ....99....99....
    """, 16, 16)
    b3 = parse_frame("""
    ..BB........BB..
    .B..A......A..B.
    ....A..CC..A....
    ..B..C....C..B..
    .....C.AA.C.....
    ..A........A....
    .B...A....A...B.
    .....C....C.....
    .....C....C.....
    .B...A....A...B.
    ....A........A..
    .....C.AA.C.....
    ..B..C....C..B..
    ....A..CC..A....
    .B..A......A..B.
    ..BB........BB..
    """, 16, 16)
    return [b0, b1, b2, b3]


def make_creatures():
    w0 = parse_frame("""
    ............
    ....2222....
    ...211112...
    ..21EF1EF2..
    ..11111111..
    ...112211...
    ..11111111..
    ..11.11.11..
    ..1..11..1..
    .1...11...1.
    ............
    ............
    """, 12, 12)
    w1 = parse_frame("""
    ............
    ....2222....
    ...211112...
    ..21EF1EF2..
    ..11111111..
    ...112211...
    ..11111111..
    ...11..11...
    ..11....11..
    ..1......1..
    ............
    ............
    """, 12, 12)
    h0 = parse_frame("""
    ............
    ............
    ...44..44...
    ..4EF44EF4..
    ..33333333..
    .3334444333.
    .3333333333.
    ..333..333..
    .33......33.
    33........33
    ............
    ............
    """, 12, 12)
    h1 = parse_frame("""
    ...44..44...
    ..4EF44EF4..
    ..33333333..
    ..33444433..
    ...333333...
    ...33..33...
    ..33....33..
    ..3......3..
    .3........3.
    ............
    ............
    ............
    """, 12, 12)
    r0 = parse_frame("""
    .....66.....
    ....6556....
    ...65EF56...
    ....5555....
    ...566665...
    ..55555555..
    ....5555....
    ...55..55...
    ..55....55..
    .55......5..
    ............
    ............
    """, 12, 12)
    r1 = parse_frame("""
    .....66.....
    ....6556....
    ...65EF56...
    ....5555....
    ..55666655..
    ....5555....
    ....5555....
    ....5555....
    ...55..55...
    ...5....55..
    ............
    ............
    """, 12, 12)
    d0 = parse_frame("""
    ............
    .8........8.
    .88.8888.88.
    ..87777778..
    ..77EF7EF7..
    .7777777777.
    .77.7777.77.
    77..7..7..77
    7...7..7...7
    ............
    ............
    ............
    """, 12, 12)
    d1 = parse_frame("""
    ............
    ..8......8..
    ..88888888..
    ..87777778..
    ..77EF7EF7..
    .7777777777.
    77..7777..77
    .7..7..7..7.
    ..7.7..7.7..
    ............
    ............
    ............
    """, 12, 12)
    g0 = parse_frame("""
    ............
    ............
    .A........A.
    AA9......9AA
    .A99AAAA99A.
    ..99EF9EF9..
    ...999999...
    ....9999....
    .....99.....
    ............
    ............
    ............
    """, 12, 12)
    g1 = parse_frame("""
    ............
    ............
    ....AAAA....
    ...9EF9EF...
    ..99999999..
    .AA999999AA.
    AA..9999..AA
    A....99....A
    ............
    ............
    ............
    ............
    """, 12, 12)
    s0 = parse_frame("""
    ............
    ............
    ....BBBB....
    ...BEFEFB...
    ..BBBBBBBB..
    .B.BBBBBB.B.
    B..BBBBBB..B
    ..B.B..B.B..
    .B..B..B..B.
    ............
    ............
    ............
    """, 12, 12)
    s1 = parse_frame("""
    ............
    ....BBBB....
    ...BEFEFB...
    ..BBBBBBBB..
    .BBBBBBBBBB.
    B..BBBBBB..B
    .B..BBBB..B.
    ..B......B..
    ............
    ............
    ............
    ............
    """, 12, 12)
    return [w0, w1, h0, h1, r0, r1, d0, d1, g0, g1, s0, s1]


def make_cargo():
    crate = parse_frame("""
    ............
    .DDDDDDDDDD.
    .DCCCCCCCCD.
    .DC..DD..CD.
    .DC.DDDD.CD.
    .DCDDDDDDCD.
    .DCDDDDDDCD.
    .DC.DDDD.CD.
    .DC..DD..CD.
    .DCCCCCCCCD.
    .DDDDDDDDDD.
    ............
    """, 12, 12)
    relic0 = parse_frame("""
    .....EE.....
    ....EAAE....
    ...EA99AE...
    ..EA9DD9AE..
    .EA9DEED9AE.
    EA9DEEEED9AE
    EA9DEEEED9AE
    .EA9DEED9AE.
    ..EA9DD9AE..
    ...EA99AE...
    ....EAAE....
    .....EE.....
    """, 12, 12)
    relic1 = parse_frame("""
    .....AA.....
    ....A88A....
    ...A8BB8A...
    ..A8BEEB8A..
    .A8BE44EB8A.
    A8BE4EE4EB8A
    A8BE4EE4EB8A
    .A8BE44EB8A.
    ..A8BEEB8A..
    ...A8BB8A...
    ....A88A....
    .....AA.....
    """, 12, 12)
    return [crate, relic0, relic1]


def make_beam():
    cols = [8, 9, 0xE, 0xD]
    frames = []
    for f in range(4):
        g = [[0] * 16 for _ in range(16)]
        for y in range(16):
            c_out = cols[(y // 2 + f) % 4]
            c_in = 0xA if ((y + f) % 4 == 0) else cols[(y // 2 + f + 1) % 4]
            for x in range(2, 14):
                if x in (2, 13):
                    g[y][x] = c_out
                elif x in (6, 7, 8, 9):
                    g[y][x] = c_in
                elif (x + y + f) % 2 == 0:
                    g[y][x] = c_out
        frames.append(g)
    return frames


def make_gun():
    g0 = parse_frame("""
    ..CCCCCC....
    .C667766A...
    C6677776AA..
    C6677776AAA.
    C6677776AAA.
    C6677776AA..
    .C667766A...
    ..CCCCCC....
    """, 12, 8)
    g1 = parse_frame("""
    ..BBBBBB....
    .BFFFFFFB...
    BFFFFFFFFB..
    BFFFFFFFFFB.
    BFFFFFFFFFB.
    BFFFFFFFFB..
    .BFFFFFFB...
    ..BBBBBB....
    """, 12, 8)
    return [g0, g1]


def make_pulse():
    p0 = [[0xD] * 32, [0xA] * 32, [0xF] * 32, [0xD] * 32]
    p1 = [[0xB] * 32, [0xF] * 32, [0xA] * 32, [0xB] * 32]
    return [p0, p1]


def make_tunnel():
    g = [[0] * 24 for _ in range(16)]
    for y in range(16):
        g[y][2] = 1
        g[y][3] = 3
        g[y][4] = 8 if (y % 4 < 2) else 2
        g[y][19] = 8 if (y % 4 < 2) else 2
        g[y][20] = 3
        g[y][21] = 1
        if y % 4 == 0:
            for x in range(5, 19):
                g[y][x] = 5
    return [g]


# ---------------------------------------------------------------------------
# Tile Sheets (8x8 Mode 2 tiles)
# ---------------------------------------------------------------------------

def make_space_tiles():
    tiles = []
    tiles.append([[0] * 8 for _ in range(8)])
    t = [[0] * 8 for _ in range(8)]; t[3][4] = 1; tiles.append(t)
    t = [[0] * 8 for _ in range(8)]; t[5][2] = 2; tiles.append(t)
    t = [[0] * 8 for _ in range(8)]; t[2][6] = 3; tiles.append(t)
    t = [[0] * 8 for _ in range(8)]; t[6][5] = 4; tiles.append(t)
    t = [[0] * 8 for _ in range(8)]; t[1][3] = 9; t[5][7] = 0xC; tiles.append(t)
    t = [[0] * 8 for _ in range(8)]; t[4][1] = 0xA; t[2][5] = 0xB; tiles.append(t)
    t = [[0] * 8 for _ in range(8)]; t[2][2] = 5; t[3][3] = 6; t[5][5] = 5; t[4][6] = 7; tiles.append(t)
    return tiles


def make_planet_tiles():
    tiles = []
    tiles.append([[0] * 8 for _ in range(8)])
    tiles.append([[1] * 8 for _ in range(8)])
    t = [[1] * 8 for _ in range(8)]; t[2][5] = 8; tiles.append(t)
    t = [[1] * 8 for _ in range(8)]; t[5][2] = 9; tiles.append(t)
    t = [[1] * 8 for _ in range(8)]
    for y in range(4, 8):
        for x in range(8):
            if y >= 4 + abs(x - 3) // 2:
                t[y][x] = 2
    tiles.append(t)
    tiles.append([[2] * 8 for _ in range(8)])
    t = [[3] * 8, [3] * 8, [4, 3, 4, 4, 3, 4, 4, 3], [4] * 8,
         [4, 4, 5, 4, 4, 4, 5, 4], [4] * 8, [5, 4, 4, 5, 4, 4, 5, 4], [5] * 8]
    tiles.append(t)
    t = [[0] * 8 for _ in range(4)] + [[3] * 8, [3] * 8, [4, 3, 4, 4, 3, 4, 4, 3], [4] * 8]
    tiles.append(t)
    t = [[0] * 8 for _ in range(8)]
    for x in range(8):
        sy = x // 2
        for y in range(sy, 8):
            t[y][x] = 3 if y < sy + 2 else 4
    tiles.append(t)
    t = [[0] * 8 for _ in range(8)]
    for x in range(8):
        sy = (7 - x) // 2
        for y in range(sy, 8):
            t[y][x] = 3 if y < sy + 2 else 4
    tiles.append(t)
    t = [[4] * 8 for _ in range(8)]; t[1][2] = 5; t[3][6] = 5; t[5][1] = 5; t[6][5] = 5; tiles.append(t)
    t = [[5] * 8 for _ in range(8)]; t[2][3] = 4; t[5][6] = 4; tiles.append(t)
    t = [[0] * 8 for _ in range(8)]
    for y in range(8):
        t[y][1] = 0xC; t[y][2] = 6; t[y][3] = 7; t[y][4] = 6; t[y][5] = 0xC
        if y % 4 == 0: t[y][2] = 7; t[y][4] = 7
    tiles.append(t)
    t = [[0] * 8 for _ in range(8)]
    for y in range(4, 8):
        for x in range(1, 7):
            t[y][x] = 7 if y == 4 else 6
    t[3][3] = 0xE; t[3][4] = 0xE
    tiles.append(t)
    t = [[3] * 8 for _ in range(8)]
    for y in range(2, 8): t[y] = [4] * 8
    for y in range(0, 6):
        for x in range(1, 7):
            t[y][x] = 6 if (x == 1 or x == 6) else 7
    tiles.append(t)
    tiles.append([[0xC] * 8 for _ in range(8)])
    return tiles


def make_hud_tiles():
    """Custom 4bpp graphical cockpit HUD tiles (8x8, PAL_HUD) for Plane 1 behind Mode 1 text.
    Tile indices:
      0: panel top fill (beveled metallic dark navy)
      1: panel bottom edge (with glowing cyan cockpit rim on row 7)
      2: panel top edge (for bottom HUD: glowing cyan cockpit rim on row 0)
      3: panel divider (subtle vertical titanium seam)
      4: recessed meter well empty (dark glass + top/bottom bevel)
      5: meter well left endcap
      6: meter well right endcap
      7..10: green beveled bar fill (2, 4, 6, 8 px wide)
      11..14: gold beveled bar fill (2, 4, 6, 8 px wide)
      15..18: red beveled bar fill (2, 4, 6, 8 px wide)
      19..22: cyan beveled bar fill (2, 4, 6, 8 px wide)
      23: icon - lightning bolt (Ark energy)
      24: icon - mini saucer (Fighter / Shuttle)
      25: icon - creature loose (glowing green orb)
      26: icon - creature caught (golden badge)
      27: icon - cargo crate
      28: radar empty cell (dark glass + subtle grid line)
      29: radar left endcap
      30: radar right endcap
      31: radar tower tick
      32: radar Ark blip (golden saucer)
      33: radar creature blip (green dot)
      34: radar shuttle blip (cyan triangle)
      35: radar relic blip (magenta diamond)
      36: radar storm alert blip (red/gold!)
    """
    tiles = []

    # 0: panel top fill
    t = [[1] * 8 for _ in range(8)]
    t[0] = [0xB] * 8
    tiles.append(t)

    # 1: panel bottom edge (top HUD row 1: glowing cyan trim on row 7)
    t = [[1] * 8 for _ in range(8)]
    t[6] = [0xC] * 8
    t[7] = [4] * 8
    tiles.append(t)

    # 2: panel top edge (bottom HUD row 0: glowing cyan trim on row 0)
    t = [[1] * 8 for _ in range(8)]
    t[0] = [4] * 8
    t[1] = [0xC] * 8
    tiles.append(t)

    # 3: vertical metallic divider
    t = [[1] * 8 for _ in range(8)]
    for y in range(8):
        t[y][3] = 0xA
        t[y][4] = 0xB
    tiles.append(t)

    def meter_tile(fill_px, hi, mid, lo):
        g = [[1] * 8 for _ in range(8)]
        g[1] = [0xA] * 8
        for y in range(2, 6):
            g[y] = [0xB] * 8
        g[6] = [0xD] * 8
        for x in range(fill_px):
            g[2][x] = hi
            g[3][x] = mid
            g[4][x] = mid
            g[5][x] = lo
        return g

    # 4: empty meter well
    tiles.append(meter_tile(0, 0, 0, 0))

    # 5: meter left endcap
    t = [[1] * 8 for _ in range(8)]
    for y in range(1, 7): t[y][6] = 0xA; t[y][7] = 0xC
    t[1][7] = 4; t[6][7] = 4
    tiles.append(t)

    # 6: meter right endcap
    t = [[1] * 8 for _ in range(8)]
    for y in range(1, 7): t[y][0] = 0xC; t[y][1] = 0xA
    t[1][0] = 4; t[6][0] = 4
    tiles.append(t)

    # 7..10: green bar (2,4,6,8 px)
    for w in (2, 4, 6, 8):
        tiles.append(meter_tile(w, 2, 5, 0xE))
    # 11..14: gold bar
    for w in (2, 4, 6, 8):
        tiles.append(meter_tile(w, 2, 3, 7))
    # 15..18: red bar
    for w in (2, 4, 6, 8):
        tiles.append(meter_tile(w, 7, 6, 0xF))
    # 19..22: cyan bar
    for w in (2, 4, 6, 8):
        tiles.append(meter_tile(w, 2, 4, 0xC))

    # 23: energy lightning bolt icon
    tiles.append(parse_frame("""
    11113311
    11132111
    11323111
    13233311
    11113211
    11132111
    11331111
    11111111
    """, 8, 8))

    # 24: mini saucer icon
    tiles.append(parse_frame("""
    11111111
    11122111
    11444411
    14243441
    11CCCC11
    111AA111
    11111111
    11111111
    """, 8, 8))

    # 25: creature loose icon (green pod)
    tiles.append(parse_frame("""
    11111111
    11555511
    15E11E51
    15111151
    15111151
    15E11E51
    11555511
    11111111
    """, 8, 8))

    # 26: creature caught icon (golden filled gem)
    tiles.append(parse_frame("""
    11111111
    11333311
    13223331
    13233371
    13333771
    13377771
    11777711
    11111111
    """, 8, 8))

    # 27: cargo crate icon
    tiles.append(parse_frame("""
    11111111
    13333331
    13777731
    13733731
    13733731
    13777731
    13333331
    11111111
    """, 8, 8))

    def radar_tile(overlay=None):
        g = [[1] * 8 for _ in range(8)]
        g[0] = [0xC] * 8
        for y in range(1, 6):
            g[y] = [0xB] * 8
        g[3][0] = 0xA; g[3][4] = 0xA
        g[6] = [0xC] * 8
        g[7] = [4] * 8
        if overlay:
            for (ox, oy, c) in overlay:
                g[oy][ox] = c
        return g

    # 28: radar empty cell
    tiles.append(radar_tile())
    # 29: radar left endcap
    t = [[1] * 8 for _ in range(8)]
    t[0][6] = 4; t[0][7] = 4
    for y in range(1, 6): t[y][6] = 4; t[y][7] = 0xC
    t[6] = [0xC] * 8; t[7] = [4] * 8
    tiles.append(t)
    # 30: radar right endcap
    t = [[1] * 8 for _ in range(8)]
    t[0][0] = 4; t[0][1] = 4
    for y in range(1, 6): t[y][0] = 0xC; t[y][1] = 4
    t[6] = [0xC] * 8; t[7] = [4] * 8
    tiles.append(t)
    # 31: radar tower tick
    tiles.append(radar_tile([(3, 2, 9), (3, 3, 9), (3, 4, 9), (3, 5, 0xA)]))
    # 32: radar Ark blip (gold saucer)
    tiles.append(radar_tile([(3, 1, 2), (4, 1, 2), (2, 2, 3), (3, 2, 3), (4, 2, 3), (5, 2, 3)]))
    # 33: radar creature blip (green dot)
    tiles.append(radar_tile([(3, 3, 2), (4, 3, 5), (3, 4, 5), (4, 4, 0xE)]))
    # 34: radar shuttle blip (cyan triangle)
    tiles.append(radar_tile([(3, 2, 2), (4, 2, 2), (2, 3, 4), (3, 3, 4), (4, 3, 4), (5, 3, 4)]))
    # 35: radar relic blip (magenta diamond)
    tiles.append(radar_tile([(3, 2, 2), (2, 3, 8), (3, 3, 8), (4, 3, 8), (3, 4, 8)]))
    # 36: radar storm alert blip (red!)
    tiles.append(radar_tile([(3, 1, 6), (4, 1, 6), (3, 2, 6), (4, 2, 6), (3, 3, 3), (4, 3, 3), (3, 5, 6), (4, 5, 6)]))

    # --- Custom 4bpp Galactic Star Chart tiles (37..48, transparent over starfield!) ---
    # 37: transparent empty
    tiles.append([[0] * 8 for _ in range(8)])
    # 38: hyperlane horizontal
    t = [[0] * 8 for _ in range(8)]
    t[3] = [0xC] * 8; t[4] = [0xD] * 8
    tiles.append(t)
    # 39: hyperlane vertical
    t = [[0] * 8 for _ in range(8)]
    for y in range(8): t[y][3] = 0xC; t[y][4] = 0xD
    tiles.append(t)
    # 40: hyperlane diag down-right (\)
    t = [[0] * 8 for _ in range(8)]
    for i in range(8):
        t[i][i] = 0xC
        if i + 1 < 8: t[i][i + 1] = 0xD
    tiles.append(t)
    # 41: hyperlane diag down-left (/)
    t = [[0] * 8 for _ in range(8)]
    for i in range(8):
        t[i][7 - i] = 0xC
        if 6 - i >= 0: t[i][6 - i] = 0xD
    tiles.append(t)
    # 42: hyperlane diag cross (X)
    t = [[0] * 8 for _ in range(8)]
    for i in range(8):
        t[i][i] = 0xC; t[i][7 - i] = 0xC
    tiles.append(t)
    # 43: chart node - Starport (cyan orbital station)
    tiles.append(parse_frame("""
    ...44...
    ..4224..
    .4C44C4.
    42422424
    42422424
    .4C44C4.
    ..4CC4..
    ...44...
    """, 8, 8))
    # 44: chart node - Planet (shaded emerald globe)
    tiles.append(parse_frame("""
    ..5555..
    .52555E.
    525555EE
    5555EEEE
    555EEEEE
    55EEEEE1
    .EEEEEE.
    ..EEEE..
    """, 8, 8))
    # 45: chart node - Nebula (glowing magenta cloud)
    tiles.append(parse_frame("""
    ..88....
    .82888..
    8882888.
    .8888288
    8828888.
    .88888..
    ...888..
    ........
    """, 8, 8))
    # 46: chart node - Empty visited sector (steel beacon)
    tiles.append(parse_frame("""
    ........
    ..AAAA..
    .A9999A.
    .A9229A.
    .A9229A.
    .A9999A.
    ..AAAA..
    ........
    """, 8, 8))
    # 47: chart node - Unvisited seen sector (dim sensor ring)
    tiles.append(parse_frame("""
    ........
    ..AAAA..
    .A....A.
    .A.99.A.
    .A.99.A.
    .A....A.
    ..AAAA..
    ........
    """, 8, 8))
    # 48: chart node - The Ark (golden gleaming saucer)
    tiles.append(parse_frame("""
    ..2332..
    .332233.
    34322343
    23333332
    .777777.
    ..7337..
    ........
    ........
    """, 8, 8))

    return tiles


def main():
    IMG.mkdir(exist_ok=True)
    GEN.mkdir(parents=True, exist_ok=True)

    sheets = [
        ("ark",          64, 32, make_ark(),         PALETTES["PAL_ARK"]),
        ("rock_l",       24, 24, make_rock_l(),      PALETTES["PAL_ROCK"]),
        ("rock_m",       16, 16, make_rock_m(),      PALETTES["PAL_ROCK"]),
        ("rock_s",       12, 12, make_rock_s(),      PALETTES["PAL_ROCK"]),
        ("saucer",       16, 12, make_saucer(),      PALETTES["PAL_SAUCER"]),
        ("raider",       16, 16, make_raiders(),     PALETTES["PAL_PIRATE"]),
        ("gunship",      20, 20, make_gunships(),    PALETTES["PAL_PIRATE"]),
        ("missile",      12, 12, make_missiles(),    PALETTES["PAL_PIRATE"]),
        ("pod",          12, 12, make_pods(),        PALETTES["PAL_PIRATE"]),
        ("shots",         8,  8, make_shots(),       PALETTES["PAL_ROCK"]),
        ("boom",         16, 16, make_boom(),        PALETTES["PAL_ROCK"]),
        ("creatures",    12, 12, make_creatures(),   PALETTES["PAL_CREATURES"]),
        ("cargo",        12, 12, make_cargo(),       PALETTES["PAL_CREATURES"]),
        ("beam",         16, 16, make_beam(),        PALETTES["PAL_SAUCER"]),
        ("gun",          12,  8, make_gun(),         PALETTES["PAL_WORLD_0"]),
        ("pulse",        32,  4, make_pulse(),       PALETTES["PAL_WORLD_0"]),
        ("tunnel",       24, 16, make_tunnel(),      PALETTES["PAL_ARK"]),
        ("tiles_space",   8,  8, make_space_tiles(), PALETTES["PAL_SPACE_BG"]),
        ("tiles_planet",  8,  8, make_planet_tiles(),PALETTES["PAL_WORLD_0"]),
        ("tiles_hud",     8,  8, make_hud_tiles(),   PALETTES["PAL_HUD"]),
    ]

    h_lines = [
        "/* Generated by tools/gen_art.py — do not edit by hand. */",
        "#ifndef GEN_ASSETS_H",
        "#define GEN_ASSETS_H",
        "",
        "#include <stdint.h>",
        "",
    ]

    for name, fw, fh, frames, pal in sheets:
        img = hstack_frames(frames)
        write_indexed_png(IMG / f"{name}.png", img, pal)
        u = name.upper()
        fbytes = (fw * fh) // 2
        h_lines.append(f"#define {u}_W {fw}u")
        h_lines.append(f"#define {u}_H {fh}u")
        h_lines.append(f"#define {u}_FRAMES {len(frames)}u")
        h_lines.append(f"#define {u}_FRAME_BYTES {fbytes}u")
        h_lines.append(f"#define {u}_TOTAL_BYTES {fbytes * len(frames)}u")
        h_lines.append("")

    pal_names = list(PALETTES.keys())
    for i, pname in enumerate(pal_names):
        h_lines.append(f"#define {pname} {i}u")
    h_lines.append(f"#define PAL_COUNT {len(pal_names)}u")
    h_lines.append("")

    hud_colors = [
        ("C_TRANS", 0), ("C_PANEL", 1), ("C_WHITE", 2), ("C_GOLD", 3),
        ("C_CYAN", 4), ("C_GREEN", 5), ("C_RED", 6), ("C_ORANGE", 7),
        ("C_MAGENTA", 8), ("C_GREY", 9), ("C_DIM", 10), ("C_FRAME", 11),
        ("C_TEAL", 12), ("C_BLUE", 13), ("C_DARKGREEN", 14), ("C_DARKRED", 15),
    ]
    for cname, cid in hud_colors:
        h_lines.append(f"#define {cname} {cid}u")
    h_lines.append("")

    pal_bin = bytearray()
    for pname in pal_names:
        cols = PALETTES[pname]
        for i, (r, g, b) in enumerate(cols):
            v = rgb555_a(r, g, b, opaque=(i != 0 or pname == "PAL_SPACE_BG"))
            pal_bin.extend(struct.pack("<H", v))
    (IMG / "palettes.bin").write_bytes(pal_bin)

    h_lines.append("#endif /* GEN_ASSETS_H */")
    h_lines.append("")

    (GEN / "assets.h").write_text("\n".join(h_lines))
    print(f"Generated {len(sheets)} PNG sheets, images/palettes.bin ({len(pal_bin)}B), and src/gen/assets.h ({len(pal_names)} palettes)")


if __name__ == "__main__":
    main()
