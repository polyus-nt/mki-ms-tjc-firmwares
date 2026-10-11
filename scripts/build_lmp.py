#!/usr/bin/env python3
"""Build FabLab usercode for the three-output tjc-ms1-lmp-a3 platform."""
import argparse
import hashlib
from pathlib import Path
from build_gateways import FILES, REVISION, find_tools, prepare_runtime, patch_uart, replace_once, run

LED_FILES = {
    "UART.hpp": "cfad4f3c3787dacf4de5d3cc38412b00d868826d6ca6d1bf91ddc0c6c18d1d82",
    "system.c": "d90623f6893ae24f5f82538ff3490d638239c7ea2608de89d423a827dddb6480",
    "LED.h": "6f6907b429b3874ac489b4a8629a19178c12cfab2750ae1bf0eecca81664471c",
    "PWM.hpp": "9db97feb4159c6ac16ffaa42d955fd55ea79aefb7690d9606dc17874980c59a3",
    "PWMInit.hpp": "c6910d797b036fd759a7a147ee17f1029e3157ec0058826ec96f9ca10d32b5fa",
    "Connector.hpp": "86d0a634a1fc23ec611cf106eb28948229fddc6f21031954ca562945815166bd",
}


def prepare_lmp_runtime(source, platform):
    # Only line endings and trailing EOF whitespace are normalized.
    for name, expected in LED_FILES.items():
        text = (source / name).read_text(encoding="utf-8").rstrip() + "\n"
        if hashlib.sha256(text.encode("utf-8")).hexdigest() != expected:
            raise ValueError("Wrong lmp upstream revision or modified file: " + name)
        (platform / name).write_text(text, encoding="utf-8")


def patch_led(text):
    text = replace_once(text, '#include "PWM.hpp"',
                        '#include "BoardConfig.h"\n#include "PWM.hpp"')
    start = text.index("        RCC -> IOPENR |=")
    end = text.index("        off();", start) + len("        off();")
    # Never call startup off() for a lock: it would intentionally unlock it.
    text = text[:start] + """        RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
        const bool high = tjc_lmp::startupHigh(ledPin - 1);
        GPIOA->BSRR = 1UL << (high ? pin : pin + 16); // latch BEFORE output mode
        GPIOA->OTYPER &= ~(GPIO_OTYPER_OT0 << pin);
        GPIOA->PUPDR &= ~(3UL << (pin * 2U));
        GPIOA->MODER = (GPIOA->MODER & ~(3UL << (pin * 2U))) | (1UL << (pin * 2U));
        value = high;""" + text[end:]
    return text


def patch_connector(text):
    # Upstream Connector has a global lambda which sets ALL three outputs LOW
    # before LED constructors. Let LED own initialization instead.
    start = text.index("auto&& init = []() -> int {")
    end = text.index("}();", start) + len("}();")
    return text[:start] + "// GPIO startup is owned by patched LED constructors.\n" + text[end:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler-root", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--toolchain-bin", type=Path)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = (args.out or root / "build/lmp").resolve()
    platform = out / "platform"
    shared = {name: sha for name, sha in FILES.items()
              if name not in ("ArduinoAdapter.hpp", "arduino_link.c") and name not in LED_FILES}
    external = (args.compiler_root.resolve() / "compiler/library/tjc-ms1-lmp-a3/1.0/source"
                if args.compiler_root else None)
    prepare_runtime(external or root / "firmware/runtime", platform, shared)
    prepare_lmp_runtime(external or root / "firmware/runtime-lmp", platform)
    for name, patch in (("LED.h", patch_led), ("Connector.hpp", patch_connector),
                        ("UART.hpp", patch_uart)):
        path = platform / name
        path.write_text(patch(path.read_text(encoding="utf-8")), encoding="utf-8")
    if args.prepare_only:
        print("Verified lmp-a3 runtime with safe startup in", platform)
        return
    tools = find_tools(args.toolchain_bin)
    elf = out / "sketch.elf"
    # Upstream PWMInit uses C++14 digit separators.
    run([tools["arm-none-eabi-g++"], "-std=gnu++14", "-mcpu=cortex-m0plus",
         "-mthumb", "-DSTM32G030x6", '-D_FIR_GIT_REV="' + REVISION + '"',
         "-I", platform, "-I", root / "firmware/lmp", "-T", platform / "tjc-ms1-b4272ba421624bbe-usercode.ld",
         platform / "startup_stm32g030xx.s", root / "firmware/lmp/sketch.cpp",
         "--specs=nosys.specs", "-Os", "-g", "-fno-exceptions", "-fno-rtti",
         "-ffunction-sections", "-fdata-sections", "-fno-strict-aliasing",
         "-Wl,--gc-sections", "-Wl,-Map=" + str(out / "sketch.map"), "-o", elf])
    run([tools["arm-none-eabi-objcopy"], "-Obinary", elf, out / "sketch.bin"])
    run([tools["arm-none-eabi-size"], elf])


if __name__ == "__main__":
    main()
