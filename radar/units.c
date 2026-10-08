/* Unit conversion and formatting for display. */

#include "units.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const radar_config_t *cfg;

void units_init(const radar_config_t *config)
{
    cfg = config;
}

void with_commas(char *out, size_t size, long v)
{
    char tmp[24];
    snprintf(tmp, sizeof tmp, "%ld", v < 0 ? -v : v);
    size_t n = strlen(tmp), o = 0;
    if (v < 0 && o + 1 < size)
        out[o++] = '-';
    for (size_t i = 0; i < n && o + 1 < size; i++) {
        out[o++] = tmp[i];
        if ((n - i - 1) % 3 == 0 && i + 1 < n && o + 1 < size)
            out[o++] = ',';
    }
    out[o] = '\0';
}

void fmt_temp(char *out, size_t size, double c, bool with_unit)
{
    bool f = cfg->unit_temp == TEMP_F;
    double v = f ? c * 9.0 / 5.0 + 32.0 : c;
    long r = lround(v);
    if (r == 0)
        r = 0;                      /* no "-0" */
    snprintf(out, size, with_unit ? "%ld°%s" : "%ld°", r, f ? "F" : "C");
}

void fmt_speed(char *out, size_t size, double kt)
{
    switch (cfg->unit_speed) {
    case SPEED_KMH: snprintf(out, size, "%ld km/h", lround(kt * 1.852)); break;
    case SPEED_MPH: snprintf(out, size, "%ld mph", lround(kt * 1.150779)); break;
    default:        snprintf(out, size, "%ld kt", lround(kt)); break;
    }
}

void fmt_alt_plain(char *out, size_t size, double ft)
{
    char n[24];
    if (cfg->unit_alt == ALT_M) {
        with_commas(n, sizeof n, lround(ft * 0.3048));
        snprintf(out, size, "%s m", n);
    } else {
        with_commas(n, sizeof n, lround(ft));
        snprintf(out, size, "%s ft", n);
    }
}

void fmt_alt(char *out, size_t size, double ft)
{
    if (ft >= 18000)
        snprintf(out, size, "FL%03ld", lround(ft / 100));
    else
        fmt_alt_plain(out, size, ft);
}

void fmt_vrate(char *out, size_t size, double fpm)
{
    if (cfg->unit_alt == ALT_M) {
        snprintf(out, size, "%+.1f m/s", fpm * 0.3048 / 60.0);
    } else {
        char n[24];
        with_commas(n, sizeof n, lround(fpm / 50) * 50);
        snprintf(out, size, "%s%s fpm", fpm > 0 ? "+" : "", n);
    }
}

void fmt_dist(char *out, size_t size, double nm)
{
    switch (cfg->unit_dist) {
    case DIST_KM: snprintf(out, size, "%.1f km", nm * 1.852); break;
    case DIST_MI: snprintf(out, size, "%.1f mi", nm * 1.150779); break;
    default:      snprintf(out, size, "%.1f nm", nm); break;
    }
}

void fmt_pressure(char *out, size_t size, double hpa)
{
    switch (cfg->unit_pressure) {
    case PRESS_KPA:  snprintf(out, size, "%.1f kPa", hpa / 10.0); break;
    case PRESS_INHG: snprintf(out, size, "%.2f inHg", hpa * 0.02953); break;
    default:         snprintf(out, size, "%ld hPa", lround(hpa)); break;
    }
}
