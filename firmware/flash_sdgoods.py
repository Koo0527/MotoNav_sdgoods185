#!/usr/bin/env python3
"""Install (or remove) the SDGOODS MotoNav port.

DRY RUN by default -- nothing is written unless --confirm is passed.

Device reality (COM7, checked 2026-11-10):
  The 0x10000 factory partition is labelled "launcher" in the official 2.0
  partition table, but the image actually flashed there is an older single-app
  MotoNav firmware (esp_app_desc project_name="MOTONAV", IDF v5.5.1), NOT the
  official launcher. otadata is all 0xFF, so the bootloader always boots factory.
  No prebuilt official launcher image ships with the open-source repo.
  => Flashing only ota_0 would leave the old firmware running. Use
     --direct-boot to also put the new image at 0x10000 so it actually boots.

Install (default) writes:
    0x008000  partition-table.bin   (16 dynamic-slot placeholders + ota_0 = 3MB)
    0xFF0000  pt_bak.bin            (backup kept in sync)
    0x320000  <app.bin>             (address-free application image)
  --direct-boot additionally writes:
    0x010000  <app.bin>             (same image, factory: device boots straight into it)

NEVER touched:
    0x000000  official bootloader
    0x310000  otadata (all 0xFF -> boot factory)
    0x1000000 appdata (high 16MB user data)

--restore writes the original table + the full 3MB factory dump back and erases
ota_0, returning the device byte-for-byte to its pre-flash state.

The image is address-free (esp_app_desc_t at +0x20), so the very same app.bin is
valid at 0x10000 and at 0x320000. Official pack_app.py confirms this.
"""
import argparse
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
BOARD = HERE / 'boards' / 'sdgoods'
BACKUP = HERE / 'backup'
def _find_esptool():
    # 优先本机 Arduino15 的 esptool，其次旧机器的硬编码路径
    for p in (
        Path.home() / 'AppData/Local/Arduino15/packages/esp32/tools/esptool_py/5.3.0/esptool.exe',
        Path('C:/Users/LENOVO/AppData/Local/Arduino15/packages/esp32/tools/esptool_py/5.3.0/esptool.exe'),
        Path('C:/Users/William Koo/AppData/Local/Arduino15/packages/esp32/'
             'tools/esptool_py/5.3.0/esptool.exe'),
    ):
        if p.exists():
            return p
    return Path('C:/Users/William Koo/AppData/Local/Arduino15/packages/esp32/'
                'tools/esptool_py/5.3.0/esptool.exe')


ESPTOOL = _find_esptool()
SLOT_OFFSET = 0x320000
SLOT_SIZE = 0x300000
FACTORY_OFFSET = 0x10000


def run(args, port, confirm):
    cmd = [str(ESPTOOL), '--port', port, '--baud', '460800'] + args
    print('  $', ' '.join(cmd))
    if confirm:
        subprocess.run(cmd, check=True)
    else:
        print('  (dry run - pass --confirm to actually write)')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', default='COM7')
    ap.add_argument('--app', help='address-free MotoNav app.bin')
    ap.add_argument('--restore', action='store_true', help='restore pre-flash device state')
    ap.add_argument('--direct-boot', action='store_true',
                    help='also flash app.bin to factory 0x10000 (no launcher on this device)')
    ap.add_argument('--factory-backup',
                    default=str(BACKUP / 'COM7_factory_full_20261110.bin'),
                    help='full 3MB factory dump used by --restore')
    ap.add_argument('--confirm', action='store_true', help='actually write to the device')
    args = ap.parse_args()

    if not ESPTOOL.exists():
        raise SystemExit(f'esptool not found: {ESPTOOL}')

    if args.restore:
        orig = BACKUP / 'COM7_partition_table.bin'
        if not orig.exists():
            raise SystemExit(f'missing original table dump: {orig}')
        factory = Path(args.factory_backup)
        print(f'RESTORE {args.port}: original table + factory image + erase ota_0')
        run(['write_flash', '0x8000', str(orig), '0xff0000', str(orig)], args.port, args.confirm)
        if factory.exists():
            if factory.stat().st_size != SLOT_SIZE:
                raise SystemExit(f'factory dump size mismatch: {factory.stat().st_size}')
            run(['write_flash', hex(FACTORY_OFFSET), str(factory)], args.port, args.confirm)
        else:
            print(f'  WARNING: no full factory dump at {factory}; old firmware NOT restored')
        run(['erase_region', hex(SLOT_OFFSET), hex(SLOT_SIZE)], args.port, args.confirm)
        print('Done. Device boots the original factory firmware again.')
        return

    app = Path(args.app) if args.app else None
    if not app or not app.exists():
        raise SystemExit('pass --app <path to MotoNav_sdgoods_*_app.bin>')
    if app.read_bytes()[:1] != b'\xe9':
        raise SystemExit('not an ESP application image (first byte must be 0xE9)')
    if app.stat().st_size > 3040870:
        raise SystemExit(f'app too large for a SDGOODS slot: {app.stat().st_size} > 3040870')

    table = BOARD / 'partition-table.bin'
    ptbak = BOARD / 'pt_bak.bin'
    for f in (table, ptbak):
        if not f.exists():
            raise SystemExit(f'missing {f} - run make_partition_table.py first')

    flash = ['write_flash', '0x8000', str(table), '0xff0000', str(ptbak),
             hex(SLOT_OFFSET), str(app)]
    if args.direct_boot:
        flash += [hex(FACTORY_OFFSET), str(app)]
        print(f'INSTALL {args.port}: ota_0 + factory (direct boot, no launcher present)')
    else:
        print(f'INSTALL {args.port}: ota_0 only (launcher-managed device)')
    print('  bootloader / otadata / appdata are NOT touched')
    run(flash, args.port, args.confirm)
    if args.direct_boot:
        print('After reboot: MotoNav runs directly from factory (see FW_VERSION).')
        print('  No launcher present: serial-L return is disabled in firmware.')
        print('  Power key: hold 3s = off/on; tap = standby screen (idle) / map style (nav & cruise).')
        print('  Restore: flash_sdgoods.py --restore --confirm')
    else:
        print('After reboot: launcher menu -> MotoNav. Long-press power key 3s to return.')


if __name__ == '__main__':
    main()
