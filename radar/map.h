#ifndef RADAR_MAP_H
#define RADAR_MAP_H

#include "config.h"
#include "lvgl.h"

/* Load cfg->map_file (made by make_map.py) and draw it onto the face
   canvas, clipped to the scope. Does nothing if map_file is empty; prints
   a note and carries on if the file can't be read. Map labels avoid the
   `reserved` screen areas (range labels, status line). */
void map_draw(lv_obj_t *canvas, const radar_config_t *cfg,
              int cx, int cy, int r_outer,
              const lv_area_t *reserved, int n_reserved);

/* A labelled point drawn on the scope: airport, vor, ndb, peak or town */
typedef struct {
    char   kind[16];
    char   label[64];       /* as drawn, e.g. "CYYJ 63'" */
    char   details[640];    /* lines for the info screen, '\n' separated */
    double lat, lon;
    int    x, y;            /* on screen */
} map_place_t;

/* Nearest point drawn within radius px of (x, y). Returns 1 if found. */
int map_hit(int x, int y, int radius, map_place_t *out);

#endif
