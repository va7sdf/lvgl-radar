---
title: How the radar works
---

# 5. How the radar works

## The big picture

```
 your receiver ──► aircraft.json ─┐
                                  ├─► fetch threads ──► snapshots ─┐
 adsb.fi (internet) ──► JSON ─────┘    (libcurl, cJSON)            │
                                                                   ▼
 touch ──► evdev ──► LVGL ◄── timers in the main thread: merge, plot, sweep
                       │
                       ▼
                  /dev/fb0 ──► the screen
```

ADS-B (Automatic Dependent Surveillance–Broadcast) is how aircraft announce
themselves: about twice a second each one broadcasts its identity,
position, altitude, speed and heading on 1090 MHz. A receiver such as
dump1090 decodes those messages from a cheap USB radio and publishes the
current picture as `aircraft.json`, a file that updates every second.

The radar reads that file, and adsb.fi's equivalent for the same area,
and draws what's in it.

## Threads: don't keep the screen waiting

A web request can take a moment, or hang for seconds when Wi-Fi drops. If
the screen's loop waited for it, the sweep would stutter and touches would
be ignored. So each data source has its own **thread**: a loop that
fetches, parses, stores the result and sleeps until the next poll.

The one firm rule: **LVGL may only be used from the main thread.** It's not
built for several threads changing widgets at once. So fetch threads never
touch the screen. They put each new list of aircraft in a *snapshot*,
guarded by a mutex (a lock), and the main thread copies the latest
snapshot every half second from an LVGL timer:

```c
lv_timer_create(refresh_cb, 500, NULL);   // every 500 ms, in the main thread

static void refresh_cb(lv_timer_t *t)
{
    for (int s = 0; s < cfg.n_sources; s++)
        feed_get(s, &snaps[s], ...);      // copy under the lock
    merge_sources();                      // one list, first source wins
    radar_ui_update(&merged);             // move the aircraft
    ui_update(&merged);                   // the other screens
}
```

In Python terms: worker threads put results in a `queue`, and the GUI
thread takes them out. Weather, METARs, receiver stats and aircraft
lookups work the same way, each in its own thread.

## Merging two sources

My receiver and adsb.fi often see the same aircraft. They're matched by
their **ICAO address**, a 24-bit number unique to each airframe, shown as
six hex digits like `C0184B`. The first source in `radar.conf` wins (put
your own first), and it borrows details only adsb.fi has, such as
registration, type, owner and the military flag. Each aircraft is drawn in
its source's colour, so you can see what your own antenna is hearing.

## From latitude and longitude to pixels

Over 25 nautical miles the Earth is flat enough. One minute of latitude is
one nautical mile; a minute of longitude is a little shorter, by the
cosine of the latitude:

```c
double east_nm  = (lon - home_lon) * 60.0 * cos(home_lat * M_PI / 180.0);
double north_nm = (lat - home_lat) * 60.0;
int x = 540 + east_nm  * 500 / range_nm;    // 500 px = outer ring
int y = 540 - north_nm * 500 / range_nm;    // screen y grows downwards
```

## The sweep

The sweep is the most demanding part: a wedge of fading green, hundreds of
pixels long, turning smoothly 20 times a second on a Zero 2.

Drawing it with LVGL's shapes would mean redrawing the whole disc every
frame, which is too slow. Instead the face (disc, map, rings, labels) is
drawn once into a **canvas**, a widget that's just a block of pixels the
program can write to directly. Two copies are kept: the face as it should
look without the sweep, and what's on screen.

At start-up a **lookup table** is built holding, for every pixel, its
bearing from the centre. Each frame, only the pixels the sweep has moved
across are recomputed, and for each one the work is tiny:

```c
uint16_t d = angle - bearing[pixel];      // how far behind the sweep edge
if (d < trail)
    screen[pixel] = blend(face[pixel], green, fade[d]);   // glowing
else
    screen[pixel] = face[pixel];          // back to the plain face
```

No trigonometry per frame, just a subtraction, a table lookup and a
colour blend. Angles are 16-bit numbers where 65,536 is a full circle, so
they wrap around north by themselves. The Settings screen shows how long
each sweep frame takes to draw, against the time available per frame.

## Aircraft move only when swept

A real radar only learns where an aircraft is when the beam passes it, so
the blip jumps once per turn and glows, then fades until the next pass.
The radar copies that. New positions are stored when they arrive, but the
blip moves only when the sweep crosses its bearing:

1. The sweep crosses the aircraft: move the blip to the latest position,
   set its glow to full, add a point to its track.
2. Every frame after: fade it a little (the *phosphor*).
3. If the aircraft has gone from the data when the sweep comes round,
   remove it and its track.

## Tracks

Each aircraft keeps a history of where it's been (two minutes by default)
and draws it as a line that fades with age. Tracks are drawn into the
canvas under the sweep, so the sweep glides over them. When an aircraft
leaves the scope or vanishes, its track goes with it.

## Colours that mean something

- Each source has its own colour (green for mine, blue for adsb.fi)
- **Red**: squawk 7500 (hijack), 7600 (radio failure) or 7700 (emergency)
- **Grey**: on the ground
- The label shows the callsign and the altitude in hundreds of feet
  (`062` is 6,200 ft), as on an air traffic control screen

## Memory

The canvas and its copies are 2.3 MB each (1080 × 1080 × 2 bytes), the
bearing table the same again: about 10 MB in all, fine on a 512 MB Zero 2.
