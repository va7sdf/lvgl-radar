#!/usr/bin/env python3
"""
Record your radar's live data at home, then replay it anywhere.

  record   Poll every [source] in radar.conf (plus receiver stats, weather,
           METARs and adsbdb lookups) and save them in a folder.
  conf     Write demo.conf: a copy of radar.conf that reads from the replay.
  replay   Serve a recording on http://127.0.0.1:8080 in real time, looping,
           so the radar works with no network at all.

Typical use:

  ./demo/demo.py record --minutes 90           # at home, at a busy time
  ./demo/demo.py conf                          # writes demo.conf
  ./demo/demo.py replay &                      # then...
  ./build/radar demo.conf

or ./demo/demo-mode on, to have the Pi start in demo mode at boot.
Standard library only.
"""

import argparse
import datetime
import gzip
import json
import math
import os
import re
import signal
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(HERE)
UA = "radar-display-demo/1.0"

DEFAULT_WEATHER = (
    "https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}"
    "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,"
    "wind_direction_10m,wind_gusts_10m,pressure_msl,is_day"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
    "&wind_speed_unit=kn&timezone=auto&forecast_days=4")
DEFAULT_LOOKUP = "https://api.adsbdb.com/v0"
DEFAULT_METAR = "https://aviationweather.gov/api/data/metar?ids={station}&format=json"

# ---------------------------------------------------------------- radar.conf


def strip_comment(val):
    """A # after whitespace starts a comment, as in config.c."""
    m = re.search(r"\s#", val)
    return val[:m.start()].rstrip() if m else val


def read_conf(path):
    g, sources, cur = {}, [], None
    with open(path) as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            if s == "[source]":
                cur = {}
                sources.append(cur)
                continue
            if "=" not in s:
                continue
            k, v = s.split("=", 1)
            k, v = k.strip(), strip_comment(v.strip())
            if cur is not None and k in ("name", "url", "array_key", "poll_s", "colour", "color"):
                cur[k] = v
            else:
                g.setdefault(k, v)
    return g, [s for s in sources if s.get("url")]


def widest_nm(g):
    w = float(g.get("range_nm", 10) or 10)
    try:
        lo, hi, st = (float(g.get(k, d) or d) for k, d in
                      (("range_min", 5), ("range_max", 25), ("range_step", 5)))
        r = lo
        while st > 0 and r <= hi + 1e-9:
            w = max(w, r)
            r += st
    except ValueError:
        pass
    return w


def expand(url, g):
    return (url.replace("{lat}", "%.4f" % float(g["lat"]))
               .replace("{lon}", "%.4f" % float(g["lon"]))
               .replace("{dist}", str(max(1, math.ceil(widest_nm(g) * 1.2)))))


def stats_url(g, sources):
    if g.get("stats_url"):
        return g["stats_url"]
    u = sources[0]["url"] if sources else ""
    return u[:-len("aircraft.json")] + "stats.json" if u.endswith("aircraft.json") else ""


def map_airports(g, conf_dir):
    """Four-letter airport codes in the map, for their METARs."""
    path = g.get("map_file", "map.txt")
    if not path:
        return []
    path = os.path.join(conf_dir, path)
    codes = []
    try:
        with open(path) as f:
            for line in f:
                p = line.split(None, 4)
                if len(p) == 5 and p[0] == "@" and p[1] == "airport":
                    code = p[4].split("|")[0].split()[0]
                    if re.fullmatch(r"[A-Z0-9]{4}", code):
                        codes.append(code)
    except OSError:
        pass
    return codes

# ---------------------------------------------------------------- record


def fetch(url, timeout=10):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read().decode("utf-8", "replace")


def airline_callsign(cs):
    return len(cs) >= 4 and cs[:3].isalpha() and cs[3].isdigit()


class Recorder:
    def __init__(self, args):
        self.conf_dir = os.path.dirname(os.path.abspath(args.conf))
        self.g, self.sources = read_conf(args.conf)
        if not self.sources:
            sys.exit("%s: no [source] with a url" % args.conf)
        self.out = args.out
        os.makedirs(self.out, exist_ok=True)
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.t0 = time.time()
        self.frames = gzip.open(os.path.join(self.out, "frames.jsonl.gz"), "wt", compresslevel=6)
        self.n_frames = 0
        self.extras = {"lookups": {}, "metar": {}, "weather": None}
        self.wanted = []                # lookups still to make
        self.asked = set()
        self.meta = {
            "created": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
            "sources": [{"name": s.get("name", "source %d" % i),
                         "array_key": s.get("array_key", "aircraft")}
                        for i, s in enumerate(self.sources)],
        }

    def frame(self, src, body):
        line = json.dumps({"t": round(time.time() - self.t0, 2), "src": src, "body": body})
        with self.lock:
            self.frames.write(line + "\n")
            self.n_frames += 1

    def note_aircraft(self, i, body):
        try:
            ac = json.loads(body).get(self.meta["sources"][i]["array_key"], [])
        except (ValueError, AttributeError):
            return 0
        with self.lock:
            for a in ac:
                hx = str(a.get("hex", "")).strip().lower().lstrip("~")
                cs = str(a.get("flight", "")).strip().upper()
                for key in ("aircraft/" + hx if hx else "",
                            "callsign/" + cs if airline_callsign(cs) else ""):
                    if key and key not in self.asked:
                        self.asked.add(key)
                        self.wanted.append(key)
        return len(ac)

    def poll_source(self, i):
        s = self.sources[i]
        url = expand(s["url"], self.g)
        every = max(1.0, float(s.get("poll_s", 1) or 1))
        while not self.stop.is_set():
            t = time.time()
            try:
                body = fetch(url)
                self.frame(i, body)
                self.counts[i] = self.note_aircraft(i, body)
            except Exception as e:                  # keep going through blips
                self.errors[i] = str(e)[:60]
            self.stop.wait(max(0.0, every - (time.time() - t)))

    def poll_stats(self, url):
        while not self.stop.is_set():
            try:
                self.frame("stats", fetch(url))
            except Exception:
                pass
            self.stop.wait(10)

    def poll_slow(self):
        """Weather and METARs now and then; adsbdb lookups, gently."""
        weather = expand(self.g.get("weather_url") or DEFAULT_WEATHER, self.g)
        metar_tmpl = self.g.get("metar_url") or DEFAULT_METAR
        stations = []
        for c in [self.g.get("metar_station", "")] + map_airports(self.g, self.conf_dir):
            if c and c not in stations:
                stations.append(c)
        base = (self.g.get("lookup_url") or DEFAULT_LOOKUP).rstrip("/")
        next_wx = 0
        while not self.stop.is_set():
            if time.time() >= next_wx:
                next_wx = time.time() + 900
                if weather and self.g.get("weather_url", None) != "":
                    try:
                        self.extras["weather"] = fetch(weather)
                    except Exception as e:
                        print("\nweather: %s" % e)
                if stations:
                    try:
                        body = fetch(metar_tmpl.replace("{station}", ",".join(stations)))
                        for m in json.loads(body):
                            sid = m.get("icaoId")
                            if sid:
                                self.extras["metar"][sid] = json.dumps([m])
                    except Exception as e:
                        print("\nmetar: %s" % e)
                self.save_extras()
            key = None
            with self.lock:
                if self.wanted:
                    key = self.wanted.pop(0)
            if key:
                try:
                    self.extras["lookups"][key] = fetch("%s/%s" % (base, key))
                except urllib.error.HTTPError:
                    pass                                # unknown: the replay says so too
                except Exception:
                    with self.lock:                     # try again later
                        self.wanted.append(key)
                self.stop.wait(1.0)
            else:
                self.stop.wait(2.0)

    def save_extras(self):
        with self.lock:
            data = json.dumps({"meta": self.meta, **self.extras})
        tmp = os.path.join(self.out, "extras.json.tmp")
        with open(tmp, "w") as f:
            f.write(data)
        os.replace(tmp, os.path.join(self.out, "extras.json"))

    def run(self, minutes):
        self.counts = [0] * len(self.sources)
        self.errors = [""] * len(self.sources)
        threads = [threading.Thread(target=self.poll_source, args=(i,), daemon=True)
                   for i in range(len(self.sources))]
        su = stats_url(self.g, self.sources)
        if su:
            threads.append(threading.Thread(target=self.poll_stats, args=(su,), daemon=True))
        threads.append(threading.Thread(target=self.poll_slow, daemon=True))
        for t in threads:
            t.start()
        signal.signal(signal.SIGTERM, lambda *a: self.stop.set())
        end = self.t0 + minutes * 60 if minutes else None
        print("Recording to %s/ - Ctrl-C to finish%s" %
              (self.out, " (or after %g minutes)" % minutes if minutes else ""))
        try:
            while not self.stop.is_set() and (end is None or time.time() < end):
                el = int(time.time() - self.t0)
                parts = ["%s %s" % (s["name"], self.errors[i] or self.counts[i])
                         for i, s in enumerate(self.meta["sources"])]
                print("\r%02d:%02d:%02d  %s  lookups %d  frames %d   " %
                      (el // 3600, el // 60 % 60, el % 60, "  ".join(parts),
                       len(self.extras["lookups"]), self.n_frames), end="", flush=True)
                if el and el % 60 == 0:
                    self.save_extras()
                    with self.lock:
                        self.frames.flush()
                self.stop.wait(1)
        except KeyboardInterrupt:
            pass
        self.stop.set()
        with self.lock:
            self.meta["seconds"] = round(time.time() - self.t0)
        self.save_extras()
        with self.lock:
            self.frames.close()
        print("\nSaved %d frames, %d minutes." % (self.n_frames, self.meta["seconds"] // 60))

# ---------------------------------------------------------------- replay


class Replay:
    def __init__(self, folder, gap, speed):
        self.folder = folder
        with open(os.path.join(folder, "extras.json")) as f:
            ex = json.load(f)
        self.meta = ex["meta"]
        self.lookups = {k.lower(): v for k, v in ex.get("lookups", {}).items()}
        self.metar = ex.get("metar", {})
        self.weather = ex.get("weather")
        self.gap, self.speed = gap, speed
        self.lock = threading.Lock()
        self.current = {}
        self.clear()
        self.loops = 0

    def empty(self, i):
        return json.dumps({"now": time.time(), self.meta["sources"][i]["array_key"]: []})

    def clear(self):
        with self.lock:
            for i in range(len(self.meta["sources"])):
                self.current[i] = self.empty(i)

    def play(self):
        """Feed frames at their recorded times; a short empty gap between
           loops lets the radar clear its tracks instead of jumping."""
        while True:
            start = time.time()
            with gzip.open(os.path.join(self.folder, "frames.jsonl.gz"), "rt") as f:
                try:
                    for line in f:
                        try:
                            fr = json.loads(line)
                        except ValueError:
                            break                       # cut short: loop from here
                        wait = start + fr["t"] / self.speed - time.time()
                        if wait > 0:
                            time.sleep(wait)
                        with self.lock:
                            self.current[fr["src"]] = fr["body"]
                except (EOFError, OSError, gzip.BadGzipFile):
                    pass                                # unfinished recording
            self.loops += 1
            self.clear()
            time.sleep(self.gap)

    def weather_now(self):
        """The recorded forecast, with its days moved to start today."""
        if not self.weather:
            return None
        try:
            w = json.loads(self.weather)
            days = w.get("daily", {}).get("time", [])
            today = datetime.date.today()
            w["daily"]["time"] = [(today + datetime.timedelta(days=i)).isoformat()
                                  for i in range(len(days))]
            return json.dumps(w)
        except (ValueError, AttributeError, KeyError):
            return self.weather


def make_handler(rp):
    class H(BaseHTTPRequestHandler):
        def send(self, code, body, ctype="application/json"):
            data = body.encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            u = urllib.parse.urlparse(self.path)
            p = u.path.rstrip("/")
            m = re.fullmatch(r"/src/(\d+)/aircraft\.json", p)
            if m and int(m.group(1)) in rp.current:
                with rp.lock:
                    return self.send(200, rp.current[int(m.group(1))])
            if p == "/stats.json":
                with rp.lock:
                    body = rp.current.get("stats")
                return self.send(200, body) if body else self.send(404, "{}")
            if p.startswith("/adsbdb/"):
                body = rp.lookups.get(p[len("/adsbdb/"):].lower())
                if body:
                    return self.send(200, body)
                kind = "unknown aircraft" if "/aircraft/" in p else "unknown callsign"
                return self.send(404, json.dumps({"response": kind}))
            if p == "/weather":
                body = rp.weather_now()
                return self.send(200, body) if body else self.send(404, "{}")
            if p == "/metar":
                ids = urllib.parse.parse_qs(u.query).get("ids", [""])[0].upper().split(",")
                out = []
                for i in ids:
                    if i in rp.metar:
                        out += json.loads(rp.metar[i])
                return self.send(200, json.dumps(out))
            if p in ("", "/status"):
                return self.send(200, "Replaying %s (loop %d)\n" % (rp.folder, rp.loops + 1),
                                 "text/plain")
            self.send(404, "{}")

        def log_message(self, *a):
            pass
    return H

# ---------------------------------------------------------------- demo.conf


def write_conf(args):
    g, sources = read_conf(args.conf)
    base = "http://127.0.0.1:%d" % args.port
    overrides = {
        "stats_url": base + "/stats.json",
        "lookup_url": base + "/adsbdb",
        "weather_url": base + "/weather",
        "metar_url": base + "/metar?ids={station}",
        "range_file": "demo-range.txt",        # keep the demo out of your real range plot
    }
    out, src_i, in_source, done = [], -1, False, set()
    with open(args.conf) as f:
        lines = f.read().splitlines()
    out.append("# demo.conf - made by demo/demo.py from %s; reads the replay on %s"
               % (os.path.basename(args.conf), base))
    for line in lines:
        s = line.strip()
        if s == "[source]":
            if not in_source:                   # globals not already in the file
                for k, v in overrides.items():
                    if k not in done:
                        out.append("%s = %s" % (k, v))
                out.append("")
            in_source = True
            src_i += 1
            out.append(line)
            continue
        m = re.match(r"\s*([A-Za-z_]+)\s*=", line)
        key = m.group(1) if m else None
        if in_source and key == "url":
            out.append("url = %s/src/%d/aircraft.json" % (base, src_i))
        elif not in_source and key in overrides:
            out.append("%s = %s" % (key, overrides[key]))
            done.add(key)
        else:
            out.append(line)
    with open(args.out, "w") as f:
        f.write("\n".join(out) + "\n")
    print("Wrote %s (%d sources from the replay on %s)" % (args.out, src_i + 1, base))

# ---------------------------------------------------------------- main


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("record", help="record live data")
    r.add_argument("--conf", default=os.path.join(TOP, "radar.conf"))
    r.add_argument("--out", default=os.path.join(TOP, "recording"))
    r.add_argument("--minutes", type=float, default=0, help="stop after this long (0 = Ctrl-C)")
    c = sub.add_parser("conf", help="write demo.conf")
    c.add_argument("--conf", default=os.path.join(TOP, "radar.conf"))
    c.add_argument("--out", default=os.path.join(TOP, "demo.conf"))
    c.add_argument("--port", type=int, default=8080)
    p = sub.add_parser("replay", help="serve a recording")
    p.add_argument("--rec", default=os.path.join(TOP, "recording"))
    p.add_argument("--port", type=int, default=8080)
    p.add_argument("--gap", type=float, default=12, help="seconds of empty sky between loops")
    p.add_argument("--speed", type=float, default=1.0, help="2 = twice as fast")
    a = ap.parse_args()

    if a.cmd == "record":
        Recorder(a).run(a.minutes)
    elif a.cmd == "conf":
        write_conf(a)
    else:
        rp = Replay(a.rec, a.gap, a.speed)
        threading.Thread(target=rp.play, daemon=True).start()
        srv = ThreadingHTTPServer(("127.0.0.1", a.port), make_handler(rp))
        secs = rp.meta.get("seconds", 0)
        print("Replaying %s (recorded %s, %d:%02d long) on http://127.0.0.1:%d" %
              (a.rec, rp.meta.get("created", "?"), secs // 60, secs % 60, a.port), flush=True)
        signal.signal(signal.SIGTERM, lambda *x: sys.exit(0))
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
