#ifndef RADAR_WEATHER_H
#define RADAR_WEATHER_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define WEATHER_DAYS 4

typedef struct {
    bool   valid;
    time_t fetched;             /* when it last arrived */

    /* Now */
    double temp_c;
    int    humidity;            /* % */
    int    code;                /* WMO weather code */
    int    is_day;              /* 1 day, 0 night, -1 not reported */
    double wind_kt, gust_kt;
    int    wind_dir;            /* degrees, where the wind comes from */
    double pressure_hpa;        /* mean sea level */

    /* Daily, [0] = today */
    int    n_days;
    char   day[WEATHER_DAYS][4];    /* "Sat" */
    int    day_code[WEATHER_DAYS];
    double day_max[WEATHER_DAYS], day_min[WEATHER_DAYS];
    int    day_pop[WEATHER_DAYS];   /* chance of precipitation, %, -1 unknown */
} weather_t;

/* url is the Open-Meteo forecast URL with {lat} {lon} already filled in.
   Empty disables weather. */
void weather_start(const char *url, int every_s);

/* Copy the latest weather; returns false if there is none yet. */
bool weather_get(weather_t *out);

/* Replace the stored weather; for testing. */
void weather_set(const weather_t *w);

/* Parse an Open-Meteo response; exposed for testing. */
bool weather_parse(const char *json, weather_t *out);

/* ---------- METAR (aviationweather.gov) ---------- */

typedef struct {
    bool   valid;
    char   raw[256];        /* the report as issued */
    char   category[8];     /* VFR, MVFR, IFR, LIFR, or empty */
    time_t fetched;
} metar_t;

/* Where METARs come from; {station} is replaced by the ICAO code. Set
   before metar_start() and lookup_metar(); the default is aviationweather.gov. */
void metar_set_source(const char *url_template);
void metar_make_url(char *out, size_t size, const char *station);

/* Fetch the METAR for station (e.g. CYYJ) every 10 minutes; empty = off. */
void metar_start(const char *station);
bool metar_get(metar_t *out);
bool metar_parse(const char *json, metar_t *out);   /* exposed for testing */
void metar_set(const metar_t *m);                   /* for testing */

/* Short text for a WMO weather code, e.g. "Light rain" */
const char *weather_text(int code);

/* The Weather Icons (Erik Flowers) glyph for a WMO code, as a UTF-8
   string for a label set in that font. */
const char *weather_icon(int code, bool day);

#endif
