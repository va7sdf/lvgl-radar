/* JSON parsing for dump1090 aircraft.json and ADSBx-v2 style APIs (adsb.fi). */

#include "feed.h"

#include <cjson/cJSON.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#define MAX_POS_AGE_S 60.0

static void copy_trimmed(char *dst, size_t size, const char *src)
{
    while (*src == ' ')
        src++;
    size_t n = strlen(src);
    while (n > 0 && src[n - 1] == ' ')
        n--;
    if (n >= size)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool get_number(const cJSON *obj, const char *key, double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item))
        return false;
    *out = item->valuedouble;
    return true;
}

static double get_or_nan(const cJSON *obj, const char *key)
{
    double v;
    return get_number(obj, key, &v) ? v : NAN;
}

static const char *get_string(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

int feed_parse(const char *json, const char *array_key, feed_snapshot_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;

    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, array_key);
    if (!cJSON_IsArray(list)) {
        cJSON_Delete(root);
        return -1;
    }

    out->count = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        if (out->count >= FEED_MAX_AC)
            break;

        double lat, lon, seen_pos = NAN;
        if (!get_number(item, "lat", &lat) || !get_number(item, "lon", &lon))
            continue;
        if (get_number(item, "seen_pos", &seen_pos) && seen_pos > MAX_POS_AGE_S)
            continue;
        if (!get_number(item, "seen_pos", &seen_pos))
            seen_pos = NAN;

        const char *hex = get_string(item, "hex");
        if (!hex)
            continue;

        aircraft_t *a = &out->ac[out->count];
        memset(a, 0, sizeof *a);
        a->lat = lat;
        a->lon = lon;
        copy_trimmed(a->hex, sizeof a->hex, hex);
        for (char *p = a->hex; *p; p++)
            *p = (char)toupper((unsigned char)*p);

        const char *s;
        if ((s = get_string(item, "flight")))
            copy_trimmed(a->flight, sizeof a->flight, s);
        if ((s = get_string(item, "squawk")))
            copy_trimmed(a->squawk, sizeof a->squawk, s);

        /* alt_baro is a number, or the string "ground" */
        double v;
        const cJSON *alt = cJSON_GetObjectItemCaseSensitive(item, "alt_baro");
        if (cJSON_IsNumber(alt)) {
            a->alt_ft = alt->valueint;
            a->alt_valid = true;
        } else if (cJSON_IsString(alt) && strcmp(alt->valuestring, "ground") == 0) {
            a->on_ground = true;
        }

        if (get_number(item, "track", &v)) {
            a->track = v;
            a->track_valid = true;
        }

        if ((s = get_string(item, "r")))
            copy_trimmed(a->reg, sizeof a->reg, s);
        if ((s = get_string(item, "t")))
            copy_trimmed(a->type, sizeof a->type, s);
        if ((s = get_string(item, "desc")))
            copy_trimmed(a->desc, sizeof a->desc, s);
        if ((s = get_string(item, "ownOp")))
            copy_trimmed(a->owner, sizeof a->owner, s);
        if ((s = get_string(item, "category")))
            copy_trimmed(a->category, sizeof a->category, s);
        double flags;
        a->military = get_number(item, "dbFlags", &flags) && ((int)flags & 1);
        a->gs = get_or_nan(item, "gs");
        a->vrate = get_or_nan(item, "baro_rate");
        if (isnan(a->vrate))
            a->vrate = get_or_nan(item, "geom_rate");
        a->sel_alt = get_or_nan(item, "nav_altitude_mcp");
        if (isnan(a->sel_alt))
            a->sel_alt = get_or_nan(item, "nav_altitude_fms");
        a->alt_geom = get_or_nan(item, "alt_geom");
        a->rssi = get_or_nan(item, "rssi");
        a->seen_pos = seen_pos;

        out->count++;
    }

    cJSON_Delete(root);
    return 0;
}
