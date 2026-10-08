#!/usr/bin/env python3
"""Build handwritten main-a4 hub using the pinned, verified SimpleBus runtime."""
import argparse
from pathlib import Path
import shutil
from build_gateways import FILES, REVISION, prepare_runtime, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler-root", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--toolchain-bin", type=Path)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = (args.out or root / "build/hub").resolve()
    platform = out / "platform"
    # These main-a4 files are byte-identical to the pinned gateway runtime.
    # Exclude Arduino-specific files: the hub has no Austere hardware interface.
    files = {name: sha for name, sha in FILES.items()
             if name not in ("ArduinoAdapter.hpp", "arduino_link.c")}
    source = args.compiler_root.resolve() / "compiler/library/tjc-ms1-main-a4/1.0/source"
    prepare_runtime(source, platform, files)
    if args.prepare_only:
        print("Verified and patched main-a4 runtime in", platform)
        return
    tools = {}
    for tool in ("arm-none-eabi-g++", "arm-none-eabi-objcopy", "arm-none-eabi-size"):
        located = str(args.toolchain_bin / tool) if args.toolchain_bin else shutil.which(tool)
        if not located or not Path(located).is_file():
            raise SystemExit("Missing ARM target tool: " + tool)
        tools[tool] = located
    elf = out / "sketch.elf"
    run([tools["arm-none-eabi-g++"], "-std=gnu++11", "-mcpu=cortex-m0plus",
         "-mthumb", "-DSTM32G030x6", '-D_FIR_GIT_REV="' + REVISION + '"',
         "-I", platform, "-T", platform / "tjc-ms1-b4272ba421624bbe-usercode.ld",
         platform / "startup_stm32g030xx.s", root / "firmware/hub/sketch.cpp",
         "--specs=nosys.specs", "-Os", "-g", "-fno-exceptions", "-fno-rtti",
         "-ffunction-sections", "-fdata-sections", "-fno-strict-aliasing",
         "-Wl,--gc-sections", "-Wl,-Map=" + str(out / "sketch.map"), "-o", elf])
    run([tools["arm-none-eabi-objcopy"], "-Obinary", elf, out / "sketch.bin"])
    run([tools["arm-none-eabi-size"], elf])


if __name__ == "__main__":
    main()
