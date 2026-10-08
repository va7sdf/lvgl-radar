#ifndef RADAR_LIST_H
#define RADAR_LIST_H

#include "config.h"
#include "feed.h"
#include "lvgl.h"

/* Every aircraft within range, nearest first: callsign, type, altitude,
   speed, distance and bearing. Scrolls; tapping a row calls open_cb with
   that aircraft's hex code. */
lv_obj_t *list_create(const radar_config_t *cfg, void (*open_cb)(const char *hex));

/* Refresh from the latest merged data; call every couple of seconds while shown. */
void list_refresh(const feed_snapshot_t *merged);

#endif
