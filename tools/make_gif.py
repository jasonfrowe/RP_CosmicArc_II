#!/usr/bin/env python3
"""Generate a Discord-ready animated showcase GIF of Cosmic Arc II using rp6502-emu + ffmpeg.

Output: docs/cosmic_arc_ii_showcase.gif (640x480 crisp nearest-neighbor, 20 fps).
"""

import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "llvm-mos" / "release" / "RP_CosmicArc_II"
ROM = BUILD / "RP_CosmicArc_II.rp6502"
EMU = BUILD / "rp6502-emu"
OUT = ROOT / "docs" / "cosmic_arc_ii_showcase.gif"


def emit_capture(lines, out_dir, start_idx, total_frames, step=3):
    """Append rp6502-emu run/shot commands capturing every `step` frames."""
    idx = start_idx
    rem = total_frames
    while rem > 0:
        dt = min(step, rem)
        lines.append(f"run {dt}")
        lines.append(f'shot "{out_dir}/f_{idx:04d}.png"')
        idx += 1
        rem -= dt
    return idx


def run_emu(lines, seed=42, save_dir=None):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join(lines) + "\n")
        path = f.name
    cmd = [str(EMU), str(ROM), "--mute", "--memseed", str(seed), "--script", path]
    if save_dir:
        cmd += ["--save-dir", str(save_dir)]
    subprocess.run(cmd, check=True)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        fdir = Path(tmp) / "frames"
        fdir.mkdir()
        sdir = Path(tmp) / "save"
        sdir.mkdir()
        idx = 0

        # ------------------------------------------------------------------
        # Scene 1: Title Screen (1.8s = 108 frames -> 36 GIF frames @ 20fps)
        # ------------------------------------------------------------------
        print("Capturing Scene 1: Title Screen...")
        lines = ["run 15"]
        idx = emit_capture(lines, fdir, idx, 108, step=3)
        run_emu(lines, seed=42)

        # ------------------------------------------------------------------
        # Scene 2: Deep Space Combat + Fighter Launch (~4.8s = 288 frames)
        # ------------------------------------------------------------------
        print("Capturing Scene 2: Deep Space Combat & Fighter...")
        lines = [
            "run 15",
            "press s", "run 2", "release s", "run 4",
            "press s", "run 2", "release s", "run 4",
            "press z", "run 2", "release z",
            "run 130",  # let Ark arrive and asteroids enter
            "press z",
        ]
        # Ark aiming & firing at asteroids (1.4s)
        idx = emit_capture(lines, fdir, idx, 36, step=3)
        lines += ["press d"]
        idx = emit_capture(lines, fdir, idx, 24, step=3)
        lines += ["release d", "press s"]
        idx = emit_capture(lines, fdir, idx, 24, step=3)
        lines += ["release s", "release z"]
        # Launch Fighter Saucer (B = x) and dogfight! (3.4s)
        lines += ["press x", "run 2", "release x", "press d", "press z"]
        idx = emit_capture(lines, fdir, idx, 36, step=3)
        lines += ["release d", "press w"]
        idx = emit_capture(lines, fdir, idx, 42, step=3)
        lines += ["release w", "press a"]
        idx = emit_capture(lines, fdir, idx, 48, step=3)
        lines += ["release a", "press s"]
        idx = emit_capture(lines, fdir, idx, 42, step=3)
        lines += ["release s", "release z"]
        idx = emit_capture(lines, fdir, idx, 36, step=3)
        run_emu(lines, seed=77)

        # ------------------------------------------------------------------
        # Scene 3: Planetary Extraction (~4.6s = 276 frames)
        # ------------------------------------------------------------------
        print("Capturing Scene 3: Planetary Extraction...")
        lines = [
            "run 15",
            "press s", "run 2", "release s", "run 3",
            "press s", "run 2", "release s", "run 3",
            "press s", "run 2", "release s", "run 3",
            "press z", "run 2", "release z",
            "run 60",  # Ark arrives in orbit
        ]
        # Brief orbit shot, then launch shuttle down the tunnel and fly low/high to dodge pulse
        idx = emit_capture(lines, fdir, idx, 24, step=3)
        lines += ["press s"]
        idx = emit_capture(lines, fdir, idx, 36, step=3)
        lines += ["release s", "press d"]
        idx = emit_capture(lines, fdir, idx, 54, step=3)
        lines += ["release d", "press z"]  # Shimmering Tractor Beam!
        idx = emit_capture(lines, fdir, idx, 48, step=3)
        lines += ["release z", "press s", "press d"]  # Dive under laser pulse!
        idx = emit_capture(lines, fdir, idx, 42, step=3)
        lines += ["release s", "release d", "press z"]
        idx = emit_capture(lines, fdir, idx, 48, step=3)
        lines += ["release z"]
        run_emu(lines, seed=42)

        # ------------------------------------------------------------------
        # Scene 4: Galaxy Star Chart, Close-Up & Starport (~3.6s = 216 frames)
        # ------------------------------------------------------------------
        print("Capturing Scene 4: Galaxy Chart, Close-Up & Starport...")
        lines = [
            "run 15",
            "press z", "run 2", "release z", "run 8",
            "press z", "run 2", "release z", "run 12",
            # Warp twice so the Star Chart has a nice cluster
            "press s", "run 2", "release s", "run 4",
            "press z", "run 2", "release z", "run 10",
            "press s", "run 2", "release s", "run 4",
            "press z", "run 2", "release z", "run 10",
            # Open 4bpp Star Chart (R1 = e)
            "press e", "run 2", "release e", "run 6",
        ]
        idx = emit_capture(lines, fdir, idx, 24, step=3)
        lines += ["press a", "run 2", "release a"]
        idx = emit_capture(lines, fdir, idx, 24, step=3)
        lines += ["press s", "run 2", "release s"]
        idx = emit_capture(lines, fdir, idx, 24, step=3)
        # Flip to Close-Up Circuit (R1 = e)
        lines += ["press e", "run 2", "release e"]
        idx = emit_capture(lines, fdir, idx, 54, step=3)
        # Flip to Achievements (R1 = e)
        lines += ["press e", "run 2", "release e"]
        idx = emit_capture(lines, fdir, idx, 45, step=3)
        # Back to Sector (B = x)
        lines += ["press x", "run 2", "release x"]
        idx = emit_capture(lines, fdir, idx, 45, step=3)
        run_emu(lines, seed=42, save_dir=sdir)

        print(f"Captured {idx} frames ({idx / 20.0:.1f}s @ 20fps). Encoding GIF with ffmpeg...")
        pal = Path(tmp) / "palette.png"
        inp = str(fdir / "f_%04d.png")
        vf_base = "scale=640:480:flags=neighbor"

        subprocess.run(
            [
                "ffmpeg", "-y", "-framerate", "20", "-i", inp,
                "-vf", f"{vf_base},palettegen=max_colors=128:stats_mode=diff",
                str(pal),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        subprocess.run(
            [
                "ffmpeg", "-y", "-framerate", "20", "-i", inp, "-i", str(pal),
                "-lavfi", f"{vf_base} [x]; [x][1:v] paletteuse=dither=none:diff_mode=rectangle",
                "-loop", "0",
                str(OUT),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    size_kb = OUT.stat().st_size / 1024.0
    print(f"Created {OUT.relative_to(ROOT)}: {size_kb:.1f} KB ({size_kb/1024.0:.2f} MB), {idx} frames @ 20 fps")


if __name__ == "__main__":
    main()
