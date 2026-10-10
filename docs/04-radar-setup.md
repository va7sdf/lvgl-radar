---
title: Building the radar
---

# 4. Building the radar

With the touch demo working, the radar needs only a few more packages and
some settings.

## Build it

```
sudo apt install git cmake build-essential libcurl4-openssl-dev libcjson-dev
git clone --recurse-submodules https://github.com/va7sdf/lvgl-radar.git
cd lvgl-radar/radar
cmake -B build
cmake --build build -j4
```

`libcurl` fetches data from the web and `cJSON` reads it. Both are standard
Pi OS packages.

## Set your position and receiver

Open `radar.conf` in your editor (it's commented throughout). The first
things to set:

```ini
lat = 48.4220           # your receiver's position, decimal degrees
lon = -123.3700         # (this is Victoria's Inner Harbour: use your own)
range_nm = 10           # starting range: outer ring, nautical miles

[source]
name = local
url = http://192.168.1.10/data/aircraft.json    # your receiver
array_key = aircraft
poll_s = 1
colour = 48FF6A
```

Where `aircraft.json` lives depends on the receiver software. Common
places, with `PI` your receiver's address:

| Software | URL |
|---|---|
| dump1090-fa with SkyAware | `http://PI/skyaware/data/aircraft.json` |
| readsb / tar1090 | `http://PI/tar1090/data/aircraft.json` |

Open the URL in a browser: a page of JSON listing aircraft means it's
right.

The address can be IPv4 or IPv6. An IPv6 address goes in square brackets,
`url = http://[fd00::22]/data/aircraft.json`, or use a hostname: if it has
both kinds of address, the radar tries IPv6 first and falls back to IPv4.
The receiver's web server has to be listening on IPv6 for that to work. No receiver? Delete the `local` source and keep `adsb.fi` (chapter 9).

## Make the map

The outline map is made for your position by a Python script that
downloads free shoreline and airport data the first time:

```
./make_map.py --download
```

It writes `map.txt`, about 300 KB for 50 nm around Victoria; chapter 6
has the options. You can also run it later from the radar's
Settings screen with **Update map**.

## Run it

```
sudo ./build/radar radar.conf
```

The sweep starts at once; aircraft appear as their data arrives and the
sweep passes them. Ctrl-C stops it. When you're happy, make it start at
every boot:

```
./install-service.sh
```

That installs and starts a systemd service (chapter 11 explains it). From
then on, after changing code or settings:

```
cmake --build build -j4 && sudo systemctl restart radar
journalctl -u radar -f          # its output, live
```

## Updating

```
git pull
git submodule update            # in case LVGL moved
cmake --build build -j4 && sudo systemctl restart radar
```

Your edits to `radar.conf` survive a `git pull` unless the same lines
changed upstream; git will tell you if so.
