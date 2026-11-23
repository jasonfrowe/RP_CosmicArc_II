#!/usr/bin/env python3
"""Generate OPL2 sound effects for Cosmic Arc II (RP6502).

Each effect is a short stream of OPL2 register writes and waits on channel 7
(player) or channel 8 (enemy), leaving channels 0-6 for streamed VGM music.

Writes:
  music/sfx.bin        packed SFX blob loaded into XRAM_SFX_DATA
  src/gen/sfx_ids.h    effect IDs and byte offsets into sfx.bin
"""

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VGM_RATE = 44100
OPL_RATE = 49716.0
MOD = {7: 0x11, 8: 0x12}
CAR = {7: 0x14, 8: 0x15}


def fnum_block(hz):
    for block in range(8):
        f = round(hz * (2 ** (20 - block)) / OPL_RATE)
        if 0 < f <= 1023:
            return f, block
    return 1023, 7


class Sfx:
    def __init__(self, ch):
        self.ch = ch
        self.ev = []

    def w(self, reg, val):
        self.ev.append((reg, val & 0xFF, 0))

    def patch(self, mod_mult=1, mod_tl=0, mod_ar=15, mod_dr=8, mod_sl=0, mod_rr=8, mod_wave=0,
              car_mult=1, car_tl=0, car_ar=15, car_dr=8, car_sl=0, car_rr=8, car_wave=0,
              feedback=0, connection=0, sustain=1):
        m, c = MOD[self.ch], CAR[self.ch]
        self.w(0x20 + m, (sustain << 5) | mod_mult)
        self.w(0x20 + c, (sustain << 5) | car_mult)
        self.w(0x40 + m, mod_tl)
        self.w(0x40 + c, car_tl)
        self.w(0x60 + m, (mod_ar << 4) | mod_dr)
        self.w(0x60 + c, (car_ar << 4) | car_dr)
        self.w(0x80 + m, (mod_sl << 4) | mod_rr)
        self.w(0x80 + c, (car_sl << 4) | car_rr)
        self.w(0xE0 + m, mod_wave)
        self.w(0xE0 + c, car_wave)
        self.w(0xC0 + self.ch, (feedback << 1) | connection)
        return self

    def on(self, hz):
        f, b = fnum_block(hz)
        self.w(0xA0 + self.ch, f & 0xFF)
        self.w(0xB0 + self.ch, 0x20 | (b << 2) | (f >> 8))
        return self

    def off(self):
        self.w(0xB0 + self.ch, 0)
        return self

    def wait(self, ms):
        n = round(ms * VGM_RATE / 1000)
        if n > 0 and self.ev:
            r, v, w = self.ev[-1]
            self.ev[-1] = (r, v, w + n)
        return self

    def sweep(self, a, b, ms, steps=8):
        for i in range(steps + 1):
            self.on(a + (b - a) * i / steps)
            if i < steps:
                self.wait(ms / steps)
        return self

    def boom(self, a, b, ms, steps, tl0, tl1):
        for i in range(steps + 1):
            t = i / steps
            self.w(0x40 + MOD[self.ch], round(tl0 + (tl1 - tl0) * t))
            self.on(a + (b - a) * t)
            if i < steps:
                self.wait(ms / steps)
        return self

    def stream(self):
        out = bytearray()
        for r, v, w in self.ev:
            out += bytes([0x5A, r, v])
            while w > 0:
                n = min(w, 0xFFFF)
                out += bytes([0x61]) + struct.pack("<H", n)
                w -= n
        out.append(0x66)
        return bytes(out)


def build_effects():
    fx = {}

    # Player channel (ch 7)
    # 1. Ark point-defense laser
    s = Sfx(7).patch(mod_mult=2, mod_tl=18, mod_dr=10, car_tl=6, car_dr=9, feedback=3)
    s.sweep(1400, 320, 70, 7).off()
    fx["SFX_LASER"] = s

    # 2. Fighter twin blasters (crisper, higher)
    s = Sfx(7).patch(mod_mult=3, mod_tl=22, mod_dr=11, car_tl=8, car_dr=10, feedback=2)
    s.sweep(1900, 620, 50, 5).off()
    fx["SFX_BLASTER"] = s

    # 3. Launch / undock whoosh
    s = Sfx(7).patch(mod_mult=1, mod_tl=24, mod_dr=6, car_tl=8, car_dr=6, feedback=2)
    s.sweep(220, 880, 140, 8).off()
    fx["SFX_LAUNCH"] = s

    # 4. Dock chime
    s = Sfx(7).patch(mod_mult=2, mod_tl=28, mod_dr=7, car_tl=8, car_dr=7, feedback=1)
    s.on(523).wait(60).on(659).wait(60).on(784).wait(110).off()
    fx["SFX_DOCK"] = s

    # 5. Tractor beam pulse
    s = Sfx(7).patch(mod_mult=3, mod_tl=20, mod_dr=4, car_tl=14, car_dr=5, feedback=1)
    s.sweep(340, 520, 80, 4).sweep(520, 340, 80, 4).off()
    fx["SFX_BEAM"] = s

    # 6. Creature / pod / relic pickup
    s = Sfx(7).patch(mod_mult=2, mod_tl=26, mod_dr=8, car_tl=6, car_dr=7, feedback=1)
    s.on(587).wait(45).on(740).wait(45).on(880).wait(45).on(1175).wait(110).off()
    fx["SFX_PICKUP"] = s

    # 7. Menu blip
    s = Sfx(7).patch(mod_mult=1, mod_tl=32, mod_dr=12, car_tl=12, car_dr=11)
    s.on(880).wait(25).off()
    fx["SFX_BLIP"] = s

    # 8. Trade / buy / upgrade chime
    s = Sfx(7).patch(mod_mult=2, mod_tl=28, mod_dr=8, car_tl=8, car_dr=7)
    s.on(659).wait(45).on(988).wait(95).off()
    fx["SFX_TRADE"] = s

    # 9. Error / denied buzz
    s = Sfx(7).patch(mod_mult=3, mod_tl=12, mod_dr=9, car_tl=10, car_dr=9, feedback=4)
    s.on(150).wait(65).on(120).wait(90).off()
    fx["SFX_DENY"] = s

    # 10. Achievement / wave clear fanfare
    s = Sfx(7).patch(mod_mult=2, mod_tl=24, mod_dr=6, car_tl=6, car_dr=6, feedback=1)
    s.on(523).wait(70).on(659).wait(70).on(784).wait(70).on(1046).wait(180).off()
    fx["SFX_FANFARE"] = s

    # Enemy / world channel (ch 8)
    # 11. Rock crack / small hit
    s = Sfx(8).patch(mod_mult=3, mod_tl=8, mod_dr=9, car_tl=8, car_dr=9, feedback=5)
    s.boom(380, 95, 90, 6, 8, 28).off()
    fx["SFX_ROCK_HIT"] = s

    # 12. Big explosion (pirate / Ark / shuttle)
    s = Sfx(8).patch(mod_mult=2, mod_tl=4, mod_dr=6, car_tl=4, car_dr=6, feedback=5)
    s.boom(260, 48, 240, 12, 4, 34).off()
    fx["SFX_BOOM"] = s

    # 13. Ark hit thud
    s = Sfx(8).patch(mod_mult=1, mod_tl=6, mod_dr=7, car_tl=4, car_dr=7, feedback=5)
    s.boom(180, 55, 160, 8, 6, 30).off()
    fx["SFX_ARK_HIT"] = s

    # 14. Pirate shot / missile launch
    s = Sfx(8).patch(mod_mult=2, mod_tl=16, mod_dr=10, car_tl=12, car_dr=10, feedback=2)
    s.sweep(900, 380, 65, 5).off()
    fx["SFX_PSHOT"] = s

    # 15. Defense turret pulse
    s = Sfx(8).patch(mod_mult=3, mod_tl=10, mod_dr=8, car_tl=8, car_dr=8, feedback=4)
    s.sweep(1200, 200, 120, 8).off()
    fx["SFX_PULSE"] = s

    # 16. Storm klaxon alarm
    s = Sfx(8).patch(mod_mult=2, mod_tl=14, mod_dr=5, car_tl=6, car_dr=5, feedback=3)
    s.on(740).wait(110).on(554).wait(110).on(740).wait(110).on(554).wait(120).off()
    fx["SFX_KLAXON"] = s

    return fx


def main():
    fx = build_effects()
    blob = bytearray()
    offsets = []
    for name, s in fx.items():
        offsets.append((name, len(blob)))
        blob += s.stream()

    (ROOT / "music").mkdir(exist_ok=True)
    (ROOT / "src" / "gen").mkdir(parents=True, exist_ok=True)
    (ROOT / "music" / "sfx.bin").write_bytes(blob)

    lines = [
        "/* Generated by tools/gen_sfx.py — do not edit by hand. */",
        "#ifndef SFX_IDS_H",
        "#define SFX_IDS_H",
        "",
        f"#define SFX_BLOB_SIZE {len(blob)}u",
        "",
    ]
    for i, (name, _) in enumerate(offsets):
        lines.append(f"#define {name} {i}u")
    lines += [
        f"#define SFX_COUNT {len(offsets)}u",
        "",
        "#ifdef SFX_OFFSETS",
        "static const uint16_t sfx_offsets[SFX_COUNT] = {",
    ]
    for name, off in offsets:
        lines.append(f"    {off}u, /* {name} */")
    lines += [
        "};",
        "#endif",
        "",
        "#endif /* SFX_IDS_H */",
        "",
    ]
    (ROOT / "src" / "gen" / "sfx_ids.h").write_text("\n".join(lines))
    print(f"Wrote music/sfx.bin ({len(blob)} bytes, {len(offsets)} effects)")


if __name__ == "__main__":
    main()
