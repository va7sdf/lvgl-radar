# Radar

A radar-style display of the aircraft your own ADS-B receiver hears,
merged with the [adsb.fi](https://adsb.fi) open data feed, on a Waveshare
5" 1080x1080 round touch screen and a Raspberry Pi Zero 2 W. LVGL draws
straight to the framebuffer: no desktop.

- A sweep with phosphor glow; aircraft move only when the sweep passes them
- Fading tracks, colour by source, emergencies in red
- An outline map (shorelines, rivers, airports, navaids) made for your location
- Tap an aircraft for its details, a map point for airport info and METAR
- Swipe for the clock and weather, receiver stats, an aircraft list and settings

## Quick start

```
sudo apt install git cmake build-essential libcurl4-openssl-dev libcjson-dev
git clone --recurse-submodules https://github.com/va7sdf/lvgl-radar.git
cd lvgl-radar/radar
cmake -B build && cmake --build build -j4
vi radar.conf                         # your position and receiver
./make_map.py --download              # the outline map for your position
sudo ./build/radar radar.conf         # try it; Ctrl-C to stop
./install-service.sh                  # then run it at every boot
```

Settings are explained in [radar.conf](radar.conf).
