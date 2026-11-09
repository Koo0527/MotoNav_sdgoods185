#!/usr/bin/env python3
"""Generate the SDGOODS device partition table with ONE real 3MB app slot.

Source of truth: COM7_partition_table.bin (read back from the actual device with
`esptool --no-stub read_flash 0x8000 0x1000`), so bootloader / launcher / otadata /
appdata offsets stay byte-identical to the official 2.0 dynamic-slot table.

Only three things change:
  * ota_0 (entry 4) size: 0 -> 0x300000, so the bootloader can boot an app from it
  * ota_1..ota_15 placeholders move from 0x320000 to 0x620000 (still size 0).
    They would otherwise overlap the now-real ota_0 and gen_esp32part.py / the
    bootloader would reject the table. They stay consecutive ota_N subtypes
    (0x11..0x1f), so esp_ota_get_app_partition_count() still reports 16 slots.
  * the partition-table MD5 entry is recomputed (otherwise bootloader rejects it)

The 15 placeholder slots are preserved, so the launcher still sees the official
2.0 dynamic-slot shape and can allocate further slots in 0x620000..0xFF0000.

Outputs:
  partition-table.bin  -> flash at 0x8000
  pt_bak.bin           -> flash at 0xFF0000 (keep the backup in sync, otherwise
                          the boot layer's self-heal would restore the old table)
"""
import hashlib
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent.parent / "backup" / "COM7_partition_table.bin"
SLOT0_SIZE = 0x300000          # 3 MiB, must be >= app.bin and >= SDGOODS_SLOT_SAFE_BYTES
OTA0_ENTRY = 4                 # 0:nvs 1:phy_init 2:launcher 3:otadata 4:ota_0
ENTRY_SIZE = 32
SIZE_FIELD = 8                 # esp_partition_entry_t: magic(H) type(B) subtype(B) offset(I) size(I)


def main():
    if not SRC.exists():
        raise SystemExit(f"missing device partition table dump: {SRC}")
    b = bytearray(SRC.read_bytes())
    if b[:2] != b"\xaa\x50":
        raise SystemExit("bad partition table magic (expected AA50)")

    # Locate the MD5 trailer entry (starts with EB EB + 14 x FF).
    md5_off = None
    for off in range(0, 0xC00, ENTRY_SIZE):
        if b[off:off + 2] == b"\xeb\xeb":
            md5_off = off
            break
    if md5_off is None:
        raise SystemExit("MD5 trailer not found - refusing to write an unverifiable table")

    entry = OTA0_ENTRY * ENTRY_SIZE
    if b[entry:entry + 2] != b"\xaa\x50":
        raise SystemExit("entry 4 is not a partition entry")
    old_size = struct.unpack_from("<I", b, entry + SIZE_FIELD)[0]
    if old_size not in (0, SLOT0_SIZE):
        raise SystemExit(f"unexpected ota_0 size 0x{old_size:x}; refusing to overwrite")
    struct.pack_into("<I", b, entry + SIZE_FIELD, SLOT0_SIZE)

    # Move the 15 size-0 placeholders out of ota_0's range. Keeping their
    # subtypes consecutive (ota_1..ota_15) is what the IDF bootloader needs.
    for idx in range(5, 20):                 # entries 5..19 = ota_1..ota_15
        e = idx * ENTRY_SIZE
        if b[e:e + 2] != b"\xaa\x50":
            raise SystemExit(f"entry {idx} is not a partition entry")
        old = struct.unpack_from("<I", b, e + 4)[0]      # offset field
        if old != 0x320000:
            raise SystemExit(f"unexpected ota_{idx-4} offset 0x{old:x}")
        if struct.unpack_from("<I", b, e + SIZE_FIELD)[0] != 0:
            raise SystemExit(f"ota_{idx-4} is not a size-0 placeholder")
        struct.pack_into("<I", b, e + 4, 0x620000)

    # ESP-IDF: md5 is computed over every 32-byte entry BEFORE the MD5 entry.
    digest = hashlib.md5(bytes(b[:md5_off])).digest()
    b[md5_off + 16:md5_off + 32] = digest

    (HERE / "partition-table.bin").write_bytes(bytes(b))
    (HERE / "pt_bak.bin").write_bytes(bytes(b))
    print(f"ota_0 size 0x{old_size:x} -> 0x{SLOT0_SIZE:x}")
    print(f"MD5 trailer at 0x{md5_off:x}: {digest.hex()}")
    print("wrote partition-table.bin / pt_bak.bin (4096 bytes each)")


if __name__ == "__main__":
    main()
