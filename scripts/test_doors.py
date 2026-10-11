"""Native door automation tests for FabLab and chem-reactor; requires g++."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="greenhouse-doors-") as directory:
        for name, source, flags in (
            ("fablab", root / "tests/door_automation_test.cpp", []),
            ("fablab-outputs", root / "tests/lmp_outputs_test.cpp", []),
            ("chem-outputs", root / "chem-reactor-теплица/tests/lmp8_outputs_test.cpp",
             ["-DCHEM_OUTPUTS"]),
            ("chem", root / "chem-reactor-теплица/tests/door_automation_test.cpp",
             ["-DCHEM_DOORS"]),
        ):
            binary = Path(directory) / (name + ".exe")
            subprocess.run([args.cxx, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                            *flags, str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
