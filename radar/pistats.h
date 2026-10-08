#ifndef RADAR_PISTATS_H
#define RADAR_PISTATS_H

#include <stdbool.h>

typedef struct {
    double temp_c;          /* SoC temperature, NAN if unknown */
    double cpu_pct;         /* whole Pi, since the last call */
    double app_pct;         /* this program, since the last call (of one core) */
    double load1;
    long   mem_avail_mb, mem_total_mb;
    long   disk_free_mb;
    long   uptime_s;
    bool   throttle_known;
    unsigned throttled;     /* get_throttled bits */
    char   ip[48];          /* e.g. "wlan0 192.168.0.40" */
    int    wifi_dbm;        /* 0 if unknown */
    char   ssid[64];        /* Wi-Fi network name, empty if unknown */
    char   host[64];
} pistats_t;

/* Read everything; CPU percentages are measured since the previous call. */
void pistats_read(pistats_t *s);

/* "Under-voltage now", "OK", ... from the throttled bits */
const char *pistats_throttle_text(const pistats_t *s);

#endif
