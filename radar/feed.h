#ifndef RADAR_FEED_H
#define RADAR_FEED_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "config.h"

#define FEED_MAX_AC 128

typedef struct {
    char    hex[8];
    char    flight[10];     /* callsign, trimmed, may be empty */
    char    squawk[6];      /* may be empty */
    double  lat, lon;
    int     alt_ft;         /* barometric altitude */
    bool    alt_valid;
    bool    on_ground;
    double  track;          /* degrees true */
    bool    track_valid;
    uint8_t source;         /* index into cfg->sources */

    /* Extra detail for the information screen. Numbers are NAN and strings
       empty when the feed doesn't provide them. */
    char    reg[12];        /* registration (adsb.fi "r") */
    char    type[8];        /* ICAO type code (adsb.fi "t") */
    char    desc[40];       /* type description (adsb.fi "desc") */
    char    owner[40];      /* owner/operator (adsb.fi "ownOp") */
    char    category[4];    /* ADS-B emitter category, e.g. A3 */
    bool    military;       /* adsb.fi dbFlags bit 0 */
    double  gs;             /* ground speed, kt */
    double  vrate;          /* vertical rate, ft/min */
    double  sel_alt;        /* autopilot selected altitude, ft */
    double  alt_geom;       /* GPS altitude, ft */
    double  rssi;           /* signal strength, dBFS (local receiver) */
    double  seen_pos;       /* seconds since the last position */
} aircraft_t;

typedef struct {
    aircraft_t ac[FEED_MAX_AC];
    int        count;
    uint32_t   gen;         /* bumps on every successful fetch */
    time_t     last_ok;     /* 0 = never */
} feed_snapshot_t;

/* Parse an aircraft.json / ADSBx-v2 style document into out->ac / out->count.
   Only aircraft with a recent position are kept. Returns 0 on success. */
int feed_parse(const char *json, const char *array_key, feed_snapshot_t *out);

/* Start one background fetch thread per configured source. */
int feed_start(const radar_config_t *cfg);

/* Copy source src's latest snapshot into *out if it is newer than last_gen.
   Returns true if *out was updated. */
bool feed_get(int src, feed_snapshot_t *out, uint32_t last_gen);

/* Time of source src's last successful fetch (0 = never). */
time_t feed_last_ok(int src);

#endif
