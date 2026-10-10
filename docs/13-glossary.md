---
title: Glossary
---

# 13. Glossary

## Aviation and weather

- **ADS-B** (Automatic Dependent Surveillance–Broadcast): aircraft
  broadcast their own identity, position, altitude and speed about twice
  a second on 1090 MHz. Anyone with a receiver can listen.
- **Airspeed**: speed through the air. Not shown here; see *ground speed*.
- **Altitude**: in the details, the *pressure altitude* the transponder
  reports, measured as if the air pressure at sea level were the standard
  29.92 inHg (1013.25 hPa). Close to height above sea level, but it varies
  with the weather.
- **ATIS, TWR, GND, CLD, APP, ARR, DEP, MF, RCO, CTAF**: the radio
  frequencies on an airport's screen. ATIS (Automatic Terminal Information
  Service) is a recorded weather and runway broadcast; TWR is the tower;
  GND is ground control (taxiing); CLD is clearance delivery (flight plan
  approvals); APP, ARR and DEP are approach, arrival and departure control;
  MF is a Canadian *mandatory frequency* at airports without a tower; RCO
  is a *remote communications outlet* for reaching a flight service
  station; CTAF is the *common traffic advisory frequency* pilots use to
  announce themselves at uncontrolled fields.
- **Bearing**: the direction from you to something, in degrees clockwise
  from true north (090° is east).
- **Callsign**: the name an aircraft uses on the radio and in ADS-B.
  Airlines use their ICAO code and a flight number: JZA213 is Jazz flight
  213. Private aircraft usually use their registration, like CGSDZ.
- **Category**: the size or kind of aircraft, as broadcast: A1 light (under
  15,500 lb), A2 small, A3 large, A4 a large aircraft with strong wake
  turbulence (the Boeing 757), A5 heavy (over 300,000 lb), A6 high
  performance, A7 rotorcraft, and B and C codes for gliders, balloons,
  drones and airport vehicles.
- **Ceiling**: the height of the lowest cloud layer covering more than half
  the sky (*broken* or *overcast*).
- **Closing and opening**: whether an aircraft is getting nearer to you
  (closing) or moving away (opening), and how fast its distance is
  changing, in knots.
- **DME** (Distance Measuring Equipment): a beacon, often paired with a VOR,
  that tells an aircraft its distance from the station.
- **Flight category**: how good the weather is for flying by sight, from a
  METAR's ceiling and visibility:
  **VFR** (Visual Flight Rules): ceiling above 3,000 ft and visibility over
  5 miles. **MVFR** (Marginal VFR): ceiling 1,000–3,000 ft or visibility
  3–5 miles. **IFR** (Instrument Flight Rules): ceiling 500 to under
  1,000 ft or visibility 1 to under 3 miles. **LIFR** (Low IFR): ceiling
  under 500 ft or visibility under 1 mile. Visibility is in statute miles.
- **Flight level**: altitude in hundreds of feet at standard pressure, used
  high up so everyone's altimeters agree: FL350 is 35,000 ft. In Canada's
  southern airspace and in the US, flight levels start at 18,000 ft.
- **Ground speed**: speed over the ground, in knots. It's airspeed plus or
  minus the wind, which is why a westbound airliner into a headwind can be
  100 knots slower than an eastbound one.
- **hPa, kPa, inHg**: units of air pressure: hectopascals (the same as
  millibars), kilopascals, and inches of mercury (used in North American
  altimeter settings). 1013 hPa = 101.3 kPa = 29.92 inHg.
- **ICAO** (International Civil Aviation Organization): the United Nations
  agency that sets world aviation standards.
- **ICAO address** (shown as *Hex*): a 24-bit number unique to each
  aircraft's transponder, written as six hexadecimal digits like C0184B.
  Blocks of addresses belong to countries; C0 to C3 are Canada's.
- **ICAO airport code**: four letters, like CYYJ for Victoria International.
  The first letter is the region: C for Canada, K for the contiguous US.
  The three-letter codes on luggage tags (YYJ) are IATA codes.
- **ICAO type designator**: a short code for an aircraft model: DH8D is the
  Dash 8-400, B738 the Boeing 737-800, C172 the Cessna 172.
- **Knot**: one nautical mile per hour, 1.852 km/h.
- **Magnetic variation**: the angle between true north and magnetic north.
  Around Victoria, magnetic north is about 15–18° east of true north.
- **METAR** (Meteorological Aerodrome Report): the standard routine
  airport weather report, usually issued every hour. Chapter 9 decodes one.
- **Military flag**: adsb.fi marks aircraft its database lists as
  military, which the radar can alert on.
- **Nautical mile**: 1,852 m, or one minute of latitude. The radar's range
  rings are in nautical miles.
- **Navaid** (navigation aid): a radio beacon pilots navigate by, such as a
  VOR or NDB.
- **NDB** (Non-Directional Beacon): a simple low-frequency beacon; a
  direction finder in the aircraft points at it. An **NDB-DME** adds
  distance.
- **NOTAM**: a notice to pilots of a temporary hazard or change, such as a
  closed runway.
- **Registration**: an aircraft's licence plate, painted on its side:
  C-GKQJ (C- for Canada, N for the US). Also called the tail number.
- **Selected altitude**: the altitude the pilots have set on the autopilot,
  which the aircraft is climbing or descending to.
- **Squawk**: the four-digit code the pilot sets on the transponder, given
  by air traffic control. 1200 means flying by sight (VFR) in North
  America; 7500 means hijack, 7600 radio failure and 7700 emergency.
- **TACAN** (Tactical Air Navigation): the military equivalent of a VOR and
  DME. A **VORTAC** is a VOR and TACAN at one site.
- **Track**: the direction an aircraft is moving over the ground, in
  degrees. It can differ from its *heading* (where the nose points) because
  of the wind.
- **Transponder**: the radio in an aircraft that answers air traffic
  control radar and, with ADS-B, broadcasts its position.
- **UTC** (Coordinated Universal Time): the world's reference time, the
  time at the zero meridian through Greenwich, with no daylight saving.
- **Vertical speed**: how fast an aircraft is climbing (+) or descending
  (−), in feet per minute.
- **VOR** (VHF Omnidirectional Range): a beacon that tells an aircraft its
  bearing from the station. A **VOR-DME** adds distance.
- **WMO weather code**: a number from the World Meteorological Organization
  describing the weather (0 clear, 3 overcast, 61 light rain, 95
  thunderstorm). Open-Meteo reports one, and it picks the weather icon.
- **Zulu**: aviation's name for UTC, from the time zone letter Z, "Zulu" in
  the phonetic alphabet. `1600Z` is 16:00 UTC.

## Computing

- **Aggregator and feeder**: an aggregator such as adsb.fi combines the
  data from many people's receivers, the feeders, into one picture.
- **dBFS** (decibels relative to full scale): signal strength at the
  receiver, where 0 is the loudest it can measure and lower (more
  negative) numbers are weaker. A *strong* signal is one near 0 that can
  overload the radio.
- **Dual-stack**: a network that has both IPv4 and IPv6.
- **dump1090**: the program that decodes ADS-B from a cheap USB radio and
  publishes `aircraft.json`. dump1090-fa is FlightAware's version.
- **evdev**: Linux's interface for input devices, `/dev/input/event*`.
- **Framebuffer**: the screen's memory as a file, `/dev/fb0`.
- **Gesture**: a swipe, which LVGL reports as up, down, left or right.
- **KMS** (Kernel Mode Setting): how Linux sets the display's resolution;
  the `video=` setting in `cmdline.txt` goes to it.
- **LVGL** (Light and Versatile Graphics Library): the C library that draws
  everything on the screen.
- **mDNS**: how `radar.local` finds the Pi on the local network without a
  DNS server. Pi OS's avahi-daemon answers it, for IPv4 and IPv6.
- **Motion JPEG**: video sent as a stream of JPEG pictures, which browsers
  show without plugins. The screen mirror uses it.
- **Mutex**: a lock that lets one thread at a time use shared data.
- **NAT64 and DNS64**: a translation service that lets devices on an
  IPv6-only network reach IPv4-only servers.
- **Phosphor**: the glowing coating on an old radar or TV tube, which faded
  slowly; the radar's fading blips imitate it.
- **RGB565**: a pixel in 16 bits: 5 for red, 6 for green, 5 for blue.
- **Submodule**: a git repository included in another at a fixed version;
  LVGL is one in this repository.
- **systemd service**: a program Linux starts at boot and restarts if it
  stops.
- **Thread**: a part of a program that runs at the same time as the rest.
- **Tiny TTF**: LVGL's way of drawing TrueType fonts at any size.
