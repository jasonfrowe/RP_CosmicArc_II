#!/usr/bin/env python3
"""Automated headless playtest runner for RP_CosmicArc_II."""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "llvm-mos" / "release" / "RP_CosmicArc_II"
ROM = BUILD / "RP_CosmicArc_II.rp6502"
EMU = BUILD / "rp6502-emu"


def run_script(script, save_dir=None, seed=42):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(script)
        path = f.name
    cmd = [str(EMU), str(ROM), "--mute", "--memseed", str(seed), "--script", path]
    if save_dir:
        cmd += ["--save-dir", str(save_dir)]
    r = subprocess.run(cmd)
    if r.returncode != 0:
        sys.exit(r.returncode)


def main():
    out = ROOT / "docs"
    out.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as sdir:
        # 1. Voyage flow: New Voyage -> Sector -> Port -> Warp -> Chart -> Close-up -> Achievements
        run_script(
            f'run 20\n'
            f'shot "{out}/m1_title.png"\n'
            f'press z\nrun 2\nrelease z\nrun 10\n'
            f'press z\nrun 2\nrelease z\nrun 15\n'
            f'shot "{out}/m4_sector.png"\n'
            f'press z\nrun 2\nrelease z\nrun 15\n'
            f'shot "{out}/m4_port.png"\n'
            f'press escape\nrun 2\nrelease escape\nrun 10\n'
            f'press s\nrun 2\nrelease s\nrun 5\n'
            f'press z\nrun 2\nrelease z\nrun 12\n'
            f'press x\nrun 2\nrelease x\nrun 12\n'
            f'shot "{out}/m4_chart.png"\n'
            f'press x\nrun 2\nrelease x\nrun 12\n'
            f'shot "{out}/m4_closeup.png"\n'
            f'press x\nrun 2\nrelease x\nrun 12\n'
            f'shot "{out}/m4_achieve.png"\n',
            save_dir=sdir,
            seed=42,
        )
        # 2. Verify Continue loads the saved slot cleanly!
        save_file = Path(sdir) / "ca2_s0.dat"
        assert save_file.exists() and save_file.stat().st_size > 0, "Save file missing!"
        run_script(
            'run 20\n'
            'press s\nrun 2\nrelease s\nrun 5\n'
            'press z\nrun 2\nrelease z\nrun 10\n'
            'press z\nrun 2\nrelease z\nrun 15\n',
            save_dir=sdir,
            seed=42,
        )
        print(f"Save/Continue verified ({save_file.stat().st_size} bytes)")
    print("All playtest checks passed!")


if __name__ == "__main__":
    main()
