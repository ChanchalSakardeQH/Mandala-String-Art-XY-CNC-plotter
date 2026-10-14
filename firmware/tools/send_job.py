#!/usr/bin/env python3
"""
Upload a G-code job to the Mandala plotter and (optionally) start it.
Use it to test the machine before the Android app is ready.

  USB:    python send_job.py --port COM5 square-weave.gcode --run
  Wi-Fi:  python send_job.py --host 192.168.4.1 square-weave.gcode --run
  status: python send_job.py --host 192.168.4.1 --watch

USB needs pyserial:  pip install pyserial
"""
import argparse
import os
import socket
import sys
import time


class Link:
    def __init__(self, port=None, host=None):
        self.buf = b""
        if host:
            self.sock = socket.create_connection((host, 23), timeout=5)
            self.sock.settimeout(0.2)
            self.ser = None
        else:
            import serial  # pyserial
            self.ser = serial.Serial(port, 115200, timeout=0.2)
            self.sock = None
            time.sleep(2.0)  # board resets when the port opens
        self.drain(1.0)

    def write(self, data: bytes):
        (self.sock.sendall if self.sock else self.ser.write)(data)

    def _read(self) -> bytes:
        try:
            return self.sock.recv(4096) if self.sock else self.ser.read(4096)
        except socket.timeout:
            return b""

    def readline(self, timeout=10.0):
        end = time.time() + timeout
        while time.time() < end:
            if b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                return line.decode(errors="replace").strip()
            self.buf += self._read()
        return None

    def drain(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            line = self.readline(0.2)
            if line:
                print("  <", line)

    def command(self, cmd, timeout=10.0):
        """Send one line, wait for ok/error, print anything else."""
        self.write((cmd + "\n").encode())
        while True:
            line = self.readline(timeout)
            if line is None:
                raise TimeoutError(f"no answer to {cmd!r}")
            if line.startswith("ok"):
                return line
            if line.startswith("error"):
                raise RuntimeError(f"{cmd!r} -> {line}")
            if line:
                print("  <", line)


def upload(link, path, name):
    lines = [l.rstrip("\r\n") for l in open(path, encoding="utf-8")]
    lines = [l for l in lines if l.strip()]
    link.command(f"$UPLOAD={name}")
    t0 = time.time()
    for i, l in enumerate(lines, 1):
        link.command(l)  # firmware acks every line: simple and safe for Bluetooth too
        if i % 50 == 0 or i == len(lines):
            print(f"\r  uploaded {i}/{len(lines)} lines", end="", flush=True)
    res = link.command("$END")
    print(f"\n  {res}  ({time.time() - t0:.1f} s)")
    expected = sum(len(l) + 1 for l in lines)
    if f"lines={len(lines)}" not in res or f"bytes={expected}" not in res:
        raise RuntimeError("size check failed - upload again")


def watch(link):
    print("Watching status, Ctrl-C to quit. Keys are not sent from here.")
    try:
        while True:
            link.write(b"?")
            t = time.time() + 1.0
            while time.time() < t:
                line = link.readline(0.2)
                if line:
                    print(line)
    except KeyboardInterrupt:
        pass
    except OSError:
        print("connection closed")


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--port", help="serial port, e.g. COM5 or /dev/ttyUSB0")
    g.add_argument("--host", help="plotter IP, e.g. 192.168.4.1")
    ap.add_argument("file", nargs="?", help="G-code file to upload")
    ap.add_argument("--name", help="name on the plotter (default: file name)")
    ap.add_argument("--run", action="store_true", help="start the job after upload")
    ap.add_argument("--watch", action="store_true", help="print status every second")
    a = ap.parse_args()

    link = Link(a.port, a.host)
    link.command("$I")
    if a.file:
        name = a.name or os.path.basename(a.file)
        upload(link, a.file, name)
        if a.run:
            link.command(f"$RUN={name}")
            print(f"  started {name}")
    if a.watch or a.run:
        watch(link)


if __name__ == "__main__":
    sys.exit(main())
