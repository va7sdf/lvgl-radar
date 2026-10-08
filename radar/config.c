#include "config.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

/* Cut a trailing "  # comment" (a # after whitespace, so "#48FF6A" survives) */
static char *find_comment(char *val)
{
    for (char *p = val; *p; p++)
        if (*p == '#' && p > val && isspace((unsigned char)p[-1]))
            return p;
    return NULL;
}

static void strip_comment(char *val)
{
    char *c = find_comment(val);
    if (c) {
        *c = '\0';
        char *end = c;
        while (end > val && isspace((unsigned char)end[-1]))
            *--end = '\0';
    }
}

static void copy_str(char *dst, size_t size, const char *src)
{
    snprintf(dst, size, "%s", src);
}

/* Replace {lat}, {lon} and {dist} in the URL template. */
static void expand_url(const char *tmpl, const radar_config_t *cfg,
                       char *out, size_t size)
{
    /* Ask the server for a little more than the outer ring so aircraft
       don't pop in and out right at the edge. */
    double widest = cfg->range_nm;
    for (int i = 0; i < cfg->n_ranges; i++)
        if (cfg->ranges[i] > widest)
            widest = cfg->ranges[i];
    int dist = (int)ceil(widest * 1.2);
    if (dist < 1)
        dist = 1;

    size_t n = 0;
    out[0] = '\0';
    for (const char *p = tmpl; *p && n + 1 < size; ) {
        char val[32] = "";
        size_t skip = 0;
        if (strncmp(p, "{lat}", 5) == 0) {
            snprintf(val, sizeof val, "%.4f", cfg->lat);
            skip = 5;
        } else if (strncmp(p, "{lon}", 5) == 0) {
            snprintf(val, sizeof val, "%.4f", cfg->lon);
            skip = 5;
        } else if (strncmp(p, "{dist}", 6) == 0) {
            snprintf(val, sizeof val, "%d", dist);
            skip = 6;
        }
        if (skip) {
            n += (size_t)snprintf(out + n, size - n, "%s", val);
            if (n >= size)
                n = size - 1;
            p += skip;
        } else {
            out[n++] = *p++;
            out[n] = '\0';
        }
    }
}

static source_config_t *new_source(radar_config_t *cfg, char tmpl[][512])
{
    if (cfg->n_sources >= MAX_SOURCES)
        return NULL;
    source_config_t *src = &cfg->sources[cfg->n_sources];
    tmpl[cfg->n_sources][0] = '\0';
    snprintf(src->name, sizeof src->name, "source %d", cfg->n_sources + 1);
    copy_str(src->array_key, sizeof src->array_key, "aircraft");
    src->poll_s = 3;
    src->colour = 0x48FF6A;
    cfg->n_sources++;
    return src;
}

static bool yes(const char *v)
{
    return strcasecmp(v, "yes") == 0 || strcasecmp(v, "true") == 0 ||
           strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0;
}

/* Index of val in a "a|b|c" list, ignoring case; 0 if not found */
int pick(const char *val, const char *options)
{
    int i = 0;
    const char *p = options;
    while (*p) {
        size_t n = strcspn(p, "|");
        if (strlen(val) == n && strncasecmp(val, p, n) == 0)
            return i;
        p += n;
        if (*p == '|')
            p++;
        i++;
    }
    return 0;
}

static int is_source_key(const char *key)
{
    return strcmp(key, "name") == 0 || strcmp(key, "url") == 0 ||
           strcmp(key, "array_key") == 0 || strcmp(key, "poll_s") == 0 ||
           strcmp(key, "colour") == 0 || strcmp(key, "color") == 0;
}

int config_load(const char *path, radar_config_t *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->lat = NAN;
    cfg->lon = NAN;
    cfg->range_nm = 10;
    snprintf(cfg->path, sizeof cfg->path, "%s", path);
    cfg->sweep_period_s = 4;
    cfg->sweep_trail_deg = 45;
    cfg->sweep_fps = 20;
    cfg->trail_s = 120;
    copy_str(cfg->map_file, sizeof cfg->map_file, "map.txt");
    cfg->map_colour = 0x3E7A63;
    cfg->map_label_colour = 0x7FB59C;
    cfg->show_airports = true;
    cfg->show_navaids = true;
    cfg->show_towns = true;
    cfg->show_peaks = true;
    cfg->show_rivers = true;
    cfg->map_river_colour = 0x3C6E8A;
    copy_str(cfg->lookup_url, sizeof cfg->lookup_url, "https://api.adsbdb.com/v0");
    cfg->weather_every_s = 900;
    copy_str(cfg->font_file, sizeof cfg->font_file, "fonts/Montserrat-Medium.ttf");
    cfg->detail_timeout_s = 20;
    cfg->clock_24h = true;
    cfg->clock_utc = true;
    copy_str(cfg->icon_font_file, sizeof cfg->icon_font_file, "fonts/weathericons-regular-webfont.ttf");
    copy_str(cfg->temp_font_file, sizeof cfg->temp_font_file,
             "fonts/Montserrat-Medium.ttf");
    copy_str(cfg->range_file, sizeof cfg->range_file, "range.txt");
    cfg->alert_emergency = true;
    cfg->alert_military = true;
    cfg->buzzer_gpio = -1;
    cfg->idle_after_s = 180;
    cfg->weather_view_s = 30;
    copy_str(cfg->fb_dev, sizeof cfg->fb_dev, "/dev/fb0");

    char url_tmpl[MAX_SOURCES][512];
    char weather_tmpl[512] =
        "https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}"
        "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,"
        "wind_direction_10m,wind_gusts_10m,pressure_msl,is_day"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
        "&wind_speed_unit=kn&timezone=auto&forecast_days=4";
    source_config_t *src = NULL;        /* the [source] section being read */
    bool stats_set = false;
    double rmin = 5, rmax = 25, rstep = 5;

    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return -1;
    }

    char line[768];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *s = trim(line);
        if (*s == '\0' || *s == '#')
            continue;

        if (strcmp(s, "[source]") == 0) {
            src = new_source(cfg, url_tmpl);
            if (!src)
                fprintf(stderr, "%s:%d: at most %d sources, ignoring this one\n",
                        path, lineno, MAX_SOURCES);
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) {
            fprintf(stderr, "%s:%d: expected key = value\n", path, lineno);
            continue;
        }
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);
        strip_comment(val);

        if (is_source_key(key)) {
            if (!src && cfg->n_sources == 0)
                src = new_source(cfg, url_tmpl);    /* settings outside any [source] */
            if (!src)
                continue;
            int i = (int)(src - cfg->sources);
            if      (strcmp(key, "name") == 0)      copy_str(src->name, sizeof src->name, val);
            else if (strcmp(key, "url") == 0)       copy_str(url_tmpl[i], sizeof url_tmpl[i], val);
            else if (strcmp(key, "array_key") == 0) copy_str(src->array_key, sizeof src->array_key, val);
            else if (strcmp(key, "poll_s") == 0)    src->poll_s = atoi(val);
            else                                    src->colour = (uint32_t)strtoul(val[0] == '#' ? val + 1 : val, NULL, 16);
            continue;
        }

        if      (strcmp(key, "lat") == 0)       cfg->lat = atof(val);
        else if (strcmp(key, "lon") == 0)       cfg->lon = atof(val);
        else if (strcmp(key, "range_nm") == 0)  cfg->range_nm = atof(val);
        else if (strcmp(key, "range_min") == 0)  rmin = atof(val);
        else if (strcmp(key, "range_max") == 0)  rmax = atof(val);
        else if (strcmp(key, "range_step") == 0) rstep = atof(val);
        else if (strcmp(key, "credit") == 0)    copy_str(cfg->credit, sizeof cfg->credit, val);
        else if (strcmp(key, "sweep_period_s") == 0)  cfg->sweep_period_s = atof(val);
        else if (strcmp(key, "sweep_trail_deg") == 0) cfg->sweep_trail_deg = atof(val);
        else if (strcmp(key, "sweep_fps") == 0)       cfg->sweep_fps = atoi(val);
        else if (strcmp(key, "trail_s") == 0)         cfg->trail_s = atoi(val);
        else if (strcmp(key, "map_file") == 0)  copy_str(cfg->map_file, sizeof cfg->map_file, val);
        else if (strcmp(key, "map_options") == 0) copy_str(cfg->map_options, sizeof cfg->map_options, val);
        else if (strcmp(key, "map_colour") == 0 || strcmp(key, "map_color") == 0)
            cfg->map_colour = (uint32_t)strtoul(val[0] == '#' ? val + 1 : val, NULL, 16);
        else if (strcmp(key, "map_label_colour") == 0 || strcmp(key, "map_label_color") == 0)
            cfg->map_label_colour = (uint32_t)strtoul(val[0] == '#' ? val + 1 : val, NULL, 16);
        else if (strcmp(key, "show_airports") == 0) cfg->show_airports = yes(val);
        else if (strcmp(key, "show_navaids") == 0)  cfg->show_navaids = yes(val);
        else if (strcmp(key, "show_towns") == 0)    cfg->show_towns = yes(val);
        else if (strcmp(key, "show_peaks") == 0)    cfg->show_peaks = yes(val);
        else if (strcmp(key, "show_rivers") == 0)   cfg->show_rivers = yes(val);
        else if (strcmp(key, "map_river_colour") == 0 || strcmp(key, "map_river_color") == 0)
            cfg->map_river_colour = (uint32_t)strtoul(val[0] == '#' ? val + 1 : val, NULL, 16);
        else if (strcmp(key, "lookup_url") == 0)       copy_str(cfg->lookup_url, sizeof cfg->lookup_url, val);
        else if (strcmp(key, "weather_url") == 0)      copy_str(weather_tmpl, sizeof weather_tmpl, val);
        else if (strcmp(key, "weather_every_s") == 0)  cfg->weather_every_s = atoi(val);
        else if (strcmp(key, "font_file") == 0)        copy_str(cfg->font_file, sizeof cfg->font_file, val);
        else if (strcmp(key, "detail_timeout_s") == 0) cfg->detail_timeout_s = atoi(val);
        else if (strcmp(key, "clock_24h") == 0)        cfg->clock_24h = yes(val);
        else if (strcmp(key, "clock_main") == 0)       cfg->clock_utc = strcasecmp(val, "local") != 0;
        else if (strcmp(key, "temp_font_file") == 0)   copy_str(cfg->temp_font_file, sizeof cfg->temp_font_file, val);
        else if (strcmp(key, "icon_font_file") == 0)   copy_str(cfg->icon_font_file, sizeof cfg->icon_font_file, val);
        else if (strcmp(key, "unit_temp") == 0)        cfg->unit_temp = pick(val, "C|F");
        else if (strcmp(key, "unit_speed") == 0)       cfg->unit_speed = pick(val, "kt|km/h|mph");
        else if (strcmp(key, "unit_alt") == 0)         cfg->unit_alt = pick(val, "ft|m");
        else if (strcmp(key, "unit_dist") == 0)        cfg->unit_dist = pick(val, "nm|km|mi");
        else if (strcmp(key, "unit_pressure") == 0)    cfg->unit_pressure = pick(val, "hPa|kPa|inHg");
        else if (strcmp(key, "stats_url") == 0)        { copy_str(cfg->stats_url, sizeof cfg->stats_url, val); stats_set = val[0] != 0; }
        else if (strcmp(key, "range_file") == 0)       copy_str(cfg->range_file, sizeof cfg->range_file, val);
        else if (strcmp(key, "alert_emergency") == 0)  cfg->alert_emergency = yes(val);
        else if (strcmp(key, "alert_military") == 0)   cfg->alert_military = yes(val);
        else if (strcmp(key, "watch") == 0)            copy_str(cfg->watch, sizeof cfg->watch, val);
        else if (strcmp(key, "buzzer_gpio") == 0)      cfg->buzzer_gpio = val[0] ? atoi(val) : -1;
        else if (strcmp(key, "metar_url") == 0)        copy_str(cfg->metar_url, sizeof cfg->metar_url, val);
        else if (strcmp(key, "metar_station") == 0)    copy_str(cfg->metar_station, sizeof cfg->metar_station, val);
        else if (strcmp(key, "idle_after_s") == 0)     cfg->idle_after_s = atoi(val);
        else if (strcmp(key, "weather_view_s") == 0)   cfg->weather_view_s = atoi(val);
        else if (strcmp(key, "fb_dev") == 0)    copy_str(cfg->fb_dev, sizeof cfg->fb_dev, val);
        else if (strcmp(key, "touch_dev") == 0) copy_str(cfg->touch_dev, sizeof cfg->touch_dev, val);
        else
            fprintf(stderr, "%s:%d: unknown key '%s'\n", path, lineno, key);
    }
    fclose(f);

    if (isnan(cfg->lat) || isnan(cfg->lon)) {
        fprintf(stderr, "%s: lat and lon are required\n", path);
        return -1;
    }
    if (cfg->range_nm <= 0)
        cfg->range_nm = 10;
    /* The ranges a tap on empty scope steps through */
    if (rstep <= 0)
        rstep = 5;
    if (rmin <= 0)
        rmin = rstep;
    if (rmax < rmin)
        rmax = rmin;
    cfg->n_ranges = 0;
    for (double r = rmin; r <= rmax + 1e-6 && cfg->n_ranges < MAX_RANGES; r += rstep)
        cfg->ranges[cfg->n_ranges++] = r;
    if (cfg->range_nm < rmin || cfg->range_nm > rmax + 1e-6)
        cfg->range_nm = cfg->range_nm < rmin ? rmin : rmax;

    /* Drop sources without a URL; fill in the URL templates */
    int n = 0;
    for (int i = 0; i < cfg->n_sources; i++) {
        if (url_tmpl[i][0] == '\0') {
            fprintf(stderr, "%s: source '%s' has no url, ignoring it\n",
                    path, cfg->sources[i].name);
            continue;
        }
        source_config_t *d = &cfg->sources[n];
        if (n != i)
            *d = cfg->sources[i];
        if (d->poll_s < 1)
            d->poll_s = 1;
        expand_url(url_tmpl[i], cfg, d->url, sizeof d->url);
        n++;
    }
    cfg->n_sources = n;
    expand_url(weather_tmpl, cfg, cfg->weather_url, sizeof cfg->weather_url);

    /* Receiver stats: by default, stats.json next to the first source's aircraft.json */
    if (!stats_set) {
        const char *u = cfg->sources[0].url;
        const char *p = strstr(u, "aircraft.json");
        if (p && strlen(p) == strlen("aircraft.json"))
            snprintf(cfg->stats_url, sizeof cfg->stats_url, "%.*sstats.json", (int)(p - u), u);
    }
    if (n == 0) {
        fprintf(stderr, "%s: at least one [source] with a url is required\n", path);
        return -1;
    }
    return 0;
}

int config_save_value(const char *path, const char *key, const char *value)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    size_t cap = 8192, len = 0;
    char *out = malloc(cap);
    if (!out) {
        fclose(f);
        return -1;
    }
    out[0] = '\0';

    char line[768];
    bool done = false;
    while (fgets(line, sizeof line, f)) {
        char copy[768];
        snprintf(copy, sizeof copy, "%s", line);
        char *s = trim(copy);
        char *eq = strchr(s, '=');
        if (!done && *s != '#' && eq) {
            *eq = '\0';
            if (strcmp(trim(s), key) == 0) {
                /* Keep any comment at the end of the line, lined up as before */
                char comment[256] = "";
                char *old = strchr(line, '=');
                char *hash = old ? find_comment(old + 1) : NULL;
                int col = 0;
                if (hash) {
                    col = (int)(hash - line);
                    snprintf(comment, sizeof comment, "%s", hash);
                    comment[strcspn(comment, "\r\n")] = '\0';
                }
                int n = snprintf(line, sizeof line, "%s = %s", key, value);
                if (comment[0])
                    snprintf(line + n, sizeof line - (size_t)n, "%*s%s", col > n ? col - n : 1, "", comment);
                strncat(line, "\n", sizeof line - strlen(line) - 1);
                done = true;
            }
        }
        size_t n = strlen(line);
        if (len + n + 64 > cap) {
            cap *= 2;
            char *grown = realloc(out, cap);
            if (!grown) {
                free(out);
                fclose(f);
                return -1;
            }
            out = grown;
        }
        memcpy(out + len, line, n + 1);
        len += n;
    }
    fclose(f);
    if (!done)
        len += (size_t)snprintf(out + len, cap - len, "%s%s = %s\n",
                                len && out[len - 1] != '\n' ? "\n" : "", key, value);

    /* Rewrite in place so the file keeps its owner and permissions */
    f = fopen(path, "w");
    if (!f) {
        free(out);
        return -1;
    }
    size_t w = fwrite(out, 1, len, f);
    int rc = fclose(f);
    free(out);
    return w == len && rc == 0 ? 0 : -1;
}
