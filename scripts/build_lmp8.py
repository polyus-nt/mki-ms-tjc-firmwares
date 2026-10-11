#!/usr/bin/env python3
"""Compatibility entrypoint: FabLab now uses lmp-a3, not lmp8."""
from build_lmp import main

if __name__ == "__main__":
    print("FabLab uses lmp-a3; output is build/lmp/sketch.bin, not build/lmp8.")
    main()
