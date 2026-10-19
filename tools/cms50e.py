#!/usr/bin/env python3
"""Reader of the Contec CMS50E pulse oximeter with USB HID interface (USB ID 28e9:028a), used as the reference of the
live evaluation (tools/live_eval.py).

On Linux, the oximeter appears as /dev/hidrawN with the generic HID driver: no other driver is needed, but the user
must be allowed to read and write the device (see find_device). Only the Python standard library is used (Python 3.5
or newer).

Protocol, as documented by the pulseoxdl project (https://codeberg.org/klimd/pulseoxdl): reports of 64 bytes without
report number in both directions; every command ends with a checksum, the sum of the preceding bytes in 7 bits.
  9B 00 1B          start the pulse wave data          9B 01 1C   start the measurements
  9A 1A             keep the live data going (every 5 s)  9B 7F 1A   stop the live data
The device then sends 20 reports per second, each with several frames that begin with EB:
  EB 00 s W B c     pulse wave sample (60 per second): amplitude W (0-127), bar B (0-15; 0x60 when no finger)
  EB 01 f P S i j c measurement (once per second): pulse rate P (+128 if f & 2), SpO2 S (%, 127 when no finger),
                    perfusion index i + 128 j (0x7F, 0x00 when not available)

Usage:
  python3 tools/cms50e.py [--device /dev/hidrawN] [--seconds 60] [--out FILE.csv]
prints the pulse rate and SpO2 every second, and writes them to FILE.csv with the monotonic time in microseconds.
"""
import argparse
import os
import select
import sys
import time

USB_ID = ('28E9', '028A')
START_WAVE = (0x9B, 0x00, 0x1B)
START_MEASUREMENTS = (0x9B, 0x01, 0x1C)
KEEP_ALIVE = (0x9A, 0x1A)
STOP = (0x9B, 0x7F, 0x1A)
NO_FINGER_BAR = 0x60
NO_FINGER_SPO2 = 0x7F


def find_device():
    """/dev/hidrawN of the oximeter, or None. If the device cannot be opened, a udev rule such as
    SUBSYSTEM=="hidraw", ATTRS{idVendor}=="28e9", ATTRS{idProduct}=="028a", MODE="0666"
    (in /etc/udev/rules.d/70-cms50e.rules), or 'sudo chmod 666 /dev/hidrawN', gives access to it."""
    base = '/sys/class/hidraw'
    for name in sorted(os.listdir(base)) if os.path.isdir(base) else []:
        try:
            with open(os.path.join(base, name, 'device', 'uevent')) as f:
                uevent = f.read().upper()
        except OSError:
            continue
        if 'HID_ID=0003:0000%s:0000%s' % USB_ID in uevent:
            return '/dev/' + name
    return None


class CMS50E(object):
    """Live data of the oximeter. read() returns the frames received since the previous call."""

    def __init__(self, device=None):
        self.device = device or find_device()
        if self.device is None:
            raise IOError('CMS50E oximeter (USB ID 28e9:028a) not found')
        self.fd = os.open(self.device, os.O_RDWR)
        self.last_keep_alive = 0.0
        self.checksum_errors = 0

    def send(self, command):
        os.write(self.fd, bytes([0]) + bytes(command) + bytes(64 - len(command)))   # report number 0, 64 bytes

    def start(self):
        self.send(START_WAVE)
        self.send(START_MEASUREMENTS)
        self.last_keep_alive = time.monotonic()

    def stop(self):
        try:
            self.send(STOP)
        except OSError:
            pass
        os.close(self.fd)

    def read(self, timeout=1.0):
        """Frames of the next report: ('wave', amplitude, bar, finger) or ('measurement', pulse rate, SpO2, finger),
        with the monotonic time of the report in microseconds; an empty list if nothing arrived."""
        if time.monotonic() - self.last_keep_alive >= 5:
            self.send(KEEP_ALIVE)
            self.last_keep_alive = time.monotonic()
        ready, _, _ = select.select([self.fd], [], [], timeout)
        if not ready:
            return []
        report = os.read(self.fd, 64)
        t = int(time.monotonic() * 1e6)
        frames, i = [], 0
        while i + 1 < len(report) and report[i] == 0xEB and report[i + 1] in (0, 1):
            n = 6 if report[i + 1] == 0 else 8
            f = report[i:i + n]
            i += n
            if len(f) < n or (sum(f[:-1]) & 0x7F) != f[-1]:
                self.checksum_errors += 1
                continue
            if f[1] == 0:
                frames.append((t, 'wave', f[3], f[4], int(f[4] != NO_FINGER_BAR)))
            else:
                pulse = f[3] + (0x80 if f[2] & 0x02 else 0)
                frames.append((t, 'measurement', pulse, f[4], int(f[4] != NO_FINGER_SPO2)))
        return frames


def main():
    parser = argparse.ArgumentParser(description='Live pulse rate and SpO2 of a CMS50E oximeter (USB HID)')
    parser.add_argument('--device', help='/dev/hidrawN (default: found by its USB ID)')
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--out', help='CSV file for the measurements')
    args = parser.parse_args()
    try:
        oximeter = CMS50E(args.device)
    except OSError as e:
        sys.exit('%s. Check that the oximeter is plugged in (lsusb shows "28e9:028a") and that its /dev/hidrawN '
                 'device can be read and written (see find_device in this file).' % e)
    print('Oximeter at %s' % oximeter.device)
    out = open(args.out, 'w') if args.out else None
    if out:
        out.write('t,pulse_rate,spo2,finger\n')
    oximeter.start()
    end = time.monotonic() + args.seconds
    try:
        while time.monotonic() < end:
            for t, kind, a, b, finger in oximeter.read():
                if kind == 'measurement':
                    print('pulse rate %3d bpm, SpO2 %3d %%%s' % (a, b, '' if finger else ' (no finger)'))
                    if out:
                        out.write('%d,%d,%d,%d\n' % (t, a, b, finger))
    except KeyboardInterrupt:
        pass
    finally:
        oximeter.stop()
        if out:
            out.close()
    if oximeter.checksum_errors:
        print('%d frames with checksum errors were ignored' % oximeter.checksum_errors, file=sys.stderr)


if __name__ == '__main__':
    main()
