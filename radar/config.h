#ifndef RADAR_CONFIG_H
#define RADAR_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_SOURCES 4
#define MAX_RANGES  32

typedef struct {
    char     name[24];      /* shown in the status line, e.g. "local" */
    char     url[512];      /* {lat} {lon} {dist} already filled in */
    char     array_key[32]; /* "aircraft" for dump1090, "ac" for adsb.fi */
    int      poll_s;        /* seconds between fetches */
    uint32_t colour;        /* 0xRRGGBB for this source's aircraft */
} source_config_t;

typedef struct {
    double lat;             /* station latitude, degrees */
    double lon;             /* station longitude, degrees */
    double range_nm;        /* radius of the outer ring */
    double ranges[MAX_RANGES];  /* from range_min to range_max in range_step */
    int    n_ranges;
    char   path[256];       /* where the config was loaded from */
    char   credit[64];      /* on-screen data credit, may be empty */
    double sweep_period_s;  /* seconds per revolution */
    double sweep_trail_deg; /* width of the fading trail, 0 = no sweep */
    int    sweep_fps;       /* sweep frames per second */
    int    trail_s;         /* seconds of track history to draw, 0 = off */
    char   map_file[256];   /* outline map from make_map.py, empty = none */
    uint32_t map_colour;    /* 0xRRGGBB */
    uint32_t map_label_colour;  /* airports, navaids, towns */
    bool   show_airports;
    bool   show_navaids;
    bool   show_towns;
    bool   show_peaks;
    bool   show_rivers;
    uint32_t map_river_colour;
    char   map_options[256];    /* extra make_map.py options for "Update map" */
    char   lookup_url[256];     /* adsbdb base URL, empty = no online lookups */
    char   weather_url[512];    /* Open-Meteo URL, {lat} {lon} filled in; empty = none */
    int    weather_every_s;
    char   font_file[256];      /* TrueType font for the clock */
    char   temp_font_file[256]; /* ... and for the temperature (empty = font_file) */
    char   metar_url[512];      /* {station} = ICAO code; empty = aviationweather.gov */
    char   icon_font_file[256]; /* Weather Icons font (empty = no icons) */
    bool   clock_24h;
    bool   clock_utc;           /* big clock shows UTC, local time underneath */
    int    unit_temp, unit_speed, unit_alt, unit_dist, unit_pressure;   /* see units.h */
    int    detail_timeout_s;    /* aircraft details close after this long untouched */
    int    idle_after_s;        /* clock/weather after this long with no aircraft */
    int    weather_view_s;      /* a swiped-to weather screen returns to the radar */
    char   stats_url[512];      /* your dump1090's stats.json */
    char   range_file[256];     /* where the range plot is kept */
    bool   alert_emergency;
    bool   alert_military;
    char   watch[512];          /* watch list: hex, registration, callsign, type; * = prefix */
    int    buzzer_gpio;         /* -1 = no buzzer */
    char   metar_station[8];    /* e.g. CYYJ; empty = no METAR */
    char   fb_dev[64];
    char   touch_dev[256];

    /* Data sources in priority order: when several report the same
       aircraft, the first one listed wins. */
    source_config_t sources[MAX_SOURCES];
    int             n_sources;
} radar_config_t;

/* Returns 0 on success, -1 on error (message printed to stderr). */
int config_load(const char *path, radar_config_t *cfg);

/* Index of val in a "a|b|c" list of choices, ignoring case; 0 if not found */
int pick(const char *val, const char *options);

/* Change one "key = value" line in the config file, keeping everything else
   (comments, layout, ownership) as it was. Adds the line if it's missing.
   Returns 0 on success. */
int config_save_value(const char *path, const char *key, const char *value);

#endif
