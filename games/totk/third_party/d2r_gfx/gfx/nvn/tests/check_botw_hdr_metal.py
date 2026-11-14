"""Compile synthetic MSL and emitted twins. Never fetch a toolchain or game MSL."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("fixture", type=Path)
parser.add_argument("synthetic_twin", type=Path)
parser.add_argument("game_twin", type=Path)
args = parser.parse_args()
out = args.synthetic_twin.parent
sources = [
    (args.fixture, out / "botw_compose_synthetic.air"),
    (args.synthetic_twin, args.synthetic_twin.with_suffix(".air")),
]
if os.environ.get("BOTW_HDR_COMPOSE_MSL"):
    sources.append((args.game_twin, args.game_twin.with_suffix(".air")))
for source, _ in sources:
    if not source.is_file():
        parser.error(f"missing shader: {source}")

if not shutil.which("xcrun") or subprocess.run(
    ["xcrun", "--find", "metal"], capture_output=True
).returncode:
    print("SKIP Metal compile (xcrun metal unavailable; rewrite checks ran separately)")
else:
    for source, output in sources:
        subprocess.run([
            "xcrun", "metal", "-c", str(source), "-o", str(output),
            f"-fmodules-cache-path={out / 'metal-module-cache'}",
        ], check=True)
        print(f"PASS Metal compile: {source}")
