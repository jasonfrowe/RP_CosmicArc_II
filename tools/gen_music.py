#!/usr/bin/env python3
"""Generate looping OPL2 VGM music tracks for Cosmic Arc II (RP6502).

Uses OPL2 channels 0-5 (leaving channels 7-8 free for SFX):
  ch 0: Lead synth
  ch 1: Harmony / counter-melody
  ch 2: Bass
  ch 3: Pad / chord root
  ch 4: Arp / sparkle
  ch 5: Percussion (kick/snare/hat FM synth)

Writes:
  music/Title.vgm
  music/Map.vgm
  music/Space.vgm
  music/Pirates.vgm
  music/Planet.vgm
"""

import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VGM_RATE = 44100
OPL_RATE = 49716.0

MOD_OFF = [0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12]
CAR_OFF = [0x03, 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x13, 0x14, 0x15]

NOTE_MAP = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4,
            "F": 5, "F#": 6, "Gb": 6, "G": 7, "G#": 8, "Ab": 8,
            "A": 9, "A#": 10, "Bb": 10, "B": 11}


def note_hz(name):
    if not name or name in ("-", "."):
        return None
    if len(name) >= 3 and name[1] in ("#", "b"):
        pc = NOTE_MAP[name[:2]]
        octv = int(name[2:])
    else:
        pc = NOTE_MAP[name[0]]
        octv = int(name[1:])
    midi = (octv + 1) * 12 + pc
    return 440.0 * (2.0 ** ((midi - 69) / 12.0))


def fnum_block(hz):
    for block in range(8):
        f = round(hz * (2 ** (20 - block)) / OPL_RATE)
        if 0 < f <= 1023:
            return f, block
    return 1023, 7


class VgmBuilder:
    def __init__(self):
        self.data = bytearray()
        self.total_samples = 0
        self.loop_pos = None
        self.loop_samples = 0
        self.w(0x01, 0x20)  # enable waveform select

    def w(self, reg, val):
        self.data += bytes([0x5A, reg & 0xFF, val & 0xFF])

    def mark_loop(self):
        self.loop_pos = len(self.data)
        self.loop_samples = self.total_samples

    def wait_samples(self, n):
        self.total_samples += n
        while n > 0:
            if n == 735:
                self.data.append(0x62)
                n = 0
            elif n == 882:
                self.data.append(0x63)
                n = 0
            else:
                step = min(n, 0xFFFF)
                self.data += bytes([0x61]) + struct.pack("<H", step)
                n -= step

    def patch(self, ch, mod_mult=1, mod_tl=20, mod_ar=15, mod_dr=6, mod_sl=4, mod_rr=6, mod_wave=0,
              car_mult=1, car_tl=12, car_ar=15, car_dr=5, car_sl=3, car_rr=6, car_wave=0,
              feedback=2, conn=0, sustain=1):
        m, c = MOD_OFF[ch], CAR_OFF[ch]
        self.w(0x20 + m, (sustain << 5) | (mod_mult & 0x0F))
        self.w(0x20 + c, (sustain << 5) | (car_mult & 0x0F))
        self.w(0x40 + m, mod_tl & 0x3F)
        self.w(0x40 + c, car_tl & 0x3F)
        self.w(0x60 + m, ((mod_ar & 0xF) << 4) | (mod_dr & 0xF))
        self.w(0x60 + c, ((car_ar & 0xF) << 4) | (car_dr & 0xF))
        self.w(0x80 + m, ((mod_sl & 0xF) << 4) | (mod_rr & 0xF))
        self.w(0x80 + c, ((car_sl & 0xF) << 4) | (car_rr & 0xF))
        self.w(0xE0 + m, mod_wave & 3)
        self.w(0xE0 + c, car_wave & 3)
        self.w(0xC0 + ch, ((feedback & 7) << 1) | (conn & 1))

    def key_on(self, ch, hz):
        f, b = fnum_block(hz)
        self.w(0xB0 + ch, 0)
        self.w(0xA0 + ch, f & 0xFF)
        self.w(0xB0 + ch, 0x20 | (b << 2) | (f >> 8))

    def key_off(self, ch):
        self.w(0xB0 + ch, 0)

    def drum(self, ch, kind):
        if kind == "K":  # kick
            self.patch(ch, mod_mult=1, mod_tl=10, mod_ar=15, mod_dr=12, mod_sl=8, mod_rr=10,
                       car_mult=1, car_tl=8, car_ar=15, car_dr=10, car_sl=12, car_rr=10,
                       feedback=4, sustain=0)
            self.key_on(ch, 72.0)
        elif kind == "S":  # snare
            self.patch(ch, mod_mult=5, mod_tl=4, mod_ar=15, mod_dr=11, mod_sl=10, mod_rr=10,
                       car_mult=3, car_tl=10, car_ar=15, car_dr=11, car_sl=12, car_rr=10,
                       feedback=7, mod_wave=1, sustain=0)
            self.key_on(ch, 240.0)
        elif kind == "H":  # hi-hat
            self.patch(ch, mod_mult=7, mod_tl=8, mod_ar=15, mod_dr=14, mod_sl=14, mod_rr=12,
                       car_mult=6, car_tl=18, car_ar=15, car_dr=14, car_sl=14, car_rr=12,
                       feedback=7, mod_wave=2, sustain=0)
            self.key_on(ch, 880.0)

    def build_vgm(self):
        self.data.append(0x66)
        hdr = bytearray(0x40)
        hdr[0:4] = b"Vgm "
        eof_offset = 0x40 + len(self.data) - 4
        struct.pack_into("<I", hdr, 0x04, eof_offset)
        struct.pack_into("<I", hdr, 0x08, 0x00000151)  # VGM 1.51
        struct.pack_into("<I", hdr, 0x18, self.total_samples)
        if self.loop_pos is not None:
            struct.pack_into("<I", hdr, 0x1C, 0x40 + self.loop_pos - 0x1C)
            struct.pack_into("<I", hdr, 0x20, self.total_samples - self.loop_samples)
        struct.pack_into("<I", hdr, 0x24, 60)  # 60 Hz
        struct.pack_into("<I", hdr, 0x34, 0x40 - 0x34)  # data offset
        struct.pack_into("<I", hdr, 0x50 - 0x50, 0)  # keep v1.51 header at 0x40
        # Wait: YM3812 clock is at 0x50, so use 0x80-byte header for proper VGM 1.51
        hdr = bytearray(0x80)
        hdr[0:4] = b"Vgm "
        struct.pack_into("<I", hdr, 0x04, 0x80 + len(self.data) - 4)
        struct.pack_into("<I", hdr, 0x08, 0x00000151)
        struct.pack_into("<I", hdr, 0x18, self.total_samples)
        if self.loop_pos is not None:
            struct.pack_into("<I", hdr, 0x1C, 0x80 + self.loop_pos - 0x1C)
            struct.pack_into("<I", hdr, 0x20, self.total_samples - self.loop_samples)
        struct.pack_into("<I", hdr, 0x24, 60)
        struct.pack_into("<I", hdr, 0x34, 0x80 - 0x34)
        struct.pack_into("<I", hdr, 0x50, 3579545)  # YM3812 clock
        return bytes(hdr) + bytes(self.data)


def render_song(bpm, patches, patterns):
    """patterns: list of dicts with keys lead, harm, bass, pad, arp, drums (16-step lists)."""
    v = VgmBuilder()
    for ch, p in patches.items():
        v.patch(ch, **p)
    v.mark_loop()
    step_samples = round(VGM_RATE * 60.0 / bpm / 4.0)  # 16th note
    for pat in patterns:
        steps = len(pat["bass"])
        for i in range(steps):
            for ch, key in ((0, "lead"), (1, "harm"), (2, "bass"), (3, "pad"), (4, "arp")):
                if key in pat:
                    n = pat[key][i]
                    if n == ".":
                        v.key_off(ch)
                    elif n != "-":
                        hz = note_hz(n)
                        if hz:
                            v.key_on(ch, hz)
            if "drums" in pat:
                d = pat["drums"][i]
                if d in ("K", "S", "H"):
                    v.drum(5, d)
            v.wait_samples(step_samples)
    return v.build_vgm()


def s16(text):
    toks = text.split()
    assert len(toks) == 16, f"Expected 16 steps, got {len(toks)}: {text}"
    return toks


def build_title():
    # D minor heroic synthwave, 118 BPM
    patches = {
        0: dict(mod_mult=2, mod_tl=18, mod_ar=15, mod_dr=5, mod_sl=3, mod_rr=5,
                car_mult=1, car_tl=10, car_ar=15, car_dr=4, car_sl=2, car_rr=6, feedback=3, car_wave=1),
        1: dict(mod_mult=1, mod_tl=24, mod_ar=14, mod_dr=6, mod_sl=4, mod_rr=5,
                car_mult=1, car_tl=16, car_ar=14, car_dr=5, car_sl=4, car_rr=6, feedback=1),
        2: dict(mod_mult=1, mod_tl=14, mod_ar=15, mod_dr=8, mod_sl=5, mod_rr=7,
                car_mult=1, car_tl=8, car_ar=15, car_dr=6, car_sl=4, car_rr=8, feedback=3, car_wave=1),
        3: dict(mod_mult=1, mod_tl=30, mod_ar=9, mod_dr=3, mod_sl=2, mod_rr=4,
                car_mult=1, car_tl=20, car_ar=9, car_dr=3, car_sl=2, car_rr=4, feedback=1),
        4: dict(mod_mult=3, mod_tl=26, mod_ar=15, mod_dr=9, mod_sl=6, mod_rr=8,
                car_mult=2, car_tl=20, car_ar=15, car_dr=8, car_sl=6, car_rr=8, feedback=1),
    }
    bars = [
        dict(
            lead=s16("D5 - - - F5 - A5 - G5 - F5 - E5 - - ."),
            harm=s16("A4 - - - D5 - F5 - E5 - D5 - C5 - - ."),
            bass=s16("D2 - D3 - D2 - D3 - D2 - D3 - D2 - F2 -"),
            pad =s16("D3 - - - - - - - - - - - - - - -"),
            arp =s16("D4 F4 A4 D5 D4 F4 A4 D5 D4 F4 A4 D5 D4 F4 A4 D5"),
            drums=s16("K - H - S - H - K - H K S - H -"),
        ),
        dict(
            lead=s16("Bb4 - - - D5 - F5 - E5 - D5 - C5 - - ."),
            harm=s16("F4 - - - Bb4 - D5 - C5 - Bb4 - A4 - - ."),
            bass=s16("Bb1 - Bb2 - Bb1 - Bb2 - C2 - C3 - C2 - C3 -"),
            pad =s16("Bb2 - - - - - - - C3 - - - - - - -"),
            arp =s16("Bb3 D4 F4 Bb4 Bb3 D4 F4 Bb4 C4 E4 G4 C5 C4 E4 G4 C5"),
            drums=s16("K - H - S - H - K - H K S - H S"),
        ),
        dict(
            lead=s16("D5 - - - F5 - A5 - C6 - Bb5 - A5 - G5 -"),
            harm=s16("A4 - - - D5 - F5 - A5 - G5 - F5 - E5 -"),
            bass=s16("D2 - D3 - D2 - D3 - F2 - F3 - G2 - G3 -"),
            pad =s16("D3 - - - - - - - F3 - - - G3 - - -"),
            arp =s16("D4 F4 A4 D5 D4 F4 A4 D5 F4 A4 C5 F5 G4 Bb4 D5 G5"),
            drums=s16("K - H - S - H - K - H K S - H -"),
        ),
        dict(
            lead=s16("A5 - - - G5 - F5 - E5 - - - A4 - C#5 -"),
            harm=s16("F5 - - - E5 - D5 - C#5 - - - E4 - A4 -"),
            bass=s16("A2 - A2 - G2 - F2 - E2 - A1 - A2 - A1 -"),
            pad =s16("A2 - - - - - - - A2 - - - - - - -"),
            arp =s16("A3 C#4 E4 A4 A3 C#4 E4 A4 A3 C#4 E4 A4 G4 E4 C#4 A3"),
            drums=s16("K - H - S - H - K K H - S - S S"),
        ),
    ]
    return render_song(118, patches, bars)


def build_map():
    # Calm atmospheric A minor synthwave, 102 BPM
    patches = {
        0: dict(mod_mult=1, mod_tl=26, mod_ar=12, mod_dr=5, mod_sl=3, mod_rr=5,
                car_mult=1, car_tl=14, car_ar=12, car_dr=4, car_sl=3, car_rr=5, feedback=1),
        1: dict(mod_mult=2, mod_tl=30, mod_ar=10, mod_dr=4, mod_sl=3, mod_rr=4,
                car_mult=1, car_tl=20, car_ar=10, car_dr=4, car_sl=3, car_rr=4, feedback=0),
        2: dict(mod_mult=1, mod_tl=20, mod_ar=14, mod_dr=7, mod_sl=4, mod_rr=6,
                car_mult=1, car_tl=12, car_ar=14, car_dr=6, car_sl=4, car_rr=6, feedback=2),
        3: dict(mod_mult=1, mod_tl=32, mod_ar=7, mod_dr=3, mod_sl=2, mod_rr=3,
                car_mult=1, car_tl=22, car_ar=7, car_dr=3, car_sl=2, car_rr=3, feedback=0),
        4: dict(mod_mult=2, mod_tl=28, mod_ar=15, mod_dr=9, mod_sl=8, mod_rr=8,
                car_mult=1, car_tl=22, car_ar=15, car_dr=9, car_sl=8, car_rr=8, feedback=0),
    }
    bars = [
        dict(
            lead=s16("E5 - - - - - D5 - C5 - - - B4 - - ."),
            harm=s16("A4 - - - - - - - G4 - - - - - - ."),
            bass=s16("A2 - - - E2 - - - A2 - - - E2 - G2 -"),
            pad =s16("A3 - - - - - - - - - - - - - - -"),
            arp =s16("A3 C4 E4 A4 E4 C4 A3 C4 E4 A4 E4 C4 A3 C4 E4 A4"),
            drums=s16("K - - - - - H - S - - - - - H -"),
        ),
        dict(
            lead=s16("C5 - - - D5 - E5 - G5 - - - E5 - - ."),
            harm=s16("F4 - - - - - - - G4 - - - - - - ."),
            bass=s16("F2 - - - C2 - - - G2 - - - D2 - - -"),
            pad =s16("F3 - - - - - - - G3 - - - - - - -"),
            arp =s16("F3 A3 C4 F4 C4 A3 F3 A3 G3 B3 D4 G4 D4 B3 G3 B3"),
            drums=s16("K - - - - - H - S - - - K - H -"),
        ),
        dict(
            lead=s16("A5 - - - G5 - E5 - D5 - - - C5 - B4 -"),
            harm=s16("C5 - - - - - - - B4 - - - A4 - - ."),
            bass=s16("A2 - - - E2 - - - D2 - - - A1 - - -"),
            pad =s16("A3 - - - - - - - D3 - - - - - - -"),
            arp =s16("A3 C4 E4 A4 E4 C4 A3 C4 D4 F4 A4 D5 A4 F4 D4 A3"),
            drums=s16("K - - - - - H - S - - - - - H -"),
        ),
        dict(
            lead=s16("E5 - - - - - C5 - B4 - - - - - - ."),
            harm=s16("G4 - - - - - - - E4 - - - - - - ."),
            bass=s16("E2 - - - B1 - - - E2 - - - G2 - E2 -"),
            pad =s16("E3 - - - - - - - - - - - - - - -"),
            arp =s16("E3 G3 B3 E4 B3 G3 E3 G3 E3 G#3 B3 E4 B3 G#3 E3 B3"),
            drums=s16("K - - - - - H - S - - - - - H -"),
        ),
    ]
    return render_song(102, patches, bars)


def build_space():
    # Driving A minor deep space combat, 132 BPM
    patches = {
        0: dict(mod_mult=2, mod_tl=15, mod_ar=15, mod_dr=7, mod_sl=3, mod_rr=7,
                car_mult=1, car_tl=9, car_ar=15, car_dr=5, car_sl=2, car_rr=7, feedback=4, car_wave=1),
        1: dict(mod_mult=3, mod_tl=22, mod_ar=15, mod_dr=7, mod_sl=4, mod_rr=7,
                car_mult=1, car_tl=15, car_ar=15, car_dr=6, car_sl=4, car_rr=7, feedback=2),
        2: dict(mod_mult=1, mod_tl=10, mod_ar=15, mod_dr=9, mod_sl=6, mod_rr=8,
                car_mult=1, car_tl=7, car_ar=15, car_dr=7, car_sl=5, car_rr=8, feedback=4, car_wave=1),
        3: dict(mod_mult=1, mod_tl=28, mod_ar=12, mod_dr=4, mod_sl=3, mod_rr=5,
                car_mult=1, car_tl=20, car_ar=12, car_dr=4, car_sl=3, car_rr=5, feedback=1),
        4: dict(mod_mult=2, mod_tl=24, mod_ar=15, mod_dr=10, mod_sl=8, mod_rr=9,
                car_mult=1, car_tl=18, car_ar=15, car_dr=9, car_sl=7, car_rr=9, feedback=2),
    }
    bars = [
        dict(
            lead=s16("A4 - C5 - E5 - - C5 D5 - C5 - B4 - A4 -"),
            harm=s16("E4 - A4 - C5 - - A4 B4 - A4 - G4 - E4 -"),
            bass=s16("A2 A2 A3 A2 A2 A2 A3 A2 A2 A2 A3 A2 G2 G2 A2 A2"),
            pad =s16("A3 - - - - - - - - - - - - - - -"),
            arp =s16("A4 E4 C4 E4 A4 E4 C4 E4 A4 E4 C4 E4 G4 D4 B3 D4"),
            drums=s16("K - H - S - H K K - H - S - H -"),
        ),
        dict(
            lead=s16("F5 - - E5 D5 - C5 - E5 - - D5 C5 - B4 -"),
            harm=s16("C5 - - C5 A4 - A4 - B4 - - B4 G4 - G4 -"),
            bass=s16("F2 F2 F3 F2 F2 F2 F3 F2 G2 G2 G3 G2 G2 G2 G3 G2"),
            pad =s16("F3 - - - - - - - G3 - - - - - - -"),
            arp =s16("F4 C4 A3 C4 F4 C4 A3 C4 G4 D4 B3 D4 G4 D4 B3 D4"),
            drums=s16("K - H - S - H K K - H K S - H S"),
        ),
        dict(
            lead=s16("A5 - - G5 E5 - C5 - D5 - E5 - F5 - E5 -"),
            harm=s16("E5 - - D5 C5 - A4 - A4 - C5 - D5 - C5 -"),
            bass=s16("A2 A2 A3 A2 A2 A2 A3 A2 D2 D2 D3 D2 D2 D2 D3 D2"),
            pad =s16("A3 - - - - - - - D3 - - - - - - -"),
            arp =s16("A4 E4 C4 E4 A4 E4 C4 E4 D5 A4 F4 A4 D5 A4 F4 A4"),
            drums=s16("K - H - S - H K K - H - S - H -"),
        ),
        dict(
            lead=s16("E5 - - - D5 - C5 - B4 - - - E5 - G#5 -"),
            harm=s16("B4 - - - B4 - A4 - G#4 - - - B4 - E5 -"),
            bass=s16("E2 E2 E3 E2 E2 E2 E3 E2 E2 E2 E3 E2 E2 G#2 B2 E3"),
            pad =s16("E3 - - - - - - - E3 - - - - - - -"),
            arp =s16("E4 B3 G#3 B3 E4 B3 G#3 B3 E4 B3 G#3 B3 E5 B4 G#4 E4"),
            drums=s16("K - H - S - H K K K H - S S H S"),
        ),
    ]
    return render_song(132, patches, bars)


def build_pirates():
    # Urgent E Phrygian pirate ambush / planet storm, 148 BPM
    patches = {
        0: dict(mod_mult=3, mod_tl=12, mod_ar=15, mod_dr=8, mod_sl=3, mod_rr=8,
                car_mult=1, car_tl=8, car_ar=15, car_dr=6, car_sl=2, car_rr=8, feedback=5, car_wave=1),
        1: dict(mod_mult=2, mod_tl=18, mod_ar=15, mod_dr=8, mod_sl=4, mod_rr=8,
                car_mult=1, car_tl=14, car_ar=15, car_dr=6, car_sl=4, car_rr=8, feedback=3),
        2: dict(mod_mult=1, mod_tl=8, mod_ar=15, mod_dr=10, mod_sl=6, mod_rr=9,
                car_mult=1, car_tl=6, car_ar=15, car_dr=8, car_sl=5, car_rr=9, feedback=5, car_wave=1),
        3: dict(mod_mult=1, mod_tl=26, mod_ar=14, mod_dr=5, mod_sl=4, mod_rr=6,
                car_mult=1, car_tl=20, car_ar=14, car_dr=5, car_sl=4, car_rr=6, feedback=2),
        4: dict(mod_mult=4, mod_tl=20, mod_ar=15, mod_dr=11, mod_sl=9, mod_rr=10,
                car_mult=1, car_tl=18, car_ar=15, car_dr=10, car_sl=8, car_rr=10, feedback=3),
    }
    bars = [
        dict(
            lead=s16("E5 - F5 - E5 - - B4 C5 - B4 - A4 - G4 -"),
            harm=s16("B4 - C5 - B4 - - G4 A4 - G4 - F4 - E4 -"),
            bass=s16("E2 E2 E2 F2 E2 E2 E2 G2 E2 E2 E2 F2 E2 D2 C2 B1"),
            pad =s16("E3 - - - F3 - - - E3 - - - - - - -"),
            arp =s16("E4 B4 F4 B4 E4 B4 G4 B4 E4 B4 F4 B4 E4 B4 D4 B4"),
            drums=s16("K H K H S H K H K H K K S H S H"),
        ),
        dict(
            lead=s16("F5 - - E5 D5 - C5 - B4 - C5 - D5 - F5 -"),
            harm=s16("C5 - - B4 A4 - G4 - F4 - G4 - A4 - C5 -"),
            bass=s16("F2 F2 F2 F2 G2 G2 G2 G2 F2 F2 F2 E2 D2 D2 C2 B1"),
            pad =s16("F3 - - - G3 - - - F3 - - - D3 - - -"),
            arp =s16("F4 C5 A4 C5 G4 D5 B4 D5 F4 C5 A4 C5 D4 A4 F4 A4"),
            drums=s16("K H K H S H K H K K S H K S S S"),
        ),
    ]
    return render_song(148, patches, bars)


def build_planet():
    # D Dorian plucked planetary exploration, 114 BPM
    patches = {
        0: dict(mod_mult=2, mod_tl=18, mod_ar=15, mod_dr=9, mod_sl=7, mod_rr=8,
                car_mult=1, car_tl=10, car_ar=15, car_dr=8, car_sl=6, car_rr=8, feedback=2, sustain=0),
        1: dict(mod_mult=3, mod_tl=24, mod_ar=15, mod_dr=9, mod_sl=7, mod_rr=8,
                car_mult=1, car_tl=16, car_ar=15, car_dr=8, car_sl=6, car_rr=8, feedback=1, sustain=0),
        2: dict(mod_mult=1, mod_tl=16, mod_ar=15, mod_dr=8, mod_sl=5, mod_rr=7,
                car_mult=1, car_tl=9, car_ar=15, car_dr=6, car_sl=4, car_rr=7, feedback=2),
        3: dict(mod_mult=1, mod_tl=30, mod_ar=10, mod_dr=4, mod_sl=3, mod_rr=4,
                car_mult=1, car_tl=21, car_ar=10, car_dr=4, car_sl=3, car_rr=4, feedback=0),
        4: dict(mod_mult=2, mod_tl=26, mod_ar=15, mod_dr=11, mod_sl=9, mod_rr=9,
                car_mult=2, car_tl=20, car_ar=15, car_dr=10, car_sl=9, car_rr=9, feedback=1, sustain=0),
    }
    bars = [
        dict(
            lead=s16("D5 - F5 - A5 - G5 - F5 - E5 - D5 - C5 -"),
            harm=s16("A4 - D5 - F5 - E5 - D5 - C5 - A4 - G4 -"),
            bass=s16("D2 - - D3 - - A2 - D2 - - D3 - - C2 -"),
            pad =s16("D3 - - - - - - - - - - - - - - -"),
            arp =s16("D4 A4 F4 A4 D4 A4 F4 A4 D4 A4 F4 A4 C4 G4 E4 G4"),
            drums=s16("K - H - - H S - K - H - K H S -"),
        ),
        dict(
            lead=s16("B4 - D5 - G5 - F5 - E5 - - D5 C5 - A4 -"),
            harm=s16("G4 - B4 - D5 - C5 - C5 - - A4 G4 - E4 -"),
            bass=s16("G2 - - G3 - - D2 - C2 - - C3 - - G2 -"),
            pad =s16("G3 - - - - - - - C3 - - - - - - -"),
            arp =s16("G4 D5 B4 D5 G4 D5 B4 D5 C4 G4 E4 G4 C4 G4 E4 G4"),
            drums=s16("K - H - - H S - K - H K - H S -"),
        ),
        dict(
            lead=s16("D5 - E5 - F5 - A5 - B5 - A5 - G5 - F5 -"),
            harm=s16("A4 - C5 - D5 - F5 - G5 - F5 - E5 - D5 -"),
            bass=s16("D2 - - D3 - - F2 - G2 - - G3 - - D2 -"),
            pad =s16("D3 - - - - - - - G3 - - - - - - -"),
            arp =s16("D4 A4 F4 A4 D4 A4 F4 A4 G4 D5 B4 D5 G4 D5 B4 D5"),
            drums=s16("K - H - - H S - K - H - K H S -"),
        ),
        dict(
            lead=s16("E5 - - D5 C5 - B4 - A4 - - - D5 - - ."),
            harm=s16("C5 - - A4 G4 - G4 - E4 - - - A4 - - ."),
            bass=s16("A2 - - E2 - - G2 - A2 - - E2 - A2 C3 -"),
            pad =s16("A2 - - - - - - - A2 - - - - - - -"),
            arp =s16("A3 E4 C4 E4 A3 E4 C4 E4 A3 E4 C4 E4 G4 E4 C4 A3"),
            drums=s16("K - H - - H S - K - H - S - H S"),
        ),
    ]
    return render_song(114, patches, bars)


def main():
    out = ROOT / "music"
    out.mkdir(exist_ok=True)
    tracks = {
        "Title.vgm": build_title(),
        "Map.vgm": build_map(),
        "Space.vgm": build_space(),
        "Pirates.vgm": build_pirates(),
        "Planet.vgm": build_planet(),
    }
    for name, data in tracks.items():
        (out / name).write_bytes(data)
        print(f"Wrote music/{name} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
