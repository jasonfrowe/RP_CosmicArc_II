#!/usr/bin/env python3
"""Generate 4bpp indexed PNG sprites, tiles, and src/gen/assets.h for Cosmic Arc II (RP6502).

Standard library only (no Pillow needed): writes clean non-interlaced 4-bit
indexed PNGs compatible with tools/png2bin.py.
"""

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
    """Write an 8-bit indexed PNG (color type 3) from a 2D list of 0..15 indices."""
    h = len(rows)
    w = len(rows[0])
    # Pad palette to 16 entries
    pal = list(palette_rgb) + [(0, 0, 0)] * (16 - len(palette_rgb))
    plte = b"".join(bytes(c) for c in pal[:16])

    raw = bytearray()
    for r in rows:
        raw.append(0)  # filter type 0
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
# Palettes (16 entries each; index 0 is always transparent)
# ---------------------------------------------------------------------------

PALETTES = {
    "PAL_HUD": [
        (0, 0, 0),       # 0 transparent
        (14, 18, 38),    # 1 C_PANEL (deep space navy)
        (248, 248, 255), # 2 C_WHITE
        (255, 215, 64),  # 3 C_GOLD
        (72, 230, 255),  # 4 C_CYAN
        (80, 240, 110),  # 5 C_GREEN
        (255, 72, 72),   # 6 C_RED
        (255, 152, 48),  # 7 C_ORANGE
        (216, 112, 255), # 8 C_MAGENTA
        (184, 192, 210), # 9 C_GREY
        (72, 82, 112),   # 10 C_DIM
        (36, 52, 96),    # 11 C_FRAME
        (48, 176, 168),  # 12 C_TEAL
        (96, 144, 255),  # 13 C_BLUE
        (255, 128, 192), # 14 C_PINK
        (255, 248, 160), # 15 C_YELLOW
    ],
    "PAL_ARK": [
        (0, 0, 0),       # 0 transparent
        (48, 58, 86),    # 1 dark hull
        (92, 108, 148),  # 2 mid hull
        (148, 168, 212), # 3 light hull
        (220, 232, 255), # 4 bright specular
        (24, 32, 54),    # 5 bay shadow
        (255, 210, 64),  # 6 dome gold
        (255, 244, 168), # 7 dome highlight
        (48, 224, 255),  # 8 cyan light
        (255, 72, 88),   # 9 red light
        (80, 248, 128),  # A green light
        (255, 160, 40),  # B amber trim
        (120, 48, 64),   # C dark red
        (32, 112, 148),  # D dark cyan
        (180, 120, 32),  # E dark gold
        (255, 255, 255), # F pure white
    ],
    "PAL_ROCK": [
        (0, 0, 0),       # 0 transparent
        (68, 46, 36),    # 1 dark shadow
        (116, 82, 62),   # 2 mid rock
        (164, 122, 92),  # 3 light rock
        (216, 178, 140), # 4 highlight
        (44, 30, 24),    # 5 crater pit
        (255, 255, 255), # 6 boom core white
        (255, 240, 112), # 7 boom yellow
        (255, 148, 36),  # 8 boom orange
        (220, 56, 32),   # 9 boom red
        (128, 40, 32),   # A boom dark red
        (88, 80, 96),    # B smoke grey
        (152, 148, 164), # C light smoke
        (112, 232, 255), # D laser cyan
        (255, 255, 255), # E laser core
        (255, 224, 80),  # F aim gold
    ],
    "PAL_SAUCER": [
        (0, 0, 0),       # 0 transparent
        (40, 96, 136),   # 1 dark hull
        (80, 168, 216),  # 2 mid hull
        (152, 224, 255), # 3 light hull
        (240, 252, 255), # 4 dome white
        (255, 216, 64),  # 5 rim light 1 (gold)
        (255, 72, 96),   # 6 rim light 2 (red)
        (80, 248, 128),  # 7 rim light 3 (green)
        (64, 224, 255),  # 8 beam cyan
        (168, 112, 255), # 9 beam violet
        (255, 255, 255), # A beam core
        (255, 160, 48),  # B blaster orange
        (24, 52, 80),    # C underside
        (192, 64, 224),  # D beam magenta
        (64, 140, 255),  # E beam blue
        (255, 244, 160), # F bright gold
    ],
    "PAL_PIRATE": [
        (0, 0, 0),       # 0 transparent
        (96, 20, 32),    # 1 dark crimson
        (184, 36, 52),   # 2 mid crimson
        (248, 88, 96),   # 3 bright red
        (255, 184, 128), # 4 highlight
        (52, 56, 72),    # 5 dark steel
        (104, 112, 136), # 6 mid steel
        (168, 176, 200), # 7 light steel
        (255, 224, 64),  # 8 engine/cockpit gold
        (255, 128, 32),  # 9 flame orange
        (40, 192, 96),   # A pod green
        (128, 255, 168), # B pod bright green
        (24, 96, 56),    # C pod dark green
        (255, 255, 255), # D white glint
        (216, 64, 255),  # E plasma violet
        (72, 224, 255),  # F visor cyan
    ],
    "PAL_CREATURES": [
        (0, 0, 0),       # 0 transparent
        (40, 180, 80),   # 1 walker green
        (128, 248, 144), # 2 walker light
        (232, 160, 40),  # 3 hopper amber
        (255, 224, 112), # 4 hopper bright
        (232, 72, 112),  # 5 runner coral
        (255, 152, 176), # 6 runner light
        (144, 80, 232),  # 7 dodger violet
        (208, 152, 255), # 8 dodger light
        (48, 192, 232),  # 9 glider cyan
        (152, 240, 255), # A glider light
        (240, 96, 208),  # B skitter magenta
        (140, 92, 48),   # C crate brown
        (216, 164, 72),  # D crate gold
        (255, 255, 255), # E white eye/glint
        (24, 24, 40),    # F pupil / dark outline
    ],
    "PAL_SPACE_BG": [
        (0, 0, 0),       # 0 black space
        (255, 255, 255), # 1 bright star
        (128, 184, 255), # 2 blue star
        (255, 224, 128), # 3 gold star
        (96, 120, 160),  # 4 dim star
        (44, 32, 72),    # 5 nebula deep purple
        (28, 48, 76),    # 6 nebula deep blue
        (68, 40, 88),    # 7 nebula mist
        (20, 20, 36),    # 8 subtle dark
        (180, 210, 255), # 9 twinkle A
        (100, 130, 190), # A twinkle B
        (220, 190, 110), # B twinkle C
        (64, 88, 120),   # C twinkle D
        (255, 160, 160), # D red dwarf star
        (160, 255, 220), # E cyan star
        (255, 255, 255), # F white
    ],
}

# 6 planetary worlds (sky, hills, grass/top, soil, deep rock, tower metal, gun)
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
        (0, 0, 0),       # 0 transparent (for plane 1 over plane 0 sky)
        sky,             # 1 sky
        hill,            # 2 distant hills
        top,             # 3 surface crust
        soil,            # 4 soil
        rock,            # 5 deep rock
        tow_d,           # 6 tower dark
        tow_l,           # 7 tower light
        (255, 255, 255), # 8 star bright
        (168, 208, 255), # 9 star dim
        (255, 80, 64),   # A gun red / pulse core
        (255, 224, 80),  # B gun warn flash
        (40, 44, 56),    # C tower shadow
        (255, 160, 64),  # D pulse glow
        (80, 240, 180),  # E beacon
        (255, 255, 255), # F white
    ]


# ---------------------------------------------------------------------------
# Sprite builders
# ---------------------------------------------------------------------------

def make_ark():
    """64x32 Ark mothership, 2 frames (animated rim lights)."""
    frames = []
    for phase in range(2):
        grid = [[0] * 64 for _ in range(32)]

        def hspan(y, x0, x1, c):
            for x in range(max(0, x0), min(64, x1)):
                grid[y][x] = c

        # Golden command dome (rows 3..8)
        dome = [(3, 28, 36, 7), (4, 25, 39, 7), (5, 23, 41, 6),
                (6, 21, 43, 6), (7, 20, 44, 0xE), (8, 18, 46, 2)]
        for y, x0, x1, c in dome:
            hspan(y, x0, x1, c)
        # Dome viewport windows
        for x in (24, 28, 32, 36, 40):
            grid[6][x] = 8
            grid[6][x + 1] = 0xF

        # Upper saucer hull (rows 9..13)
        hull_up = [(9, 14, 50, 3), (10, 10, 54, 4), (11, 6, 58, 3),
                   (12, 3, 61, 2), (13, 1, 63, 3)]
        for y, x0, x1, c in hull_up:
            hspan(y, x0, x1, c)

        # Mid rim with animated running lights (rows 14..16)
        hspan(14, 0, 64, 4)
        hspan(15, 0, 64, 1)
        hspan(16, 1, 63, 2)
        light_cols = [8, 9, 0xA, 6]
        for i in range(15):
            lx = 3 + i * 4
            c = light_cols[(i + phase * 2) % 4]
            grid[15][lx] = c
            grid[15][lx + 1] = c

        # Lower saucer hull (rows 17..22)
        hull_dn = [(17, 3, 61, 3), (18, 6, 58, 2), (19, 10, 54, 2),
                   (20, 14, 50, 1), (21, 18, 46, 1), (22, 20, 44, 5)]
        for y, x0, x1, c in hull_dn:
            hspan(y, x0, x1, c)
        # Hull panel lines
        for x in (16, 24, 40, 48):
            grid[11][x] = 1
            grid[12][x] = 1
            grid[18][x] = 1
            grid[19][x] = 1

        # Fighter/shuttle hangar bay underneath (rows 23..27)
        hspan(23, 20, 26, 2)
        hspan(23, 26, 38, 5)
        hspan(23, 38, 44, 2)
        hspan(24, 21, 25, 3)
        hspan(24, 25, 39, 5)
        hspan(24, 39, 43, 3)
        hspan(25, 22, 25, 1)
        hspan(25, 39, 42, 1)
        # Bay guide beacons
        grid[24][25] = 8 if phase == 0 else 6
        grid[24][38] = 8 if phase == 0 else 6

        frames.append(grid)
    return frames


def make_rock(size, seed_shift):
    """Shaded asteroid of size x size (16, 12, or 8), 2 rotation frames."""
    frames = []
    r = size / 2.0
    cx = cy = (size - 1) / 2.0
    for f in range(2):
        grid = [[0] * size for _ in range(size)]
        for y in range(size):
            for x in range(size):
                dx = x - cx
                dy = y - cy
                # Slight bumpy radius
                d2 = dx * dx + dy * dy
                bump = 0.85 if ((x + y + f * 3 + seed_shift) % 5 == 0) else 1.0
                if d2 <= (r - 0.4) * (r - 0.4) * bump:
                    # Light from top-left
                    shade = dx * 0.6 + dy * 0.8
                    if shade < -r * 0.35:
                        c = 4
                    elif shade < r * 0.15:
                        c = 3
                    elif shade < r * 0.55:
                        c = 2
                    else:
                        c = 1
                    # Craters
                    if size >= 12:
                        if ((x == int(cx - 2 + f * 2) and y == int(cy - 1)) or
                                (x == int(cx + 2 - f) and y == int(cy + 2))):
                            c = 5
                        elif ((x == int(cx - 1 + f * 2) and y == int(cy - 1)) or
                              (x == int(cx + 1 - f) and y == int(cy + 2))):
                            c = 1
                    grid[y][x] = c
        frames.append(grid)
    return frames


def make_saucer():
    """16x12 fighter / shuttle saucer, 4 frames of spinning rim lights."""
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


def make_pirates():
    """16x16 sheet: 0=Raider, 1=Gunship, 2=Missile, 3=Salvage Pod."""
    raider = parse_frame("""
    .......88.......
    ......2332......
    .....234432.....
    ....123FF321....
    ...1223FF3221...
    ..122223322221..
    .12112222221121.
    121..122221..121
    11...156651...11
    .....567765.....
    ....56.99.65....
    ....5..88..5....
    .......99.......
    ................
    ................
    ................
    """, 16, 16)

    gunship = parse_frame("""
    ....56777765....
    ...5623333265...
    ..562344443265..
    .56233FFFF33265.
    562233FFFF332265
    5622223333222265
    5611222222221165
    5615677777765165
    5615622222265165
    5611122222211165
    .56111566511165.
    ..56..5665..65..
    ..89..5885..98..
    ..9...9889...9..
    ................
    ................
    """, 16, 16)

    missile = parse_frame("""
    ................
    ................
    .......33.......
    ......3443......
    ......2332......
    .....567765.....
    .....567765.....
    .....562265.....
    ....56.22.65....
    ....5..88..5....
    .......89.......
    .......99.......
    ................
    ................
    ................
    ................
    """, 16, 16)

    pod = parse_frame("""
    ................
    ................
    .....CCCCCC.....
    ....CAAAAAAB....
    ...CAABBBBBA....
    ...CABDDDDBAC...
    ...CABD..DBAC...
    ...CABDDDDBAC...
    ...CAABBBBBA....
    ....CAAAAAAB....
    .....CCCCCC.....
    .......AA.......
    ................
    ................
    ................
    ................
    """, 16, 16)

    return [raider, gunship, missile, pod]


def make_shots():
    """8x8 sheet: 0=bolt V, 1=bolt /, 2=bolt H, 3=bolt \\, 4=blaster/pirate dot, 5=aim."""
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
    """16x16 4-frame explosion."""
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
    """12x12 creatures: 6 species x 2 animation frames = 12 frames."""
    # 0-1: Walker (green quadruped alien)
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

    # 2-3: Hopper (amber springy frog-like alien)
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

    # 4-5: Runner (coral sleek biped)
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

    # 6-7: Dodger (violet crab-like scuttler)
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

    # 8-9: Glider (cyan winged manta)
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

    # 10-11: Skitterer (magenta multi-eyed spider)
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
    """12x12: 0=ore crate, 1=relic frame 0, 2=relic frame 1."""
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
    """16x16 4-frame shimmering tractor beam."""
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
    """12x8 tower defense gun: 0=normal, 1=flashing warning."""
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
    """32x4 horizontal defense pulse, 2 frames."""
    p0 = [[0xD] * 32, [0xA] * 32, [0xF] * 32, [0xD] * 32]
    p1 = [[0xB] * 32, [0xF] * 32, [0xA] * 32, [0xB] * 32]
    return [p0, p1]


def make_tunnel():
    """24x16 Ark docking tunnel."""
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
# Tile sheets (8x8 Mode 2 tiles)
# ---------------------------------------------------------------------------

def make_space_tiles():
    """8 tiles of 8x8 for deep-space starfield."""
    tiles = []
    # 0: empty space
    tiles.append([[0] * 8 for _ in range(8)])
    # 1: bright star
    t = [[0] * 8 for _ in range(8)]
    t[3][4] = 1
    tiles.append(t)
    # 2: blue star
    t = [[0] * 8 for _ in range(8)]
    t[5][2] = 2
    tiles.append(t)
    # 3: gold star
    t = [[0] * 8 for _ in range(8)]
    t[2][6] = 3
    tiles.append(t)
    # 4: dim star
    t = [[0] * 8 for _ in range(8)]
    t[6][5] = 4
    tiles.append(t)
    # 5: twinkle star A
    t = [[0] * 8 for _ in range(8)]
    t[1][3] = 9
    t[5][7] = 0xC
    tiles.append(t)
    # 6: twinkle star B
    t = [[0] * 8 for _ in range(8)]
    t[4][1] = 0xA
    t[2][5] = 0xB
    tiles.append(t)
    # 7: subtle nebula dust
    t = [[0] * 8 for _ in range(8)]
    t[2][2] = 5
    t[3][3] = 6
    t[5][5] = 5
    t[4][6] = 7
    tiles.append(t)
    return tiles


def make_planet_tiles():
    """16 tiles of 8x8 for planet sky & rolling terrain."""
    tiles = []
    # 0: transparent
    tiles.append([[0] * 8 for _ in range(8)])
    # 1: solid sky (for plane 0)
    tiles.append([[1] * 8 for _ in range(8)])
    # 2: sky with bright star
    t = [[1] * 8 for _ in range(8)]
    t[2][5] = 8
    tiles.append(t)
    # 3: sky with dim star
    t = [[1] * 8 for _ in range(8)]
    t[5][2] = 9
    tiles.append(t)
    # 4: distant hill top
    t = [[1] * 8 for _ in range(8)]
    for y in range(4, 8):
        for x in range(8):
            if y >= 4 + abs(x - 3) // 2:
                t[y][x] = 2
    tiles.append(t)
    # 5: distant hill solid
    tiles.append([[2] * 8 for _ in range(8)])
    # 6: flat ground top (surface at row 0 of tile)
    t = [[3] * 8, [3] * 8, [4, 3, 4, 4, 3, 4, 4, 3], [4] * 8,
         [4, 4, 5, 4, 4, 4, 5, 4], [4] * 8, [5, 4, 4, 5, 4, 4, 5, 4], [5] * 8]
    tiles.append(t)
    # 7: flat ground mid (surface 4px down)
    t = [[0] * 8 for _ in range(4)] + [[3] * 8, [3] * 8, [4, 3, 4, 4, 3, 4, 4, 3], [4] * 8]
    tiles.append(t)
    # 8: slope down-right (0 -> 4px)
    t = [[0] * 8 for _ in range(8)]
    for x in range(8):
        sy = x // 2
        for y in range(sy, 8):
            t[y][x] = 3 if y < sy + 2 else 4
    tiles.append(t)
    # 9: slope up-right (4px -> 0)
    t = [[0] * 8 for _ in range(8)]
    for x in range(8):
        sy = (7 - x) // 2
        for y in range(sy, 8):
            t[y][x] = 3 if y < sy + 2 else 4
    tiles.append(t)
    # 10: soil fill
    t = [[4] * 8 for _ in range(8)]
    t[1][2] = 5
    t[3][6] = 5
    t[5][1] = 5
    t[6][5] = 5
    tiles.append(t)
    # 11: deep rock
    t = [[5] * 8 for _ in range(8)]
    t[2][3] = 4
    t[5][6] = 4
    tiles.append(t)
    # 12: tower shaft (vertical rail)
    t = [[0] * 8 for _ in range(8)]
    for y in range(8):
        t[y][1] = 0xC
        t[y][2] = 6
        t[y][3] = 7
        t[y][4] = 6
        t[y][5] = 0xC
        if y % 4 == 0:
            t[y][2] = 7
            t[y][4] = 7
    tiles.append(t)
    # 13: tower cap
    t = [[0] * 8 for _ in range(8)]
    for y in range(4, 8):
        for x in range(1, 7):
            t[y][x] = 7 if y == 4 else 6
    t[3][3] = 0xE
    t[3][4] = 0xE
    tiles.append(t)
    # 14: tower base on ground
    t = [[3] * 8 for _ in range(8)]
    for y in range(2, 8):
        t[y] = [4] * 8
    for y in range(0, 6):
        for x in range(1, 7):
            t[y][x] = 6 if (x == 1 or x == 6) else 7
    tiles.append(t)
    # 15: solid dark underground
    tiles.append([[0xC] * 8 for _ in range(8)])
    return tiles


def main():
    IMG.mkdir(exist_ok=True)
    GEN.mkdir(parents=True, exist_ok=True)

    sheets = [
        ("ark",       64, 32, make_ark(),         PALETTES["PAL_ARK"]),
        ("rock_l",    16, 16, make_rock(16, 0),   PALETTES["PAL_ROCK"]),
        ("rock_m",    12, 12, make_rock(12, 2),   PALETTES["PAL_ROCK"]),
        ("rock_s",     8,  8, make_rock(8, 4),    PALETTES["PAL_ROCK"]),
        ("saucer",    16, 12, make_saucer(),      PALETTES["PAL_SAUCER"]),
        ("pirates",   16, 16, make_pirates(),     PALETTES["PAL_PIRATE"]),
        ("shots",      8,  8, make_shots(),       PALETTES["PAL_ROCK"]),
        ("boom",      16, 16, make_boom(),        PALETTES["PAL_ROCK"]),
        ("creatures", 12, 12, make_creatures(),   PALETTES["PAL_CREATURES"]),
        ("cargo",     12, 12, make_cargo(),       PALETTES["PAL_CREATURES"]),
        ("beam",      16, 16, make_beam(),        PALETTES["PAL_SAUCER"]),
        ("gun",       12,  8, make_gun(),         PALETTES["PAL_WORLD_0"]),
        ("pulse",     32,  4, make_pulse(),       PALETTES["PAL_WORLD_0"]),
        ("tunnel",    24, 16, make_tunnel(),      PALETTES["PAL_ARK"]),
        ("tiles_space",  8, 8, make_space_tiles(),  PALETTES["PAL_SPACE_BG"]),
        ("tiles_planet", 8, 8, make_planet_tiles(), PALETTES["PAL_WORLD_0"]),
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

    # Palette IDs
    pal_names = list(PALETTES.keys())
    for i, pname in enumerate(pal_names):
        h_lines.append(f"#define {pname} {i}u")
    h_lines.append(f"#define PAL_COUNT {len(pal_names)}u")
    h_lines.append("")

    # HUD color indices
    hud_colors = [
        ("C_TRANS", 0), ("C_PANEL", 1), ("C_WHITE", 2), ("C_GOLD", 3),
        ("C_CYAN", 4), ("C_GREEN", 5), ("C_RED", 6), ("C_ORANGE", 7),
        ("C_MAGENTA", 8), ("C_GREY", 9), ("C_DIM", 10), ("C_FRAME", 11),
        ("C_TEAL", 12), ("C_BLUE", 13), ("C_PINK", 14), ("C_YELLOW", 15),
    ]
    for cname, cid in hud_colors:
        h_lines.append(f"#define {cname} {cid}u")
    h_lines.append("")

    # Palette RGB555A data table (included once in video.c)
    h_lines.append("#ifdef DEFINE_PALETTE_DATA")
    h_lines.append(f"static const uint16_t game_palettes[PAL_COUNT][16] = {{")
    for pname in pal_names:
        cols = PALETTES[pname]
        vals = [rgb555_a(r, g, b, opaque=(i != 0 or pname == "PAL_SPACE_BG"))
                for i, (r, g, b) in enumerate(cols)]
        # Make index 0 opaque black for PAL_SPACE_BG, transparent for everything else
        hexes = ", ".join(f"0x{v:04X}u" for v in vals)
        h_lines.append(f"    /* {pname} */ {{ {hexes} }},")
    h_lines.append("};")
    h_lines.append("#endif /* DEFINE_PALETTE_DATA */")
    h_lines.append("")
    h_lines.append("#endif /* GEN_ASSETS_H */")
    h_lines.append("")

    (GEN / "assets.h").write_text("\n".join(h_lines))
    print(f"Generated {len(sheets)} PNG sheets and src/gen/assets.h ({len(pal_names)} palettes)")


if __name__ == "__main__":
    main()
