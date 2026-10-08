#!/usr/bin/env python3
"""
make_map.py - build the radar's map for any receiver position.

Writes map.txt with up to four optional layers, each kept within --radius
nautical miles of the receiver:

  shorelines  coastlines, lakes and lake islands  (GSHHG, LGPL)
  rivers      major rivers                        (GSHHG/WDBII, LGPL)
  airports    code and elevation                  (OurAirports, public domain)
  navaids     VORs and NDBs                       (OurAirports, public domain)
  peaks       name and elevation                  (GeoNames, CC BY 4.0)
  towns       name                                (GeoNames, CC BY 4.0)

A layer is skipped, with a note, if its data isn't there. The easy way is
--download, which fetches whatever is missing (about 130 MB the first time,
nothing after that):

    ./make_map.py --download

or fetch the files yourself, from the folder holding make_map.py:

  Shorelines (about 95 MB) and rivers (about 19 MB):
    mkdir -p gshhs
    B=https://raw.githubusercontent.com/matplotlib/basemap/develop/data/basemap_data_hires/src/mpl_toolkits/basemap_data
    wget -P gshhs $B/gshhs_f.dat $B/gshhsmeta_f.dat
    wget -P gshhs $B/rivers_f.dat $B/riversmeta_f.dat

  Airports and navaids (about 14 MB):
    mkdir -p ourairports
    O=https://davidmegginson.github.io/ourairports-data
    wget -P ourairports $O/airports.csv $O/navaids.csv

  Towns (optional; cities1000 has places of 1000+ people, cities5000 5000+):
    wget https://download.geonames.org/export/dump/cities1000.zip
    unzip cities1000.zip                    # gives cities1000.txt
    then add:  --towns cities1000.txt

  Peaks (optional; a GeoNames country file, e.g. CA for Canada, US):
    wget https://download.geonames.org/export/dump/CA.zip
    unzip CA.zip                            # gives CA.txt
    then add:  --peaks CA.txt               (several files: --peaks CA.txt US.txt)

Usage:
    ./make_map.py                   # position from radar.conf, writes map.txt
    ./make_map.py --radius 60 --airports all --towns cities1000.txt
    ./make_map.py --peaks CA.txt US.txt --max-peaks 8
    ./make_map.py --lat 50.45 --lon -104.6 --radius 40 -o regina.txt

Only the Python standard library is needed.
"""

import argparse
import csv
import shutil
import tempfile
import urllib.request
import zipfile
import re
import math
import os
import sys
from array import array

LEVEL_NAMES = {1: "coast", 2: "lake", 3: "island", 5: "ice"}


def read_conf_position(path):
    lat = lon = None
    try:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if line.startswith("#") or "=" not in line:
                    continue
                key, val = (s.strip() for s in line.split("=", 1))
                if key == "lat":
                    lat = float(val)
                elif key == "lon":
                    lon = float(val)
    except OSError:
        pass
    return lat, lon


class Projection:
    """Flat-earth nautical miles east/north of the receiver, as in the app."""

    def __init__(self, lat0, lon0):
        self.lat0, self.lon0 = lat0, lon0
        self.coslat = math.cos(math.radians(lat0))

    def to_nm(self, lon, lat):
        # Bring the longitude within 180 degrees of the receiver
        dlon = (lon - self.lon0 + 180.0) % 360.0 - 180.0
        return dlon * 60.0 * self.coslat, (lat - self.lat0) * 60.0


def simplify(pts, tol):
    """Douglas-Peucker on a list of (x, y, lat, lon), iterative."""
    n = len(pts)
    if n < 3:
        return pts
    keep = bytearray(n)
    keep[0] = keep[-1] = 1
    stack = [(0, n - 1)]
    tol2 = tol * tol
    while stack:
        a, b = stack.pop()
        ax, ay = pts[a][0], pts[a][1]
        dx, dy = pts[b][0] - ax, pts[b][1] - ay
        seg2 = dx * dx + dy * dy
        best, best_d = -1, tol2
        for i in range(a + 1, b):
            px, py = pts[i][0] - ax, pts[i][1] - ay
            if seg2 > 0:
                t = (px * dx + py * dy) / seg2
                t = 0.0 if t < 0 else 1.0 if t > 1 else t
                ex, ey = px - t * dx, py - t * dy
            else:
                ex, ey = px, py
            d = ex * ex + ey * ey
            if d > best_d:
                best, best_d = i, d
        if best >= 0:
            keep[best] = 1
            stack.append((a, best))
            stack.append((best, b))
    return [p for p, k in zip(pts, keep) if k]


def read_lines(args, proj, name, levels, kind_of):
    """Read one of the GSHHG-format line files (gshhs or rivers), keeping
    the parts within the radius, simplified to screen resolution."""
    data = os.path.join(args.data, name + "_f.dat")
    meta = os.path.join(args.data, name + "meta_f.dat")
    label = "shorelines" if name == "gshhs" else name
    if not (os.path.exists(data) and os.path.exists(meta)):
        print("%s: no %s data in '%s', skipping" % (label, name, args.data))
        return []

    radius = args.radius
    margin = radius / 60.0 + 0.05                      # degrees of latitude

    out_lines = []      # (level, [(x, y, lat, lon), ...])
    with open(meta) as fm, open(data, "rb") as fd:
        for line in fm:
            p = line.split()
            level, npts = int(p[0]), int(p[2])
            south, north = float(p[3]), float(p[4])
            offset, nbytes = int(p[5]), int(p[6])
            if level not in levels or north < proj.lat0 - margin or south > proj.lat0 + margin:
                continue

            fd.seek(offset)
            vals = array("f")
            vals.frombytes(fd.read(nbytes))
            if sys.byteorder != "little":
                vals.byteswap()

            # Walk the shoreline, cutting it into runs that lie inside the
            # radius. One point either side is kept so lines reach the edge.
            run, prev = [], None
            for i in range(npts):
                lon, lat = vals[2 * i], vals[2 * i + 1]
                x, y = proj.to_nm(lon, lat)
                inside = x * x + y * y <= radius * radius
                pt = (x, y, lat, lon)
                if inside:
                    if not run and prev is not None:
                        run.append(prev)
                    run.append(pt)
                elif run:
                    run.append(pt)
                    out_lines.append((level, run))
                    run = []
                prev = pt
            if run:
                out_lines.append((level, run))

    result = []
    for level, run in out_lines:
        run = simplify(run, args.detail)
        if len(run) >= 2:
            result.append((kind_of(level), [(lat, lon) for _, _, lat, lon in run]))
    print("%s: %d lines, %d points" % (label, len(result), sum(len(r) for _, r in result)))
    return result


def read_shorelines(args, proj):
    levels = {1, 5} if args.no_lakes else {1, 2, 3, 5}
    return read_lines(args, proj, "gshhs", levels, lambda lv: LEVEL_NAMES.get(lv, "coast"))


def read_rivers(args, proj):
    if args.no_rivers:
        return []
    return read_lines(args, proj, "rivers", {-1}, lambda lv: "river")


# Airport types to include, most important first (drawn and labelled in this order)
AIRPORT_RANK = {"large_airport": 0, "medium_airport": 1, "small_airport": 3,
                "seaplane_base": 3, "heliport": 5}
PLACEHOLDER_IDENT = re.compile(r"^[A-Z]{2}-\d+$")     # OurAirports' own ids, e.g. CA-0811


def within(proj, radius, lat, lon):
    x, y = proj.to_nm(lon, lat)
    return x * x + y * y <= radius * radius, math.hypot(x, y)


AIRPORT_TYPE_TEXT = {"large_airport": "large airport", "medium_airport": "medium airport",
                     "small_airport": "small airport", "seaplane_base": "seaplane base",
                     "heliport": "heliport"}
SURFACE_TEXT = {"ASP": "asphalt", "CON": "concrete", "GRS": "grass", "GRV": "gravel",
                "TURF": "turf", "WATER": "water", "DIRT": "dirt", "PEM": "paved"}
# Frequencies worth showing, in this order
FREQ_TYPES = ["ATIS", "TWR", "GND", "CLD", "APP", "ARR", "DEP", "MF", "CTAF", "UNIC",
              "AFIS", "A/G", "FSS", "RCO"]


def mhz(v):
    t = ("%.3f" % float(v)).rstrip("0")             # 118.800 -> 118.8
    return t + "0" if t.endswith(".") else t


def airport_details(r, runways, freqs):
    lines = [r["name"]]
    where = ", ".join(x for x in (r["municipality"], AIRPORT_TYPE_TEXT.get(r["type"], "")) if x)
    if r["iata_code"]:
        where += "  (%s)" % r["iata_code"]
    lines.append(where)
    if r["elevation_ft"]:
        lines.append("Elevation %d ft" % round(float(r["elevation_ft"])))
    for rw in runways[:4]:
        ends = "/".join(e for e in (rw["le_ident"], rw["he_ident"]) if e)
        surface = SURFACE_TEXT.get(rw["surface"].upper(), rw["surface"].lower())
        length = "{:,} ft".format(int(float(rw["length_ft"]))) if rw["length_ft"] else ""
        lines.append(" ".join(x for x in ("Runway " + ends, length, surface) if x.strip()))
    by_type = {}
    for fr in freqs:
        t = fr["type"].upper()
        if t in FREQ_TYPES and fr["frequency_mhz"] and 108 <= float(fr["frequency_mhz"]) <= 137:
            f = mhz(fr["frequency_mhz"])
            by_type.setdefault(t, [])
            if f not in by_type[t]:
                by_type[t].append(f)
    parts = ["%s %s" % (t, " ".join(by_type[t])) for t in FREQ_TYPES if t in by_type]
    while parts:                        # two or three per line
        lines.append("   ".join(parts[:3]))
        parts = parts[3:]
    return lines


def read_airports(args, proj):
    path = os.path.join(args.airports_data, "airports.csv")
    if not os.path.exists(path):
        print("airports: no %s, skipping" % path)
        return []
    types = dict(AIRPORT_RANK)
    if not args.heliports:
        del types["heliport"]
    if args.airports == "major":
        del types["small_airport"]
        del types["seaplane_base"]

    chosen = []
    with open(path, encoding="utf-8", newline="") as f:
        for r in csv.DictReader(f):
            if r["type"] not in types or PLACEHOLDER_IDENT.match(r["ident"]):
                continue
            # "coded": smaller fields only if they have an ICAO code (e.g. CYWH)
            if (args.airports == "coded" and r["type"] in ("small_airport", "seaplane_base", "heliport")
                    and not r["icao_code"]):
                continue
            lat, lon = float(r["latitude_deg"]), float(r["longitude_deg"])
            ok, dist = within(proj, args.radius, lat, lon)
            if ok:
                chosen.append((r, lat, lon, dist))

    # Runways and frequencies for just these airports (optional files)
    idents = {r["ident"] for r, *_ in chosen}
    runways, freqs = {}, {}
    for name, store in (("runways.csv", runways), ("airport-frequencies.csv", freqs)):
        p = os.path.join(args.airports_data, name)
        if not os.path.exists(p):
            continue
        with open(p, encoding="utf-8", newline="") as f:
            for row in csv.DictReader(f):
                if row["airport_ident"] in idents and row.get("closed", "0") != "1":
                    store.setdefault(row["airport_ident"], []).append(row)
    for rws in runways.values():
        rws.sort(key=lambda rw: -float(rw["length_ft"] or 0))

    points = []
    for r, lat, lon, dist in chosen:
        label = r["ident"]
        if r["elevation_ft"]:
            label += " %d'" % round(float(r["elevation_ft"]))
        details = airport_details(r, runways.get(r["ident"], []), freqs.get(r["ident"], []))
        points.append((types[r["type"]], dist, "airport", lat, lon, label, details))
    print("airports: %d" % len(points))
    return points


def read_navaids(args, proj):
    path = os.path.join(args.airports_data, "navaids.csv")
    if not os.path.exists(path):
        print("navaids: no %s, skipping" % path)
        return []
    points = []
    with open(path, encoding="utf-8", newline="") as f:
        for r in csv.DictReader(f):
            t = r["type"]
            if t.startswith("VOR") or t == "TACAN":
                kind, rank = "vor", 2
            elif t.startswith("NDB"):
                kind, rank = "ndb", 4
            else:
                continue                                # DME-only and the like
            lat, lon = float(r["latitude_deg"]), float(r["longitude_deg"])
            ok, dist = within(proj, args.radius, lat, lon)
            if ok:
                khz = float(r["frequency_khz"] or 0)
                freq = ("%.2f MHz" % (khz / 1000)) if kind == "vor" else ("%d kHz" % khz)
                line = "%s  %s" % (t, freq)
                if r["dme_channel"]:
                    line += ", DME channel %s" % r["dme_channel"].lstrip("0")
                details = [r["name"], line]
                if r["elevation_ft"]:
                    details.append("Elevation %d ft" % round(float(r["elevation_ft"])))
                if r["magnetic_variation_deg"]:
                    v = float(r["magnetic_variation_deg"])
                    details.append("Magnetic variation %.0f° %s" % (abs(v), "E" if v >= 0 else "W"))
                if r["associated_airport"]:
                    details.append("Serves %s" % r["associated_airport"])
                points.append((rank, dist, kind, lat, lon, r["ident"], details))
    print("navaids: %d" % len(points))
    return points


def read_towns(args, proj):
    """GeoNames citiesNNNN.txt: tab separated, name in column 2, latitude 5,
    longitude 6, population 15 (counting from 1)."""
    if not args.towns:
        return []
    if not os.path.exists(args.towns):
        print("towns: no %s, skipping" % args.towns)
        return []
    points = []
    with open(args.towns, encoding="utf-8") as f:
        for line in f:
            c = line.rstrip("\n").split("\t")
            if len(c) < 15:
                continue
            pop = int(c[14] or 0)
            if pop < args.min_population:
                continue
            lat, lon = float(c[4]), float(c[5])
            ok, dist = within(proj, args.radius, lat, lon)
            if ok:
                # Bigger places first; rank 6+ keeps towns after aviation points
                points.append((6, -pop, "town", lat, lon, c[1],
                               [c[1], "Population {:,}".format(pop)]))
    print("towns: %d" % len(points))
    return points


def read_peaks(args, proj):
    """Peaks (feature code PK) from GeoNames country files: name in column 2,
    latitude 5, longitude 6, feature code 8, elevation 16, SRTM elevation 17
    (counting from 1). Keeps the highest --max-peaks within the radius."""
    peaks = []
    for path in args.peaks or []:
        if not os.path.exists(path):
            print("peaks: no %s, skipping" % path)
            continue
        with open(path, encoding="utf-8") as f:
            for line in f:
                c = line.rstrip("\n").split("\t")
                if len(c) < 17 or c[7] != "PK":
                    continue
                lat, lon = float(c[4]), float(c[5])
                ok, _ = within(proj, args.radius, lat, lon)
                if not ok:
                    continue
                metres = int(c[15]) if c[15].lstrip("-").isdigit() else None
                if metres is None and c[16].lstrip("-").isdigit() and int(c[16]) > -9999:
                    metres = int(c[16])
                if metres is None:
                    continue
                name = c[1]
                if name.startswith("Mount "):
                    name = "Mt " + name[6:]
                peaks.append((metres, lat, lon, name))
    peaks.sort(reverse=True)
    points = [(5, -m, "peak", lat, lon, "%s %d'" % (name, round(m * 3.28084)),
               [name, "Elevation {:,} ft ({:,} m)".format(round(m * 3.28084), m)])
              for m, lat, lon, name in peaks[:args.max_peaks]]
    if args.peaks:
        print("peaks: %d (highest of %d)" % (len(points), len(peaks)))
    return points


GSHHS_URL = ("https://raw.githubusercontent.com/matplotlib/basemap/develop/data/"
             "basemap_data_hires/src/mpl_toolkits/basemap_data/")
OURAIRPORTS_URL = "https://davidmegginson.github.io/ourairports-data/"
GEONAMES_URL = "https://download.geonames.org/export/dump/"


def fetch(url, dest):
    """Download url to dest unless it's already there."""
    if os.path.exists(dest):
        return True
    os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
    print("downloading %s ..." % url, flush=True)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "radar-make-map/1.0"})
        with urllib.request.urlopen(req, timeout=60) as r, \
                tempfile.NamedTemporaryFile(dir=os.path.dirname(dest) or ".", delete=False) as tmp:
            shutil.copyfileobj(r, tmp, 1 << 20)
        os.replace(tmp.name, dest)
        return True
    except Exception as e:                          # keep going without this layer
        print("  failed: %s" % e)
        try:
            os.unlink(tmp.name)
        except Exception:
            pass
        return False


def fetch_geonames(path):
    """GeoNames files come zipped: CA.txt is in CA.zip, cities1000.txt in cities1000.zip."""
    if os.path.exists(path):
        return
    name = os.path.basename(path)
    zpath = os.path.join(os.path.dirname(path) or ".", name[:-4] + ".zip")
    if fetch(GEONAMES_URL + os.path.basename(zpath), zpath):
        with zipfile.ZipFile(zpath) as z:
            z.extract(name, os.path.dirname(path) or ".")
        os.unlink(zpath)


def download(args):
    for f in ("gshhs_f.dat", "gshhsmeta_f.dat"):
        fetch(GSHHS_URL + f, os.path.join(args.data, f))
    if not args.no_rivers:
        for f in ("rivers_f.dat", "riversmeta_f.dat"):
            fetch(GSHHS_URL + f, os.path.join(args.data, f))
    for f in ("airports.csv", "navaids.csv", "runways.csv", "airport-frequencies.csv"):
        fetch(OURAIRPORTS_URL + f, os.path.join(args.airports_data, f))
    for p in (args.peaks or []) + ([args.towns] if args.towns else []):
        if p.endswith(".txt"):
            fetch_geonames(p)


def main():
    ap = argparse.ArgumentParser(description="Build map.txt for the radar display.")
    ap.add_argument("--conf", default="radar.conf", help="read lat/lon from this file")
    ap.add_argument("--lat", type=float, help="receiver latitude (overrides radar.conf)")
    ap.add_argument("--lon", type=float, help="receiver longitude (overrides radar.conf)")
    ap.add_argument("--radius", type=float, default=50.0,
                    help="nautical miles of map to keep (default 50); cover your largest range_nm")
    ap.add_argument("--detail", type=float, default=0.02,
                    help="shoreline simplification in nm (default 0.02, about 37 m)")
    ap.add_argument("--data", default="gshhs", help="folder holding gshhs_f.dat and gshhsmeta_f.dat")
    ap.add_argument("--no-lakes", action="store_true", help="coastlines only, no lakes")
    ap.add_argument("--no-rivers", action="store_true", help="leave out rivers")
    ap.add_argument("--airports-data", default="ourairports",
                    help="folder holding OurAirports airports.csv and navaids.csv")
    ap.add_argument("--airports", choices=("none", "major", "coded", "all"), default="coded",
                    help="major: large and medium airports; coded (default): also smaller "
                         "fields with an ICAO code; all: every small airport and seaplane base")
    ap.add_argument("--no-navaids", action="store_true", help="leave out VORs and NDBs")
    ap.add_argument("--heliports", action="store_true", help="include heliports")
    ap.add_argument("--towns", metavar="FILE", help="GeoNames citiesNNNN.txt to take towns from")
    ap.add_argument("--min-population", type=int, default=1000,
                    help="smallest town to include (default 1000)")
    ap.add_argument("--peaks", metavar="FILE", nargs="+",
                    help="GeoNames country file(s), e.g. CA.txt US.txt, to take peaks from")
    ap.add_argument("--max-peaks", type=int, default=10,
                    help="keep only this many of the highest peaks (default 10)")
    ap.add_argument("--download", action="store_true",
                    help="fetch any missing data files first")
    ap.add_argument("-o", "--output", default="map.txt")
    args = ap.parse_args()

    lat0, lon0 = read_conf_position(args.conf)
    if args.lat is not None:
        lat0 = args.lat
    if args.lon is not None:
        lon0 = args.lon
    if lat0 is None or lon0 is None:
        sys.exit("No position: give --lat and --lon, or a radar.conf with lat and lon.")

    if args.download:
        download(args)

    proj = Projection(lat0, lon0)
    lines = read_shorelines(args, proj) + read_rivers(args, proj)
    points = []
    if args.airports != "none":
        points += read_airports(args, proj)
    if not args.no_navaids:
        points += read_navaids(args, proj)
    points += read_peaks(args, proj)
    points += read_towns(args, proj)
    points.sort(key=lambda p: (p[0], p[1]))   # importance, then nearest/biggest first

    if not lines and not points and os.path.exists(args.output):
        sys.exit("Nothing found to draw, so %s was left as it was - check the data "
                 "folders (or the downloads above)." % args.output)

    tmp_out = args.output + ".new"
    with open(tmp_out, "w") as f:
        f.write("# radar map, made by make_map.py for %.5f %.5f\n" % (lat0, lon0))
        if lines:
            f.write("# shorelines and rivers: GSHHG (Wessel & Smith), LGPL\n")
        if any(p[2] != "town" for p in points):
            f.write("# airports and navaids: OurAirports, public domain\n")
        if any(p[2] in ("town", "peak") for p in points):
            f.write("# towns and peaks: GeoNames, CC BY 4.0\n")
        f.write("radius_nm %g\n" % args.radius)
        for kind, run in lines:
            f.write("> %s\n" % kind)
            for lat, lon in run:
                f.write("%.5f %.5f\n" % (lat, lon))
        # Points, most important first: "@ kind lat lon label|detail|detail..."
        for _, _, kind, lat, lon, label, details in points:
            clean = [d.replace("|", "/").strip() for d in [label] + details if d and d.strip()]
            f.write("@ %s %.5f %.5f %s\n" % (kind, lat, lon, "|".join(clean)))

    os.replace(tmp_out, args.output)            # the radar never sees half a file
    print("wrote %s (%g nm around %.4f, %.4f)" % (args.output, args.radius, lat0, lon0))
    if not lines and not points:
        print("Nothing to draw - check the data folders.")


if __name__ == "__main__":
    main()
