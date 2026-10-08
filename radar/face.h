#ifndef RADAR_FACE_H
#define RADAR_FACE_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "lvgl.h"

#define FACE_W        1080
#define FACE_H        1080
#define FACE_CX       540       /* screen centre */
#define FACE_CY       540
#define FACE_R_OUTER  500       /* outer ring radius in pixels */

/* Draw the static face (disc, rings, axes, labels) onto a full-screen
   canvas and start the sweep animation if it's enabled in the config. */
void face_create(lv_obj_t *parent, const radar_config_t *cfg);

/* Move the sweep to where it should be at time now_ms and redraw the part
   of the canvas that changed. Called from a timer; exposed for testing. */
void face_sweep_step(uint32_t now_ms);

/* Re-read sweep settings (period, trail width, fps) from the config. */
void face_apply_sweep(void);

/* Redraw the face after a change of range. */
void face_redraw(void);

/* Average milliseconds to draw one sweep frame (0 if not drawing). */
double face_sweep_draw_ms(void);

/* Pause or resume drawing the sweep, e.g. while another screen is shown.
   The sweep keeps turning (and painting aircraft) either way. */
void face_set_visible(bool on);

/* Current bearing of the sweep's leading edge (0..65535 = 0..360 deg,
   clockwise from north). Returns false if the sweep is off. */
bool face_sweep_angle(uint16_t *angle);

/* Redraw the overlay (aircraft trails) that sits between the face and the
   sweep. Call begin, draw onto the layer, then end with the screen area
   that changed (old and new overlay combined), or NULL for none. */
void face_overlay_begin(lv_layer_t *layer);
void face_overlay_end(lv_layer_t *layer, const lv_area_t *dirty);

#endif
