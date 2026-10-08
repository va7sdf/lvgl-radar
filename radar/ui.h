#ifndef RADAR_UI_SCREENS_H
#define RADAR_UI_SCREENS_H

#include "config.h"
#include "feed.h"

/*
 * Screens around the radar:
 *   - tap aircraft: their details, swipe between them if several were
 *     under your finger; tap or wait to return
 *   - long-press an aircraft: follow it (again to stop)
 *   - tap a map point: airport/navaid/peak/town info, live airport board
 *   - tap empty scope: next range
 *   - swipe left (or idle with no aircraft): clock, weather and METAR
 *   - swipe right: your receiver's stats and range plot
 *   - swipe down: list of aircraft in range
 *   - swipe up: settings (and from there clock & units, credits)
 *   - alerts (emergencies, military, watch list) open the details
 */

/* Call after radar_ui_create(); the active screen is the radar. */
void ui_create(radar_config_t *cfg);

/* Latest merged aircraft list, for live details. */
void ui_update(const feed_snapshot_t *merged);

#endif
