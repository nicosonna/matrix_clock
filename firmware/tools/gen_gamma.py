#!/usr/bin/env python3
"""Prints the gamma 2.2 lookup table embedded in components/display/include/display/gamma.hpp.

The firmware uses no floating point: the table is computed here, once, and
pasted as a literal. The host test AFF-04 checks the embedded table against the
same formula's properties (monotone, endpoints, mid-point).

Usage: python tools/gen_gamma.py
"""
GAMMA = 2.2


def main() -> None:
    values = [round(255 * ((i / 255) ** GAMMA)) for i in range(256)]
    for row in range(0, 256, 16):
        print("    " + ", ".join(f"{v:3d}" for v in values[row:row + 16]) + ",")


if __name__ == "__main__":
    main()
