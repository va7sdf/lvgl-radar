#ifndef RADAR_PAGES_H
#define RADAR_PAGES_H

#include "config.h"
#include "lvgl.h"

/* Clock & units: segmented choices, applied and saved as you tap them.
   done_cb runs when Close is pressed. */
lv_obj_t *clockunits_create(radar_config_t *cfg, void (*done_cb)(void));
void clockunits_opened(void);

/* Data sources, software and fonts, with their licences. Tap to close. */
lv_obj_t *credits_create(void (*done_cb)(void));

#endif
