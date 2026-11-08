#!/usr/bin/env python3
"""把用户程序写到 myos.img 里内核后面的 MYFS，并在引导扇区记下起点。"""
import struct
import sys

SECTOR = 512
DIR_SECTORS = 8
DATA_LBA = 1 + DIR_SECTORS


def main():
    img_path = sys.argv[1]
    myfs_lba = int(sys.argv[2], 0)
    args = sys.argv[3:]
    if len(args) % 3 != 0:
        raise SystemExit("usage: packmyfs.py image myfs_lba key title file ...")
    items = []
    for i in range(0, len(args), 3):
        key, title, path = args[i], args[i + 1], args[i + 2]
        items.append((key, title, open(path, "rb").read()))

    cursor = DATA_LBA
    placed = []
    for key, title, data in items:
        placed.append((key, title, cursor, data))
        cursor += (len(data) + SECTOR - 1) // SECTOR

    size = (myfs_lba + cursor) * SECTOR
    with open(img_path, "r+b") as f:
        f.seek(0, 2)
        cur = f.tell()
        if cur < size:
            f.write(b"\0" * (size - cur))
        f.seek(0x1B0)
        existing = f.read(4)
        if existing != b"\0\0\0\0":
            raise SystemExit("boot sector 0x1B0 is already used")
        f.seek(0x1B0)
        f.write(struct.pack("<I", myfs_lba))

        superblock = bytearray(SECTOR)
        superblock[0:4] = b"MYFS"
        struct.pack_into("<III", superblock, 4, 1, len(placed), SECTOR)
        base = myfs_lba * SECTOR
        f.seek(base)
        f.write(superblock)

        directory = bytearray(DIR_SECTORS * SECTOR)
        for i, (key, title, lba, data) in enumerate(placed):
            off = i * 64
            directory[off] = ord(key)
            text = title.encode()[:30]
            directory[off + 1:off + 1 + len(text)] = text
            struct.pack_into("<III", directory, off + 32, lba, len(data), 1)
        f.seek(base + SECTOR)
        f.write(directory)
        for key, title, lba, data in placed:
            f.seek(base + lba * SECTOR)
            f.write(data)


if __name__ == "__main__":
    main()
