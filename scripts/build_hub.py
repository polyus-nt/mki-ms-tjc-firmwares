#!/usr/bin/env python3
"""Build handwritten main-a4 hub using the pinned, verified SimpleBus runtime."""
import argparse
from pathlib import Path
from build_gateways import FILES, REVISION, find_tools, prepare_runtime, run

LED_FILES = {
    "LED.h": "025c3116db4eebcbde5cd082a5e42bf71ce3522ee15bfac5b61b5a939ea7f68b",
    "PWM.hpp": "44aaf6df004ed7144c703c470ce55e5baa18376c7e593228ecb8e89c3edf4e1e",
    "PWMInit.hpp": "dc59b198513aed95034a465102b16f2dfc9e97ac03c070c587bb6c69516ed033",
    "Connector.hpp": "a634b3fac8a97e23c5912d6900f9626611319f2ac12d8418c2f99553adcb4a2a",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler-root", type=Path,
                        help="optional external runtime override; default: firmware/runtime")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--toolchain-bin", type=Path)
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--test-echo", action="store_true",
                        help="build SimpleBus byte-response test instead of the API hub")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    variant = "hub-test" if args.test_echo else "hub"
    out = (args.out or root / "build" / variant).resolve()
    platform = out / "platform"
    # These main-a4 files are byte-identical to the pinned gateway runtime.
    # Exclude Arduino-specific files: the hub has no Austere hardware interface.
    files = {name: sha for name, sha in FILES.items()
             if name not in ("ArduinoAdapter.hpp", "arduino_link.c")}
    if args.test_echo:
        files.update(LED_FILES)
    source = (args.compiler_root.resolve() / "compiler/library/tjc-ms1-main-a4/1.0/source"
              if args.compiler_root else root / "firmware/runtime")
    prepare_runtime(source, platform, files)
    if args.prepare_only:
        print("Verified and patched main-a4 runtime in", platform)
        return
    tools = find_tools(args.toolchain_bin)
    elf = out / "sketch.elf"
    # Upstream PWMInit.hpp uses C++14 digit separators in integer literals.
    standard = "-std=gnu++14" if args.test_echo else "-std=gnu++11"
    run([tools["arm-none-eabi-g++"], standard, "-mcpu=cortex-m0plus",
         "-mthumb", "-DSTM32G030x6", '-D_FIR_GIT_REV="' + REVISION + '"',
         "-I", platform, "-T", platform / "tjc-ms1-b4272ba421624bbe-usercode.ld",
         platform / "startup_stm32g030xx.s", root / "firmware" / variant / "sketch.cpp",
         "--specs=nosys.specs", "-Os", "-g", "-fno-exceptions", "-fno-rtti",
         "-ffunction-sections", "-fdata-sections", "-fno-strict-aliasing",
         "-Wl,--gc-sections", "-Wl,-Map=" + str(out / "sketch.map"), "-o", elf])
    run([tools["arm-none-eabi-objcopy"], "-Obinary", elf, out / "sketch.bin"])
    run([tools["arm-none-eabi-size"], elf])


if __name__ == "__main__":
    main()
