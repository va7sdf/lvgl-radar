---
title: Where the data comes from
---

# 9. Where the data comes from

Everything comes over plain HTTP(S) as JSON, fetched with **libcurl** and
read with **cJSON**, a small C JSON parser. None of these services needs
an account or a key.

| Source | Gives | Used for | Terms |
|---|---|---|---|
| Your receiver's `aircraft.json` | Every aircraft it hears, each second | The radar | Yours |
| [adsb.fi](https://adsb.fi) open data | Aircraft near a point, from thousands of feeders | Filling in what your antenna misses; registration and type | Personal, non-commercial use; at most 1 request a second; credit adsb.fi |
| [adsbdb.com](https://www.adsbdb.com) | An aircraft's type, registration and owner; a flight's route | Details screen, airport board | Free; keep requests light |
| [Open-Meteo](https://open-meteo.com) | Current weather and forecast for a point | Clock & weather screen | Free for non-commercial use; CC BY 4.0, credit Open-Meteo |
| [aviationweather.gov](https://aviationweather.gov) (NOAA) | METARs, worldwide | Weather and airport screens | US government, public |
| Your receiver's `stats.json` | Message rates and signal levels | Receiver screen | Yours |

## Your receiver

dump1090 writes `aircraft.json` once a second. One aircraft looks like
this (trimmed):

```json
{ "hex": "c0184b", "flight": "JZA213  ", "alt_baro": 6200, "gs": 212.4,
  "track": 40.1, "lat": 48.4000, "lon": -123.3000, "squawk": "4521",
  "baro_rate": -832, "category": "A2", "rssi": -18.6, "seen_pos": 0.4 }
```

`seen_pos` is how many seconds ago the position was heard; anything over a
minute old is ignored so stale aircraft don't linger.

## adsb.fi

adsb.fi gathers data from volunteers' receivers worldwide and offers it in
the same format. Its URL takes your position and a radius:

```ini
url = https://opendata.adsb.fi/api/v3/lat/{lat}/lon/{lon}/dist/{dist}
```

The radar fills in `{lat}` and `{lon}` from `radar.conf`, and `{dist}` as
the widest range plus 20%, so aircraft don't pop in and out at the edge.
It polls every 3 seconds, well within the one-a-second limit, and the
status line names adsb.fi as the terms ask.

Its records add fields your receiver can't know, such as registration
(`r`), type (`t`), a description, owner, and `dbFlags`, whose lowest bit
marks military aircraft. The alerts use that.

## adsbdb

When you tap an aircraft, a background thread asks adsbdb about it:

```
https://api.adsbdb.com/v0/aircraft/C0184B      type, registration, owner
https://api.adsbdb.com/v0/callsign/JZA213      airline, origin, destination
```

Answers are cached, so each aircraft and flight is asked about only once.
Routes are only looked up for airline-style callsigns (three letters then a
number, like JZA213), not for registrations used as callsigns.

## Open-Meteo

One request every 15 minutes brings the current temperature, humidity,
WMO weather code, wind (in knots), pressure and whether it's day or night,
plus highs, lows, weather codes and chance of rain for the next few days.

## METARs

```
CYYJ 031600Z 25012G22KT 15SM -RA FEW018 BKN040 OVC080 12/09 A2990
```

Victoria, on the 3rd at 16:00 UTC: wind from 250° at 12 knots gusting 22,
15 statute miles visibility, light rain, cloud layers at 1,800, 4,000 and
8,000 feet, temperature 12 °C, dew point 9 °C, altimeter 29.90 inches. The
JSON from aviationweather.gov also gives the flight category (VFR, MVFR,
IFR, LIFR), which colours it on screen.

## What I left out

- **Flight schedules** for the airport boards: the good sources are paid.
  The live board uses adsbdb routes for aircraft already on the scope.
- **NOTAMs**: Nav Canada has no open API, and the FAA's needs registration.
- **Aircraft photos**: each photo belongs to its photographer, and the
  terms for showing them in an app weren't clear, so I skipped them.

> **A good habit:** every one of these is a free service run by people who
> asked only for a credit and light use. The radar polls no faster than it
> needs, caches lookups, backs off when a source fails, and lists them all
> on the Credits screen. Aggregators like adsb.fi exist because people
> feed them, so I'm planning to give back by feeding my own receiver's data
> to adsb.fi and airplanes.live.

## IPv4 and IPv6

The radar fetches with libcurl and the Python tools with Python's own
library. Both handle IPv6 and IPv4 alike: given a name with both kinds of
address, they try IPv6 first and fall back to IPv4. On a home network
with both (*dual-stack*), everything works whichever the services offer.
On a network with IPv6 only, the IPv4-only services below are reachable
only if the network provides NAT64/DNS64, a translation service that most
IPv6-only networks, mobile carriers included, do offer.

| Has IPv6 | IPv4 only |
|---|---|
| adsbdb.com (lookups) | adsb.fi (aircraft) |
| aviationweather.gov (METARs) | Open-Meteo (weather) |
| raw.githubusercontent.com (GSHHG shoreline data) | GeoNames (towns and peaks) |
| OurAirports data (on GitHub Pages) | github.com (cloning the repo) |

This was checked in October 2026; services add IPv6 over time. On your own
network the receiver, SSH and the screen mirror (chapter 11) all work over
IPv6 too.
