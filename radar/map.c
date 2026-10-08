/*
 * Outline map: shorelines from map.txt (see make_map.py), drawn once onto
 * the radar face underneath the rings, so it costs nothing per frame.
 *
 * map.txt format:
 *   # comment
 *   radius_nm 50          how far the map extends from the receiver
 *   > coast               start of a new line (coast, lake, island, ice, river)
 *   48.36708 -124.61508   latitude longitude
 *   ...
 *   @ airport 48.64720 -123.42779 CYYJ 63'    a labelled point: airport,
 *                                             vor, ndb, peak or town
 *
 * Points come most important first. Each gets its symbol; its label is
 * skipped if it would overlap one already placed, which keeps busy areas
 * readable at any range.
 */

#include "map.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BATCH 400           /* line segments per draw batch */
#define MAX_LABELS 256
#define MAX_PLACES 400

#if LV_FONT_MONTSERRAT_16
#define MAP_FONT (&lv_font_montserrat_16)
#else
#define MAP_FONT (&lv_font_montserrat_14)
#endif

typedef struct {
    lv_obj_t   *canvas;
    lv_layer_t  layer;
    int         queued;
    lv_color_t  colour;
    double      cx, cy, r;  /* clip circle */
    int         segments;
    lv_color_t  label_colour;
    lv_area_t   labels[MAX_LABELS];     /* label boxes placed so far */
    int         n_labels;
    int         points;
} drawer_t;

static void flush(drawer_t *d)
{
    if (d->queued) {
        lv_canvas_finish_layer(d->canvas, &d->layer);
        lv_canvas_init_layer(d->canvas, &d->layer);
        d->queued = 0;
    }
}

/* Clip the segment to the circle; returns false if nothing is left. */
static bool clip(const drawer_t *d, double *x0, double *y0, double *x1, double *y1)
{
    double ax = *x0 - d->cx, ay = *y0 - d->cy;
    double dx = *x1 - *x0, dy = *y1 - *y0;
    double a = dx * dx + dy * dy;
    double b = 2 * (ax * dx + ay * dy);
    double c = ax * ax + ay * ay - d->r * d->r;
    if (a == 0)
        return c <= 0;
    double disc = b * b - 4 * a * c;
    if (disc <= 0)
        return false;
    double s = sqrt(disc);
    double t0 = (-b - s) / (2 * a), t1 = (-b + s) / (2 * a);
    if (t0 < 0) t0 = 0;
    if (t1 > 1) t1 = 1;
    if (t0 >= t1)
        return false;
    double ox = *x0, oy = *y0;
    *x0 = ox + t0 * dx; *y0 = oy + t0 * dy;
    *x1 = ox + t1 * dx; *y1 = oy + t1 * dy;
    return true;
}

static void segment(drawer_t *d, double x0, double y0, double x1, double y1)
{
    if (!clip(d, &x0, &y0, &x1, &y1))
        return;
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = d->colour;
    l.width = 2;
    l.round_start = 1;
    l.round_end = 1;
    l.p1.x = (lv_value_precise_t)lround(x0);
    l.p1.y = (lv_value_precise_t)lround(y0);
    l.p2.x = (lv_value_precise_t)lround(x1);
    l.p2.y = (lv_value_precise_t)lround(y1);
    if (l.p1.x == l.p2.x && l.p1.y == l.p2.y)
        return;
    lv_draw_line(&d->layer, &l);
    d->segments++;
    if (++d->queued >= BATCH)
        flush(d);
}

/* ---------- labelled points ---------- */

static map_place_t places[MAX_PLACES];      /* points on the scope, for tapping */
static int         n_places;

int map_hit(int x, int y, int radius, map_place_t *out)
{
    int best = -1, best_d = radius * radius + 1;
    for (int i = 0; i < n_places; i++) {
        int dx = places[i].x - x, dy = places[i].y - y, dd = dx * dx + dy * dy;
        if (dd < best_d) {
            best = i;
            best_d = dd;
        }
    }
    if (best < 0)
        return 0;
    *out = places[best];
    return 1;
}

static bool kind_enabled(const radar_config_t *cfg, const char *kind)
{
    if (strcmp(kind, "airport") == 0)
        return cfg->show_airports;
    if (strcmp(kind, "vor") == 0 || strcmp(kind, "ndb") == 0)
        return cfg->show_navaids;
    if (strcmp(kind, "town") == 0)
        return cfg->show_towns;
    if (strcmp(kind, "peak") == 0)
        return cfg->show_peaks;
    return false;
}

static void symbol(drawer_t *d, const char *kind, int x, int y)
{
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    lv_area_t a;

    if (strcmp(kind, "airport") == 0) {            /* ring */
        r.bg_opa = LV_OPA_TRANSP;
        r.border_color = d->label_colour;
        r.border_width = 2;
        r.radius = LV_RADIUS_CIRCLE;
        a = (lv_area_t){ x - 6, y - 6, x + 6, y + 6 };
        lv_draw_rect(&d->layer, &r, &a);
    } else if (strcmp(kind, "vor") == 0) {         /* diamond */
        static const int dx[4] = { 0, 7, 0, -7 }, dy[4] = { -7, 0, 7, 0 };
        lv_draw_line_dsc_t l;
        lv_draw_line_dsc_init(&l);
        l.color = d->label_colour;
        l.width = 2;
        l.round_start = l.round_end = 1;
        for (int i = 0; i < 4; i++) {
            l.p1.x = x + dx[i];           l.p1.y = y + dy[i];
            l.p2.x = x + dx[(i + 1) % 4]; l.p2.y = y + dy[(i + 1) % 4];
            lv_draw_line(&d->layer, &l);
        }
    } else if (strcmp(kind, "ndb") == 0) {         /* small dot */
        r.bg_color = d->label_colour;
        r.bg_opa = LV_OPA_COVER;
        r.radius = LV_RADIUS_CIRCLE;
        a = (lv_area_t){ x - 3, y - 3, x + 3, y + 3 };
        lv_draw_rect(&d->layer, &r, &a);
    } else if (strcmp(kind, "peak") == 0) {        /* triangle */
        static const int dx[3] = { 0, 7, -7 }, dy[3] = { -7, 5, 5 };
        lv_draw_line_dsc_t l;
        lv_draw_line_dsc_init(&l);
        l.color = d->label_colour;
        l.width = 2;
        l.round_start = l.round_end = 1;
        for (int i = 0; i < 3; i++) {
            l.p1.x = x + dx[i];           l.p1.y = y + dy[i];
            l.p2.x = x + dx[(i + 1) % 3]; l.p2.y = y + dy[(i + 1) % 3];
            lv_draw_line(&d->layer, &l);
        }
    } else {                                       /* town: small square */
        r.bg_color = d->label_colour;
        r.bg_opa = LV_OPA_COVER;
        a = (lv_area_t){ x - 3, y - 3, x + 3, y + 3 };
        lv_draw_rect(&d->layer, &r, &a);
    }
}

static bool overlaps(const lv_area_t *a, const lv_area_t *b)
{
    return a->x1 <= b->x2 && b->x1 <= a->x2 && a->y1 <= b->y2 && b->y1 <= a->y2;
}

static void point(drawer_t *d, const char *kind, int x, int y, double lat, double lon,
                  const char *text, const char *details)
{
    double dx = x - d->cx, dy = y - d->cy;
    if (dx * dx + dy * dy > (d->r - 8) * (d->r - 8))
        return;                                    /* off the scope */

    symbol(d, kind, x, y);
    d->points++;
    if (n_places < MAX_PLACES) {
        map_place_t *p = &places[n_places++];
        snprintf(p->kind, sizeof p->kind, "%s", kind);
        snprintf(p->label, sizeof p->label, "%s", text);
        snprintf(p->details, sizeof p->details, "%s", details);
        p->lat = lat;
        p->lon = lon;
        p->x = x;
        p->y = y;
    }
    if (!text[0] || d->n_labels >= MAX_LABELS)
        return;

    /* Label to the right of the symbol, or to the left near the right edge */
    lv_point_t size;
    lv_text_get_size(&size, text, MAP_FONT, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_area_t box;
    box.x1 = x + 10;
    if (box.x1 + size.x > d->cx + d->r - 10)
        box.x1 = x - 10 - size.x;
    box.x2 = box.x1 + size.x;
    box.y1 = y - size.y / 2;
    box.y2 = box.y1 + size.y;

    lv_area_t symbol_box = { x - 8, y - 8, x + 8, y + 8 };
    for (int i = 0; i < d->n_labels; i++)
        if (overlaps(&box, &d->labels[i]) || overlaps(&symbol_box, &d->labels[i]))
            return;                                /* crowded: symbol only */

    lv_draw_label_dsc_t l;
    lv_draw_label_dsc_init(&l);
    l.text = text;
    l.text_local = 1;
    l.font = MAP_FONT;
    l.color = d->label_colour;
    lv_draw_label(&d->layer, &l, &box);
    d->labels[d->n_labels++] = box;

    if (++d->queued >= BATCH)
        flush(d);
}

void map_draw(lv_obj_t *canvas, const radar_config_t *cfg, int cx, int cy, int r_outer,
              const lv_area_t *reserved, int n_reserved)
{
    if (!cfg->map_file[0])
        return;

    FILE *f = fopen(cfg->map_file, "r");
    if (!f) {
        fprintf(stderr, "map: %s not found, drawing without a map "
                        "(run make_map.py to create one)\n", cfg->map_file);
        return;
    }

    drawer_t d = { 0 };
    d.canvas = canvas;
    d.colour = lv_color_hex(cfg->map_colour);
    d.cx = cx;
    d.cy = cy;
    d.r = r_outer - 3;              /* stay inside the outer ring line */
    d.label_colour = lv_color_hex(cfg->map_label_colour);
    /* Keep map labels off the range labels and status line */
    for (int i = 0; i < n_reserved && d.n_labels < MAX_LABELS; i++)
        d.labels[d.n_labels++] = reserved[i];
    int n_fixed = d.n_labels;
    lv_canvas_init_layer(canvas, &d.layer);

    const double coslat = cos(cfg->lat * M_PI / 180.0);
    const double scale = r_outer / cfg->range_nm;       /* pixels per nm */

    char line[1024];
    double px = 0, py = 0, radius = 0;
    n_places = 0;
    bool have_prev = false, skip_line = false;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (line[0] == '>') {                           /* new line starts */
            have_prev = false;
            skip_line = false;
            if (strstr(line, "river")) {
                skip_line = !cfg->show_rivers;
                d.colour = lv_color_hex(cfg->map_river_colour);
            } else {
                d.colour = lv_color_hex(cfg->map_colour);
            }
            continue;
        }
        if (line[0] == '@') {                           /* labelled point */
            char kind[16];
            double lat, lon;
            int used = 0;
            if (sscanf(line + 1, "%15s %lf %lf %n", kind, &lat, &lon, &used) < 3)
                continue;
            if (!kind_enabled(cfg, kind))
                continue;
            char *text = line + 1 + used;
            text[strcspn(text, "\r\n")] = '\0';
            /* "label|detail|detail": details become lines on the info screen */
            char *details = strchr(text, '|');
            if (details)
                *details++ = '\0';
            else
                details = "";
            for (char *c = details; *c; c++)
                if (*c == '|')
                    *c = '\n';
            double dlon = fmod(lon - cfg->lon + 540.0, 360.0) - 180.0;
            int x = (int)lround(cx + dlon * 60.0 * coslat * scale);
            int y = (int)lround(cy - (lat - cfg->lat) * 60.0 * scale);
            point(&d, kind, x, y, lat, lon, text, details);
            continue;
        }
        if (strncmp(line, "radius_nm", 9) == 0) {
            radius = atof(line + 9);
            continue;
        }
        double lat, lon;
        if (sscanf(line, "%lf %lf", &lat, &lon) != 2)
            continue;
        double dlon = fmod(lon - cfg->lon + 540.0, 360.0) - 180.0;
        double x = cx + dlon * 60.0 * coslat * scale;
        double y = cy - (lat - cfg->lat) * 60.0 * scale;
        if (have_prev && !skip_line)
            segment(&d, px, py, x, y);
        px = x;
        py = y;
        have_prev = true;
    }
    fclose(f);
    flush(&d);
    lv_canvas_finish_layer(canvas, &d.layer);

    printf("map: %s, %d segments and %d points (%d labelled) on screen\n",
           cfg->map_file, d.segments, d.points, d.n_labels - n_fixed);
    if (radius > 0 && radius < cfg->range_nm)
        fprintf(stderr, "map: %s only covers %g nm but range_nm is %g; "
                        "run make_map.py --radius %g\n",
                cfg->map_file, radius, cfg->range_nm, ceil(cfg->range_nm));
}
