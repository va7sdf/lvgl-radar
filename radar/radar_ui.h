#ifndef RADAR_UI_H
#define RADAR_UI_H

#include <stdbool.h>

#include "config.h"
#include "feed.h"

/* Build the static radar face on the active screen. */
void radar_ui_create(const radar_config_t *cfg);

/* Place, move or hide blips from a fresh snapshot. */
void radar_ui_update(const feed_snapshot_t *snap);

/* Store the latest reports. With the sweep on, aircraft only move on
   screen when the sweep passes them (see radar_ui_sweep_step). */

/* Per-frame work: paint aircraft the sweep has just passed, fade the rest
   (phosphor), redraw trails if needed. Runs from a timer; exposed for testing. */
void radar_ui_sweep_step(void);

/* Bottom status line: colour-coded count per source (plus credit, if set).
   source_ok has one entry per configured source. */
void radar_ui_set_status(const bool *source_ok);

/* Apply changed sweep, frame rate or track history settings. */
void radar_ui_apply_settings(void);

/* cfg->range_nm changed: redraw the face and move everything to scale. */
void radar_ui_range_changed(void);

/* Number of aircraft currently drawn on the scope. */
int radar_ui_shown(void);

/* Aircraft drawn within `radius` px of (x, y), nearest first. Fills up to
   `max` hex codes and returns how many. */
int radar_ui_hits(int x, int y, int radius, char hex[][8], int max);

/* Follow this aircraft (ring, distance and closing speed in its label), or
   stop following if it already is, or if hex is NULL. Returns true if now
   following. */
bool radar_ui_follow(const char *hex);

#endif
