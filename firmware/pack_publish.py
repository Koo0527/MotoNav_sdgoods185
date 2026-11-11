#!/usr/bin/env python3
"""把 MotoNav 编译产物打成「谷仓 SDGOODS 开放平台」发布用的两种包。

产物（默认 dist/publish/v<版本>/）：

  1. MotoNav_sdgoods_v<版本>_app.bin        单应用包 —— 纯应用镜像，不带地址
                                            · 写 0x10000（出厂区 / 单应用模式）或任意 ota_N 槽
                                            · 这是**要上传到平台的那一个**
  2. MotoNav_sdgoods_v<版本>_full_0x0.bin   整包 —— 带地址，从 0x0 起：
                                            bootloader@0x0 + 分区表@0x8000
                                            + 空 otadata@0x310000（擦除态，保证从出厂区启动）
                                            + app@0x10000
                                            · 仅用于整机烧录 / 官方通道，开发者账号上传会被 403

  3. MotoNav_sdgoods_v<版本>_manifest.json  两者的 sha256、大小、烧录地址、校验结果

为什么要改镜像身份（重要）
--------------------------
Arduino 构建的 app，esp_app_desc_t 是 arduino-esp32 **预编译内核**里那份，
project_name 恒为 "arduino-lib-builder"、version 是内核的 git 短哈希。
谷仓平台把 project_name 当 **app_id**（appdata/<app_id>/ 目录名、卸载按它删），
不改的话所有 Arduino 构建的 app 会共用同一个数据目录、互相删数据。
所以这里把 desc 里的 project_name/version 就地改写，并**重算镜像尾部的
checksum 与 SHA-256**（bootloader 会校验这两样，不重算就刷不进去）。

用法：
    python pack_publish.py                      # 用 .build/sdgoods/output 下的产物
    python pack_publish.py --no-desc-patch      # 保留内核原始身份（用于对比/排查）
    python pack_publish.py --project MotoNav --version v7.28.3
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DEFAULT_OUT = ROOT / '.build' / 'sdgoods' / 'output'

APP_MAGIC = 0xE9
DESC_MAGIC = 0xABCD5432
DESC_OFF = 0x20          # sizeof(esp_image_header)+sizeof(esp_image_segment_header)
OFF_VERSION = 16         # esp_app_desc_t.version   （相对 desc 起点）
OFF_PROJECT = 48         # esp_app_desc_t.project_name
STR_LEN = 32

BOOTLOADER_ADDR = 0x0
PARTTABLE_ADDR = 0x8000
OTADATA_ADDR = 0x310000
OTADATA_SIZE = 0x2000
APP_ADDR = 0x10000


def _cstr(buf, off, n):
    raw = buf[off:off + n].split(b'\x00', 1)[0]
    return raw.decode('utf-8', 'replace')


def read_desc(app):
    if len(app) < DESC_OFF + 256 or app[0] != APP_MAGIC:
        raise SystemExit('not an ESP32 application image')
    if struct.unpack_from('<I', app, DESC_OFF)[0] != DESC_MAGIC:
        raise SystemExit('esp_app_desc magic missing (not an app image)')
    return dict(
        version=_cstr(app, DESC_OFF + OFF_VERSION, STR_LEN),
        project=_cstr(app, DESC_OFF + OFF_PROJECT, STR_LEN),
        chip_id=struct.unpack_from('<H', app, 0x0C)[0],
    )


def image_checksum(app):
    """镜像尾部 1 字节：所有段数据的异或，初值 0xEF。"""
    off = 24
    ck = 0xEF
    for _ in range(app[1]):
        _addr, ln = struct.unpack_from('<II', app, off)
        data = app[off + 8:off + 8 + ln]
        for byte in data:
            ck ^= byte
        off += 8 + ln
    return ck


def reseal(app):
    """重算 checksum（倒数第 33 字节）与 SHA-256（末 32 字节），返回新镜像。"""
    out = bytearray(app)
    out[len(out) - 33] = image_checksum(out)
    out[len(out) - 32:] = hashlib.sha256(bytes(out[:len(out) - 32])).digest()
    return bytes(out)


def patch_desc(app, project, version):
    out = bytearray(app)
    old = read_desc(app)
    for off, val in ((OFF_PROJECT, project), (OFF_VERSION, version)):
        raw = val.encode('utf-8')
        if len(raw) >= STR_LEN:
            raise SystemExit(f'{val!r} too long, esp_app_desc field is {STR_LEN} bytes')
        base = DESC_OFF + off
        out[base:base + STR_LEN] = raw + b'\x00' * (STR_LEN - len(raw))
    return reseal(out), old


def build_full(app, boot, part, include_otadata=True):
    """整包：bootloader@0x0 + 分区表@0x8000 + (空 otadata@0x310000) + app@0x10000。"""
    end = APP_ADDR + len(app)
    if include_otadata:
        end = OTADATA_ADDR + OTADATA_SIZE
    img = bytearray(b'\xFF' * end)
    img[BOOTLOADER_ADDR:BOOTLOADER_ADDR + len(boot)] = boot
    img[PARTTABLE_ADDR:PARTTABLE_ADDR + len(part)] = part
    img[APP_ADDR:APP_ADDR + len(app)] = app
    if include_otadata:
        img[OTADATA_ADDR:OTADATA_ADDR + OTADATA_SIZE] = b'\xFF' * OTADATA_SIZE
    return bytes(img)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-b', '--build-dir', default=str(DEFAULT_OUT))
    ap.add_argument('-o', '--outdir', default=None, help='默认 dist/publish/v<版本>')
    ap.add_argument('--project', default='MotoNav', help='写进 esp_app_desc.project_name（= app_id）')
    ap.add_argument('--version', default=None, help='默认取 FirmwareConfig.h 的 FW_VERSION')
    ap.add_argument('--no-desc-patch', action='store_true', help='不改身份，原样打包')
    ap.add_argument('--no-otadata', action='store_true', help='整包不带 0x310000 的空 otadata')
    args = ap.parse_args()

    build = Path(args.build_dir)
    app_src = build / 'MotoNav.ino.bin'
    boot_src = build / 'MotoNav.ino.bootloader.bin'
    part_src = build / 'MotoNav.ino.partitions.bin'
    for p in (app_src, boot_src, part_src):
        if not p.exists():
            raise SystemExit(f'missing build artifact: {p}')

    version = args.version
    if not version:
        import re
        cfg = ROOT / 'boards' / 'sdgoods' / 'MotoNav' / 'src' / 'core' / 'FirmwareConfig.h'
        version = re.search(r'^#define FW_VERSION "v([^"]+)"',
                            cfg.read_text(encoding='utf-8'), re.MULTILINE).group(1)
    tag = f'MotoNav_sdgoods_v{version}'

    app_raw = app_src.read_bytes()
    desc_before = read_desc(app_raw)
    if args.no_desc_patch:
        app = app_raw
        desc_after = desc_before
    else:
        app, _ = patch_desc(app_raw, args.project, f'v{version}')
        desc_after = read_desc(app)
        if app[-32:] != hashlib.sha256(app[:-32]).digest():
            raise SystemExit('reseal failed: hash mismatch')

    boot = boot_src.read_bytes()
    part = part_src.read_bytes()
    full = build_full(app, boot, part, include_otadata=not args.no_otadata)

    outdir = Path(args.outdir) if args.outdir else ROOT / 'dist' / 'publish' / f'v{version}'
    outdir.mkdir(parents=True, exist_ok=True)
    app_out = outdir / f'{tag}_app.bin'
    full_out = outdir / f'{tag}_full_0x0.bin'
    app_out.write_bytes(app)
    full_out.write_bytes(full)

    # 整包里 app 段必须与单应用包逐字节一致
    assert full[APP_ADDR:APP_ADDR + len(app)] == app

    manifest = {
        'name': 'MotoNav',
        'firmware': version,
        'board': 'SDGOODS ESP32-S3 360x360（谷仓次元屏）',
        'chip': 'esp32s3',
        'platform': 'sdgoods',
        'appId': desc_after['project'],
        'descVersion': desc_after['version'],
        'descPatched': not args.no_desc_patch,
        'descBefore': {'project': desc_before['project'], 'version': desc_before['version']},
        'images': {
            'single_app': {
                'file': app_out.name,
                'bytes': len(app),
                'sha256': hashlib.sha256(app).hexdigest(),
                'flashAddress': '0x10000（出厂区/单应用）或任意 ota_N 槽',
                'addressFree': True,
                'uploadToPlatform': True,
                'flash': f'esptool --chip esp32s3 -p <PORT> write-flash 0x10000 {app_out.name}',
            },
            'full_0x0': {
                'file': full_out.name,
                'bytes': len(full),
                'sha256': hashlib.sha256(full).hexdigest(),
                'flashAddress': '0x0',
                'addressFree': False,
                'uploadToPlatform': False,
                'segments': [
                    {'part': 'bootloader', 'address': '0x0', 'bytes': len(boot)},
                    {'part': 'partition-table', 'address': '0x8000', 'bytes': len(part)},
                    {'part': 'otadata(blank)', 'address': '0x310000', 'bytes': 0 if args.no_otadata else OTADATA_SIZE},
                    {'part': 'app', 'address': '0x10000', 'bytes': len(app)},
                ],
                'flash': f'esptool --chip esp32s3 -p <PORT> write-flash 0x0 {full_out.name}',
            },
        },
        'note': ('整包带地址、从 0x0 起写，会覆盖 bootloader/分区表/出厂区，'
                 '仅用于整机烧录或官方通道上传；开发者账号上传平台请用 single_app。'),
    }
    (outdir / f'{tag}_manifest.json').write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    print(f'app_id      : {desc_after["project"]}  (was {desc_before["project"]})')
    print(f'desc version: {desc_after["version"]}  (was {desc_before["version"]})')
    print(f'single app  : {len(app):,} B  sha256 {manifest["images"]["single_app"]["sha256"][:16]}…')
    print(f'full 0x0    : {len(full):,} B  sha256 {manifest["images"]["full_0x0"]["sha256"][:16]}…')
    print(f'outdir      : {outdir}')


if __name__ == '__main__':
    sys.exit(main())
