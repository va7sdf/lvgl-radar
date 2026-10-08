#ifndef RADAR_UNITS_H
#define RADAR_UNITS_H

#include <stddef.h>

#include "config.h"

/*
 * Display units. Defaults are Canadian / aviation: 24-hour UTC clock,
 * degrees C, knots, feet, nautical miles and hectopascals. Everything is
 * stored internally in those units and converted only for display.
 */

typedef enum { TEMP_C, TEMP_F } unit_temp_t;
typedef enum { SPEED_KT, SPEED_KMH, SPEED_MPH } unit_speed_t;
typedef enum { ALT_FT, ALT_M } unit_alt_t;
typedef enum { DIST_NM, DIST_KM, DIST_MI } unit_dist_t;
typedef enum { PRESS_HPA, PRESS_KPA, PRESS_INHG } unit_press_t;

void units_init(const radar_config_t *cfg);

/* Each writes the value with its unit, e.g. "12°", "14 kt", "6,200 ft" */
void fmt_temp(char *out, size_t size, double c, bool with_unit);
void fmt_speed(char *out, size_t size, double kt);
void fmt_alt(char *out, size_t size, double ft);         /* FL above 18,000 ft */
void fmt_alt_plain(char *out, size_t size, double ft);   /* never a flight level */
void fmt_vrate(char *out, size_t size, double fpm);
void fmt_dist(char *out, size_t size, double nm);
void fmt_pressure(char *out, size_t size, double hpa);

/* "12,345" */
void with_commas(char *out, size_t size, long v);

#endif
