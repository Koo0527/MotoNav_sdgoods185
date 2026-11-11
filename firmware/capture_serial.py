"""Capture ESP32-S3 serial boot log.

Opening the port with DTR/RTS held low avoids the automatic reset pulse that
otherwise fires on Windows when a COM port is opened.

Reset options:
  --reset            pulse RTS once after opening (works on classic USB-UART)
  --reset-esptool    run esptool's own --after hard_reset BEFORE opening the
                     port (reliable for ESP32-S3 USB-Serial-JTAG), then open
                     with a retry loop because the USB device re-enumerates.
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

import serial

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--port', default='COM5')
ap.add_argument('--baud', type=int, default=115200)
ap.add_argument('--duration', type=float, default=280.0)
ap.add_argument('--out', default='serial_capture.txt')
ap.add_argument('--reset', action='store_true', help='pulse RTS once after opening')
ap.add_argument('--reset-esptool', action='store_true',
                help='let esptool hard-reset the chip before opening the port')
ap.add_argument('--esptool', default=str(Path.home() / 'AppData/Local/Arduino15/'
                'packages/esp32/tools/esptool_py/5.3.0/esptool.exe'))
args = ap.parse_args()

if args.reset_esptool:
    cmd = [args.esptool, '--port', args.port, '--baud', '115200',
           '--after', 'hard_reset', 'chip_id']
    print('$', ' '.join(cmd), flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    print(r.stdout[-1200:], flush=True)
    if r.returncode:
        print(r.stderr[-1200:], file=sys.stderr, flush=True)
        raise SystemExit(f'esptool reset failed: {r.returncode}')


def open_port():
    s = serial.Serial()
    s.port = args.port
    s.baudrate = args.baud
    s.timeout = 0.5
    s.dtr = False
    s.rts = False
    s.open()
    return s


ser = None
deadline = time.time() + 8
while ser is None and time.time() < deadline:
    try:
        ser = open_port()
    except Exception:
        time.sleep(0.05)
if ser is None:
    raise SystemExit(f'cannot open {args.port}')
print(f'port {args.port} opened WITHOUT reset, capturing {args.duration}s ...', flush=True)

if args.reset:
    time.sleep(0.2)
    ser.setDTR(False)          # normal boot (do not enter download mode)
    ser.setRTS(True)           # assert reset
    time.sleep(0.2)
    ser.setRTS(False)          # release -> chip boots
    print('reset pulse sent (RTS)', flush=True)

start = time.time()
with open(args.out, 'w', encoding='utf-8') as f:
    while time.time() - start < args.duration:
        try:
            line = ser.readline()
        except Exception as e:
            print('read err:', e, flush=True)
            break
        if not line:
            continue
        s = line.decode('utf-8', 'replace').rstrip()
        ts = time.strftime('%H:%M:%S', time.localtime())
        f.write(f'{ts} {s}\n')
        f.flush()
        print(f'{ts} {s}', flush=True)
ser.close()
print('capture done.', flush=True)
