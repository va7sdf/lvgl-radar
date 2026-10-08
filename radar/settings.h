#ifndef RADAR_SETTINGS_H
#define RADAR_SETTINGS_H

#include "config.h"
#include "lvgl.h"

/* Build the settings screen. close_cb is called when the user is done
   (Close button or timeout), after changes have been saved; open_page(0)
   asks for the clock & units page, open_page(1) for the credits. */
lv_obj_t *settings_create(radar_config_t *cfg, void (*close_cb)(void), void (*open_page)(int page));

/* Sync the controls with the current config; call before showing. */
void settings_opened(void);

/* Refresh the Pi statistics; call every second or two while shown. */
void settings_refresh(void);

/* Save anything changed to the config file; called by Close and timeouts. */
void settings_save(void);

#endif
