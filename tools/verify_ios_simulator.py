#!/usr/bin/env python3
"""Compare the existing headless runner on macOS and inside the iOS app.

Requires an installed DKC1_IOS_DIAGNOSTICS=ON app on an explicitly selected
booted simulator. No device automation or changes to desktop save slots.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
BUNDLE = "com.flat2vr.dkc1recomp.ios"


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, capture_output=True, **kwargs)


def fingerprints(text):
    values = dict(re.findall(r"(\w+_sha256|audio_fnv1a)=([0-9a-f]+)", text))
    if len(values) != 7 or "result=completed" not in text:
        raise RuntimeError("Incomplete successful frame/memory/audio report")
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--headless", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--route", type=Path, default=ROOT / "recipes/route_jungle.dks")
    parser.add_argument("--frames", type=int, default=12000)
    parser.add_argument("--render-width", type=int, default=256,
                        help="Even native source width, 256..448; 418 matches the connected iPhone")
    args = parser.parse_args()
    if not 256 <= args.render_width <= 448 or args.render_width % 2:
        parser.error("--render-width must be even and in 256..448")
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    documents = Path(run("xcrun", "simctl", "get_app_container", args.simulator, BUNDLE, "data").stdout.strip()) / "Documents"
    # Each invocation owns a separate output directory in the app sandbox.
    private = documents / output.name
    private.mkdir(exist_ok=False)
    shutil.copyfile(args.rom, documents / "game.sfc")
    route = private / "route.dks"
    shutil.copyfile(args.route, route)
    reports = []
    for repeat in range(3):
        for platform in ("macos", "ios"):
            directory = (private if platform == "ios" else output) / f"{platform}-{repeat+1}"
            directory.mkdir()
            settings = {
                "DKC1_WIDESCREEN": "0", "DKC1_RENDER_WIDTH": str(args.render_width),
                "DKC1_SCRIPT": str(route),
                "DKC1_SESSION_DIR": str(directory),
                "DKC1_FRAME_PPM": str(directory / "final.ppm"),
                "DKC1_WRAM_OUTPUT": str(directory / "wram.bin"),
                "DKC1_VRAM_OUTPUT": str(directory / "vram.bin"),
                "DKC1_SAVESTATE_OUTPUT": str(directory / "final.state"),
            }
            log = output / f"{platform}-{repeat+1}.stdout.log"
            errors = output / f"{platform}-{repeat+1}.stderr.log"
            if platform == "macos":
                result = run(str(args.headless.resolve()), str(args.rom.resolve()), str(args.frames),
                             cwd=directory, env={**os.environ, **settings})
                log.write_text(result.stdout)
                errors.write_text(result.stderr)
            else:
                subprocess.run(["xcrun", "simctl", "terminate", args.simulator, BUNDLE], capture_output=True)
                status = documents / "verification-result.txt"
                status.unlink(missing_ok=True)
                env = {**os.environ, **{f"SIMCTL_CHILD_{k}": v for k, v in settings.items()},
                       "SIMCTL_CHILD_DKC1_IOS_VERIFY_FRAMES": str(args.frames)}
                run("xcrun", "simctl", "launch", f"--stdout={log}", f"--stderr={errors}", args.simulator, BUNDLE, "--verify", env=env)
                deadline = time.monotonic() + 300
                while not status.exists() and time.monotonic() < deadline:
                    time.sleep(0.25)
                if not status.exists() or status.read_text().strip() != "0":
                    raise RuntimeError(f"iOS verification failed; inspect {errors}")
                shutil.copytree(directory, output / directory.name)
            report = fingerprints(log.read_text())
            reports.append({"platform":platform, "repeat":repeat+1, **report})
            print(f"{platform} repeat {repeat+1}: {report['frame_sha256']}", flush=True)
    reference = {k:v for k,v in reports[0].items() if k not in ("platform", "repeat")}
    passed = all({k:v for k,v in report.items() if k not in ("platform", "repeat")} == reference for report in reports)
    (output / "comparison.json").write_text(json.dumps({"passed":passed,"render_width":args.render_width,"runs":reports}, indent=2)+"\n")
    if not passed:
        raise RuntimeError("Platform/repeat fingerprint mismatch")
    print("PASS: three iOS and three macOS replays match frame, WRAM, VRAM, CGRAM, both OAM hashes and audio.")


if __name__ == "__main__":
    main()
