#ifndef RADAR_RECEIVER_H
#define RADAR_RECEIVER_H

#include "config.h"
#include "feed.h"
#include "lvgl.h"

#define RANGE_BINS 36       /* 10 degrees each */

typedef struct {
    bool   valid;
    double msg_per_s;
    double signal, noise, peak;     /* dBFS */
    double gain;                    /* dB */
    double strong_pct;              /* messages above -3 dBFS, % */
} receiver_stats_t;

/* Start fetching your dump1090's stats.json (cfg->stats_url) and load the
   saved range history (cfg->range_file). */
void receiver_start(const radar_config_t *cfg);

/* Feed in each new merged list; positions from your own receiver (the
   first source) build up the range plot. */
void receiver_track(const feed_snapshot_t *merged);

/* The stats screen: build once, refresh while shown. */
lv_obj_t *receiver_screen_create(void);
void receiver_refresh(void);

/* Parse stats.json; exposed for testing. */
bool receiver_parse_stats(const char *json, receiver_stats_t *out);

#endif
