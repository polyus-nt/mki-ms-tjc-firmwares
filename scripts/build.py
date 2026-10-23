#!/usr/bin/env python3
"""Build hub and gateways from the local runtime (Windows/Linux/macOS)."""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("all", "hub", "hub-test", "buttons-test", "gateways", "gateway-actuator-2", "buttons", "lmp", "lmp8"), default="all")
    parser.add_argument("--toolchain-bin", type=Path, help="ARM GCC bin directory, otherwise PATH")
    parser.add_argument("--out", type=Path, help="build output root, otherwise build/")
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    scripts = Path(__file__).resolve().parent
    targets = ("hub", "gateways", "buttons", "lmp") if args.target == "all" else (args.target,)
    for target in targets:
        if target == "lmp8":
            print("Deprecated FabLab target lmp8 -> lmp-a3; output: build/lmp")
            target = "lmp"
        builder = target.split("-")[0] if target in ("hub-test", "buttons-test") else ("gateways" if target == "gateway-actuator-2" else target)
        command = [sys.executable, str(scripts / f"build_{builder}.py")]
        if target in ("hub-test", "buttons-test"):
            command += ["--test-echo"]
        if target == "gateway-actuator-2":
            command += ["--gateway", "actuator-2"]
        if args.toolchain_bin:
            command += ["--toolchain-bin", str(args.toolchain_bin.resolve())]
        if args.out:
            command += ["--out", str(args.out.resolve() / target)]
        if args.prepare_only:
            command += ["--prepare-only"]
        subprocess.run(command, check=True)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
