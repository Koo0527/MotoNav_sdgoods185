#!/usr/bin/env python3
"""Build MotoNav as a SDGOODS launcher app image (pure app.bin, no addresses).

Unlike build_143c.py this NEVER produces a merged/full_0x0 image: in multi-app
mode flashing a merged image would overwrite the official bootloader, partition
table and launcher. Only the address-free app image is a valid app package.

Validated by the official tools/pack_app.py: 0xE9 image magic,
0xABCD5432 esp_app_desc at 0x20, size <= SDGOODS_SLOT_SAFE_BYTES (3040870).
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
# 32MB Flash + 8MB Octal PSRAM; USB-Serial-JTAG CDC. The sketch-local
# partitions.csv (boards/sdgoods/MotoNav/partitions.csv) overrides the menu.
FQBN = ('esp32:esp32:esp32s3:FlashSize=32M,'
        'PartitionScheme=app13M_data7M_32MB,PSRAM=opi,CDCOnBoot=cdc')
SLOT_SAFE_BYTES = 3040870          # SDGOODS_SLOT_SAFE_BYTES (2.9 MiB)


def package(output, dest, version):
    app = (output / 'MotoNav.ino.bin').read_bytes()
    if not app or app[0] != 0xE9:
        raise ValueError('Invalid ESP32 application image')
    if len(app) > SLOT_SAFE_BYTES:
        raise ValueError(f'app.bin is {len(app)} B, exceeds SDGOODS slot safe limit {SLOT_SAFE_BYTES} B')
    dest.mkdir(parents=True, exist_ok=True)
    label = f'MotoNav_sdgoods_v{version}'
    name = f'{label}_app.bin'
    (dest / name).write_bytes(app)
    manifest = dict(
        firmware=version, platform='sdgoods', board='SDGOODS ESP32-S3 360x360',
        esp32Core='3.3.10', flashSize='32MB',
        mode='DIRECT BOOT (same image at factory 0x10000 + ota_0 0x320000)',
        slotOffset='0x320000', slotSize='0x300000', factoryOffset='0x10000',
        appBytes=len(app), slotSafeBytes=SLOT_SAFE_BYTES,
        sha256=hashlib.sha256(app).hexdigest(),
        deviceTested=False,
        note=('Address-free app image; valid at any app partition offset. '
              'Never flash at 0x0 (bootloader). This device has NO official '
              'launcher in factory, so the same image is also written to '
              '0x10000 for direct boot; otadata stays all-0xFF.'))
    (dest / f'{label}_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'app.bin: {len(app)} B ({len(app)/SLOT_SAFE_BYTES*100:.1f}% of slot limit)')
    print(f'output : {dest / name}')
    return dest


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cli', default='arduino-cli')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--source', default=str(ROOT / 'boards' / 'sdgoods' / 'MotoNav'),
                    help='sketch directory (must contain partitions.csv)')
    args = ap.parse_args()
    if not 1 <= args.jobs <= 32:
        ap.error('--jobs must be between 1 and 32')
    source = Path(args.source)
    partitions = source / 'partitions.csv'
    if not partitions.exists():
        raise SystemExit(f'missing {partitions} - the sketch directory must carry the device partition table')

    cfg = source / 'src/core/FirmwareConfig.h'
    version = re.search(r'^#define FW_VERSION "v([^\"]+)"',
                        cfg.read_text(encoding='utf-8'), re.MULTILINE).group(1)
    build = ROOT / '.build' / 'sdgoods'
    output = build / 'output'
    output.mkdir(parents=True, exist_ok=True)

    home = str(Path.home())
    if sys.platform == 'win32':
        # Spaces in the Windows user profile path break -ffile-prefix-map;
        # fall back to the 8.3 short path.
        import ctypes
        buf = ctypes.create_unicode_buffer(300)
        if ctypes.windll.kernel32.GetShortPathNameW(home, buf, 300):
            home = buf.value.replace('\\', '/')
    prefix_map = '-ffile-prefix-map=' + home + '=/builder'

    subprocess.run([args.cli, 'compile', '--jobs', str(args.jobs),
                    '--libraries', str(ROOT / 'libraries'),
                    '--build-path', str(build / 'build'),
                    '--fqbn', FQBN,
                    '--build-property', 'compiler.c.extra_flags=' + prefix_map,
                    '--build-property', 'compiler.cpp.extra_flags=' + prefix_map,
                    '--build-property',
                    'compiler.c.elf.extra_flags=-Wl,--wrap=xTaskCreatePinnedToCore',
                    '--output-dir', str(output), str(source)], check=True)
    package(output, ROOT / 'dist' / 'sdgoods' / 'slot0', version)


if __name__ == '__main__':
    main()
