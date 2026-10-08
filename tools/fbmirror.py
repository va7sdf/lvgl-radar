#!/usr/bin/env python3
"""
fbmirror - show the Pi's framebuffer in a web browser.

Reads /dev/fb0 a few times a second and serves it as a live picture
(Motion JPEG), so a laptop on the same network can put the 5" screen on
a projector or share it over Jitsi. Works with anything drawing to the
framebuffer: the touch demo, the radar, or the text console.

  sudo apt install python3-pil      # once
  ./tools/fbmirror.py               # then browse to http://<pi>:8081/

Options: --fps 4 (frames per second), --size 1080 (picture size in
pixels), --port 8081, --fb /dev/fb0. Only changed frames are sent, so an
idle screen costs next to nothing.
"""

import argparse
import hashlib
import os
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO

try:
    from PIL import Image
except ImportError:
    sys.exit("fbmirror needs Pillow: sudo apt install python3-pil")

PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Pi screen</title>
<style>
  html, body { margin: 0; height: 100%; background: #000; }
  body { display: flex; align-items: center; justify-content: center; }
  img { max-width: 100vw; max-height: 100vh; aspect-ratio: 1 / 1; }
</style></head>
<body><img src="/stream" alt="Live view of the Pi's screen"></body></html>
"""


def fb_geometry(dev):
    """Width, height, bits per pixel and bytes per line, from sysfs."""
    name = os.path.basename(dev)
    sys_dir = "/sys/class/graphics/" + name

    def read(f):
        with open(os.path.join(sys_dir, f)) as h:
            return h.read().strip()
    w, h = (int(v) for v in read("virtual_size").split(","))
    bpp = int(read("bits_per_pixel"))
    stride = int(read("stride")) if os.path.exists(os.path.join(sys_dir, "stride")) else w * bpp // 8
    return w, h, bpp, stride


class Grabber:
    """Keeps the latest frame as JPEG; encodes only when the screen changed."""

    def __init__(self, a):
        self.dev, self.fps, self.size = a.fb, a.fps, a.size
        if a.geometry:
            w, h, bpp = (int(v) for v in a.geometry.split("x"))
            self.w, self.h, self.bpp, self.stride = w, h, bpp, w * bpp // 8
        else:
            self.w, self.h, self.bpp, self.stride = fb_geometry(self.dev)
        if self.bpp not in (16, 32):
            sys.exit("fbmirror: %d bits per pixel isn't supported" % self.bpp)
        self.jpeg, self.digest, self.seq = b"", None, 0
        self.cond = threading.Condition()

    def grab(self):
        with open(self.dev, "rb") as f:
            data = f.read(self.stride * self.h)
        digest = hashlib.blake2b(data, digest_size=16).digest()
        if digest == self.digest:
            return
        if self.bpp == 16:      # RGB565, as LVGL draws it with LV_COLOR_DEPTH 16
            im = Image.frombuffer("RGB", (self.w, self.h), data, "raw", "BGR;16", self.stride, 1)
        else:                   # XRGB8888
            im = Image.frombuffer("RGB", (self.w, self.h), data, "raw", "BGRX", self.stride, 1)
        if self.size and self.size != self.w:
            im = im.resize((self.size, self.size * self.h // self.w), Image.BILINEAR)
        out = BytesIO()
        im.save(out, "JPEG", quality=85)
        with self.cond:
            self.jpeg, self.digest = out.getvalue(), digest
            self.seq += 1
            self.cond.notify_all()

    def run(self):
        while True:
            t = time.monotonic()
            try:
                self.grab()
            except OSError as e:
                print("fbmirror: %s" % e, file=sys.stderr)
                time.sleep(2)
            time.sleep(max(0.0, 1.0 / self.fps - (time.monotonic() - t)))


def handler(g):
    class H(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/":
                body = PAGE.encode()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path == "/snapshot.jpg":
                with g.cond:
                    body = g.jpeg
                self.send_response(200)
                self.send_header("Content-Type", "image/jpeg")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path == "/stream":
                self.send_response(200)
                self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
                self.send_header("Cache-Control", "no-cache")
                self.end_headers()
                seen = -1
                try:
                    while True:
                        with g.cond:
                            # Resend now and then even if nothing changed, so a
                            # browser that dropped a frame catches up
                            g.cond.wait_for(lambda: g.seq != seen, timeout=5)
                            seen, jpeg = g.seq, g.jpeg
                        self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\n"
                                         b"Content-Length: %d\r\n\r\n" % len(jpeg))
                        self.wfile.write(jpeg + b"\r\n")
                        self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError):
                    pass                    # the viewer went away
            else:
                self.send_error(404)

        def log_message(self, *a):
            pass
    return H


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fb", default="/dev/fb0")
    ap.add_argument("--port", type=int, default=8081)
    ap.add_argument("--fps", type=float, default=4)
    ap.add_argument("--size", type=int, default=0, help="picture width in pixels (default: as is)")
    ap.add_argument("--geometry", help="WxHxBPP for a file instead of a device, e.g. 1080x1080x16")
    a = ap.parse_args()

    g = Grabber(a)
    threading.Thread(target=g.run, daemon=True).start()
    srv = ThreadingHTTPServer(("0.0.0.0", a.port), handler(g))
    srv.daemon_threads = True
    print("fbmirror: %s %dx%d %d-bit on http://%s:%d/" %
          (a.fb, g.w, g.h, g.bpp, socket.gethostname(), a.port), flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
