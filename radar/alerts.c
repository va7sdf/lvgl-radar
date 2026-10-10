/* Emergency, military and watch-list alerts. */

#include "alerts.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define ALERT_REPEAT_S 1800
#define MAX_RECENT     64
#define MAX_WATCH      32

static const radar_config_t *cfg;
static char   watch[MAX_WATCH][16];
static int    n_watch;
static struct { char hex[8]; time_t at; } recent[MAX_RECENT];
static int    next_recent;
static char   reason_buf[64];

/* Compare ignoring case and dashes, so C-GSDZ matches CGSDZ */
static bool same(const char *a, const char *b, bool prefix)
{
    while (*a || *b) {
        if (*a == '-') { a++; continue; }
        if (*b == '-') { b++; continue; }
        if (!*b)
            return prefix;
        if (!*a || toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return true;
}

static bool watched(const aircraft_t *a)
{
    for (int i = 0; i < n_watch; i++) {
        char w[16];
        snprintf(w, sizeof w, "%s", watch[i]);
        size_t n = strlen(w);
        bool prefix = n > 0 && w[n - 1] == '*';
        if (prefix)
            w[n - 1] = '\0';
        if (same(a->hex, w, false) || same(a->reg, w, prefix) ||
            same(a->flight, w, prefix) || same(a->type, w, prefix))
            if (a->hex[0] && w[0])
                return true;
    }
    return false;
}

const char *alerts_reason(const aircraft_t *a)
{
    if (cfg->alert_emergency) {
        if (!strcmp(a->squawk, "7700")) return "7700 general emergency";
        if (!strcmp(a->squawk, "7600")) return "7600 radio failure";
        if (!strcmp(a->squawk, "7500")) return "7500 unlawful interference";
    }
    if (cfg->alert_military && a->military)
        return "military";
    if (watched(a)) {
        snprintf(reason_buf, sizeof reason_buf, "on your watch list");
        return reason_buf;
    }
    return NULL;
}

/* ---------- checking ---------- */

static bool recently(const char *hex, time_t now)
{
    for (int i = 0; i < MAX_RECENT; i++)
        if (!strcmp(recent[i].hex, hex) && now - recent[i].at < ALERT_REPEAT_S)
            return true;
    return false;
}

bool alerts_check(const feed_snapshot_t *merged, char hex[8])
{
    time_t now = time(NULL);
    const double coslat = cos(cfg->lat * M_PI / 180.0);
    for (int i = 0; i < merged->count; i++) {
        const aircraft_t *a = &merged->ac[i];
        double n = (a->lat - cfg->lat) * 60.0, e = (a->lon - cfg->lon) * 60.0 * coslat;
        if (hypot(n, e) > cfg->range_nm)
            continue;
        const char *why = alerts_reason(a);
        if (!why || recently(a->hex, now))
            continue;

        snprintf(recent[next_recent].hex, sizeof recent[next_recent].hex, "%s", a->hex);
        recent[next_recent].at = now;
        next_recent = (next_recent + 1) % MAX_RECENT;
        snprintf(hex, 8, "%s", a->hex);
        printf("alert: %s %s (%s)\n", a->flight[0] ? a->flight : a->hex, a->reg, why);
        return true;
    }
    return false;
}

void alerts_init(const radar_config_t *config)
{
    cfg = config;
    n_watch = 0;
    char list[512];
    snprintf(list, sizeof list, "%s", cfg->watch);
    for (char *tok = strtok(list, ", \t"); tok && n_watch < MAX_WATCH; tok = strtok(NULL, ", \t"))
        snprintf(watch[n_watch++], sizeof watch[0], "%s", tok);
}
