#!/usr/bin/env python3
"""把 PROG 头里的装入长度和 bss 长度写成真实值。"""
import subprocess
import struct
import sys


def sym_addr(elf, name):
    out = subprocess.check_output(["nm", elf], text=True)
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == name:
            return int(parts[0], 16)
    return None


def main():
    elf, raw = sys.argv[1], sys.argv[2]
    data = bytearray(open(raw, "rb").read())
    if data[:4] != b"PROG":
        raise SystemExit(raw + ": missing PROG header")
    bss_start = sym_addr(elf, "__bss_start")
    end = sym_addr(elf, "_end")
    bss = 0 if bss_start is None or end is None else end - bss_start
    struct.pack_into("<III", data, 4, 16, len(data), bss)
    open(raw, "wb").write(data)


if __name__ == "__main__":
    main()
