#ifndef RADAR_ALERTS_H
#define RADAR_ALERTS_H

#include <stdbool.h>

#include "config.h"
#include "feed.h"

/*
 * Aircraft worth interrupting for: emergency squawks, military aircraft
 * (as flagged by adsb.fi) and anything on your watch list. Each aircraft
 * alerts once, then again only after ALERT_REPEAT_S.
 */

void alerts_init(const radar_config_t *cfg);

/* Look for a new alert among aircraft within range. Returns true and fills
   hex if one should be shown now (and beeps the buzzer, if fitted). */
bool alerts_check(const feed_snapshot_t *merged, char hex[8]);

/* Why this aircraft is interesting, or NULL. */
const char *alerts_reason(const aircraft_t *a);

#endif
