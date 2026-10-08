#ifndef RADAR_LOOKUP_H
#define RADAR_LOOKUP_H

#include <stdbool.h>

#include "weather.h"

/*
 * On-demand lookups from adsbdb.com, made only when someone opens an
 * aircraft's details. Requests are queued to a background thread and the
 * answers cached for the session, so each aircraft or flight is asked
 * about once.
 */

typedef enum { LOOKUP_PENDING, LOOKUP_FOUND, LOOKUP_NONE } lookup_state_t;

typedef struct {
    char reg[16];
    char type[48];          /* e.g. "A320 214" */
    char icao_type[8];      /* e.g. "A320" */
    char manufacturer[40];
    char owner[48];
} lookup_aircraft_t;

typedef struct {
    char airline[48];
    char origin_icao[8], origin_iata[8], origin_city[40];
    char dest_icao[8], dest_iata[8], dest_city[40];
} lookup_route_t;

/* base_url is e.g. https://api.adsbdb.com/v0; empty disables lookups */
void lookup_start(const char *base_url);
bool lookup_enabled(void);

/* Ask once; later calls return the cached state (and fill *out if found). */
lookup_state_t lookup_aircraft(const char *hex, lookup_aircraft_t *out);
lookup_state_t lookup_route(const char *callsign, lookup_route_t *out);

/* Latest METAR for an airport (aviationweather.gov), refreshed every 15 min. */
lookup_state_t lookup_metar(const char *icao, metar_t *out);

/* Store a route as if it had been looked up; for testing. */
void lookup_set_route(const char *callsign, const lookup_route_t *r);

/* Parsers, exposed for testing. Return true if the response held data. */
bool lookup_parse_aircraft(const char *json, lookup_aircraft_t *out);
bool lookup_parse_route(const char *json, lookup_route_t *out);

#endif
