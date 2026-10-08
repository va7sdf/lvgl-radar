/* Aircraft blips and status line, drawn over the radar face. */

#include "radar_ui.h"

#include "face.h"
#include "units.h"
#include "lvgl.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DOT_SIZE  14
#define VEC_LEN   26       /* track vector length in pixels */
#define VEC_BOX   60       /* line object is VEC_BOX x VEC_BOX, centred on the blip */

#define COL_TEXT_DIM  lv_color_hex(0x3C9C54)
#define COL_GROUND    lv_color_hex(0x8A8A8A)
#define COL_EMERG     lv_color_hex(0xFF3B30)
#define COL_LABEL     lv_color_hex(0xC8FFD0)

#define TRAIL_MAX       64      /* history points kept per aircraft */
#define TRAIL_MIN_PX    6       /* only add a point once it has moved this far */
#define TRAIL_OPA_MAX   210     /* newest part of a trail */

/* Phosphor: opacity just after the sweep passes, and what it fades to */
#define DOT_OPA_MIN     70
#define LABEL_OPA_MIN   150

typedef struct {
    double lat, lon;
    double t;                       /* seconds, from now_s() */
} trail_pt_t;

typedef struct {
    char               hex[8];
    bool               used;

    /* Latest report from the feed */
    aircraft_t         data;
    bool               in_data;     /* in the latest data and within range */
    uint16_t           data_bearing;

    /* What's on screen: only changes when the sweep passes ("painted") */
    bool               shown;
    double             lat, lon;
    uint16_t           bearing;     /* 0..65535 = 0..360 deg from the station */
    lv_color_t         colour;
    uint8_t            glow;        /* last phosphor level applied */
    lv_obj_t          *dot;
    lv_obj_t          *vec;
    lv_obj_t          *label;
    lv_point_precise_t pts[2];      /* must outlive the line object */
    trail_pt_t         trail[TRAIL_MAX];   /* oldest first */
    int                trail_n;
} blip_t;

#define TRAIL_REDRAW_MS 250     /* batch trail redraws at most this often */

static const radar_config_t *cfg;
static blip_t    blips[FEED_MAX_AC];
static lv_obj_t *status_label;
static int       in_range[MAX_SOURCES];
static bool      sweep_on;

/* Following one aircraft (long-press) */
static char      follow_hex[8];
static lv_obj_t *follow_ring;
static double    follow_prev_nm, follow_prev_t;
static double    follow_closing_kt = NAN;
static lv_timer_t *step_timer;
static uint16_t  last_sweep;
static bool      last_sweep_valid;
static lv_area_t trail_area;        /* screen area covered by the trails last drawn */
static bool      trail_area_valid;
static bool      trails_dirty;
static uint32_t  trails_drawn_ms;

static void sweep_timer_cb(lv_timer_t *t);

static double now_s(void)
{
    return lv_tick_get() / 1000.0;
}

static uint16_t bearing_of(double x, double y)
{
    double b = atan2(x - FACE_CX, FACE_CY - y) / (2.0 * M_PI);   /* 0 = north, clockwise */
    if (b < 0)
        b += 1.0;
    return (uint16_t)((uint32_t)lround(b * 65536.0) & 0xFFFF);
}

static lv_obj_t *make_plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

/* Station-relative position in pixels. Flat-earth approximation: fine at radar ranges. */
static void to_screen(double lat, double lon, double *x, double *y, double *dist_nm)
{
    double north = (lat - cfg->lat) * 60.0;
    double east  = (lon - cfg->lon) * 60.0 * cos(cfg->lat * M_PI / 180.0);
    double scale = FACE_R_OUTER / cfg->range_nm;        /* pixels per nm */
    *x = FACE_CX + east * scale;
    *y = FACE_CY - north * scale;
    if (dist_nm)
        *dist_nm = sqrt(north * north + east * east);
}

/* ---------- phosphor ---------- */

static void apply_glow(blip_t *b, uint8_t glow)
{
    uint32_t dot_opa = DOT_OPA_MIN + (255 - DOT_OPA_MIN) * glow / 255;
    uint32_t lbl_opa = LABEL_OPA_MIN + (255 - LABEL_OPA_MIN) * glow / 255;
    lv_obj_set_style_bg_opa(b->dot, (lv_opa_t)dot_opa, 0);
    lv_obj_set_style_line_opa(b->vec, (lv_opa_t)dot_opa, 0);
    lv_obj_set_style_text_opa(b->label, (lv_opa_t)lbl_opa, 0);
    b->glow = glow;
}

/* ---------- trails ---------- */

static void add_trail_point(blip_t *b, double now)
{
    /* Forget history older than trail_s */
    int drop = 0;
    while (drop < b->trail_n && now - b->trail[drop].t > cfg->trail_s)
        drop++;
    if (drop) {
        memmove(b->trail, b->trail + drop, sizeof(trail_pt_t) * (size_t)(b->trail_n - drop));
        b->trail_n -= drop;
    }

    /* Only record a new point once the aircraft has visibly moved */
    if (b->trail_n > 0) {
        double x0, y0, x1, y1;
        const trail_pt_t *last = &b->trail[b->trail_n - 1];
        to_screen(last->lat, last->lon, &x0, &y0, NULL);
        to_screen(b->lat, b->lon, &x1, &y1, NULL);
        if (hypot(x1 - x0, y1 - y0) < TRAIL_MIN_PX)
            return;
    }
    if (b->trail_n == TRAIL_MAX) {
        memmove(b->trail, b->trail + 1, sizeof(trail_pt_t) * (TRAIL_MAX - 1));
        b->trail_n--;
    }
    b->trail[b->trail_n++] = (trail_pt_t){ b->lat, b->lon, now };
}

static void grow_area(lv_area_t *a, bool *valid, double x, double y)
{
    int ix = (int)lround(x), iy = (int)lround(y);
    if (!*valid) {
        a->x1 = a->x2 = ix;
        a->y1 = a->y2 = iy;
        *valid = true;
        return;
    }
    if (ix < a->x1) a->x1 = ix;
    if (ix > a->x2) a->x2 = ix;
    if (iy < a->y1) a->y1 = iy;
    if (iy > a->y2) a->y2 = iy;
}

static void draw_segment(lv_layer_t *layer, double x0, double y0, double x1, double y1,
                         lv_color_t colour, lv_opa_t opa)
{
    if (opa < 4)
        return;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = colour;
    d.width = 2;
    d.opa = opa;
    d.p1.x = (lv_value_precise_t)lround(x0);
    d.p1.y = (lv_value_precise_t)lround(y0);
    d.p2.x = (lv_value_precise_t)lround(x1);
    d.p2.y = (lv_value_precise_t)lround(y1);
    lv_draw_line(layer, &d);
}

/* Redraw every trail onto the face, fading each segment with age */
static void draw_trails(double now)
{
    lv_area_t area = { 0 };
    bool valid = false;

    for (int i = 0; i < FEED_MAX_AC; i++) {
        blip_t *b = &blips[i];
        if (!b->shown || b->trail_n == 0)
            continue;
        double x, y;
        for (int k = 0; k < b->trail_n; k++) {
            to_screen(b->trail[k].lat, b->trail[k].lon, &x, &y, NULL);
            grow_area(&area, &valid, x, y);
        }
        to_screen(b->lat, b->lon, &x, &y, NULL);
        grow_area(&area, &valid, x, y);
    }

    if (!valid && !trail_area_valid)
        return;                     /* nothing before, nothing now */

    /* Redraw where the old trails were as well as the new ones */
    lv_area_t dirty = area;
    if (valid && trail_area_valid) {
        if (trail_area.x1 < dirty.x1) dirty.x1 = trail_area.x1;
        if (trail_area.y1 < dirty.y1) dirty.y1 = trail_area.y1;
        if (trail_area.x2 > dirty.x2) dirty.x2 = trail_area.x2;
        if (trail_area.y2 > dirty.y2) dirty.y2 = trail_area.y2;
    } else if (!valid) {
        dirty = trail_area;
    }
    dirty.x1 -= 3; dirty.y1 -= 3; dirty.x2 += 3; dirty.y2 += 3;

    lv_layer_t layer;
    face_overlay_begin(&layer);
    for (int i = 0; i < FEED_MAX_AC; i++) {
        blip_t *b = &blips[i];
        if (!b->shown || b->trail_n == 0)
            continue;

        double px, py;
        to_screen(b->trail[0].lat, b->trail[0].lon, &px, &py, NULL);
        for (int k = 1; k <= b->trail_n; k++) {
            double x, y;
            double age;
            if (k < b->trail_n) {
                to_screen(b->trail[k].lat, b->trail[k].lon, &x, &y, NULL);
                age = now - b->trail[k].t;
            } else {                /* last stretch up to where the aircraft is now */
                to_screen(b->lat, b->lon, &x, &y, NULL);
                age = 0;
            }
            double fade = 1.0 - age / cfg->trail_s;
            if (fade < 0)
                fade = 0;
            draw_segment(&layer, px, py, x, y, b->colour, (lv_opa_t)lround(TRAIL_OPA_MAX * fade));
            px = x;
            py = y;
        }
    }
    face_overlay_end(&layer, &dirty);

    trail_area = area;
    trail_area_valid = valid;
    trails_dirty = false;
    trails_drawn_ms = lv_tick_get();
}

void radar_ui_create(const radar_config_t *config)
{
    cfg = config;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Rings, compass and sweep live on one canvas at the bottom */
    face_create(scr, cfg);

    status_label = make_label(scr, &lv_font_montserrat_20, COL_TEXT_DIM);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    /* Dark backing so the text stays readable as the sweep passes under it */
    lv_obj_set_style_bg_color(status_label, lv_color_hex(0x031A09), 0);
    lv_obj_set_style_bg_opa(status_label, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(status_label, 10, 0);
    lv_obj_set_style_pad_ver(status_label, 4, 0);
    lv_obj_set_style_radius(status_label, 8, 0);
    lv_label_set_recolor(status_label, true);
    lv_label_set_text(status_label, "waiting for data");
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -70);

    follow_ring = make_plain(scr);
    lv_obj_set_size(follow_ring, 44, 44);
    lv_obj_set_style_radius(follow_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(follow_ring, lv_color_white(), 0);
    lv_obj_set_style_border_width(follow_ring, 3, 0);
    lv_obj_add_flag(follow_ring, LV_OBJ_FLAG_HIDDEN);

    radar_ui_apply_settings();
}

/* ---------- blips ---------- */

static blip_t *find_or_alloc(const char *hex)
{
    blip_t *free_slot = NULL;
    for (int i = 0; i < FEED_MAX_AC; i++) {
        if (blips[i].used && strcmp(blips[i].hex, hex) == 0)
            return &blips[i];
        if (!blips[i].used && !free_slot)
            free_slot = &blips[i];
    }
    if (!free_slot)
        return NULL;

    blip_t *b = free_slot;
    lv_obj_t *scr = lv_screen_active();
    if (!b->dot) {                  /* objects are created once and reused */
        b->vec = lv_line_create(scr);
        lv_obj_set_size(b->vec, VEC_BOX, VEC_BOX);
        lv_obj_set_style_line_width(b->vec, 3, 0);
        lv_obj_set_style_line_rounded(b->vec, true, 0);
        lv_obj_clear_flag(b->vec, LV_OBJ_FLAG_CLICKABLE);

        b->dot = make_plain(scr);
        lv_obj_set_size(b->dot, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(b->dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(b->dot, LV_OPA_COVER, 0);

        b->label = make_label(scr, &lv_font_montserrat_20, COL_LABEL);
    }
    /* Keep it hidden until the sweep first paints it */
    lv_obj_add_flag(b->dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(b->vec, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(b->label, LV_OBJ_FLAG_HIDDEN);
    snprintf(b->hex, sizeof b->hex, "%s", hex);
    b->used = true;
    b->shown = false;
    b->trail_n = 0;
    return b;
}

/* Remove an aircraft from the screen, tail and all, and free its slot */
static void vanish(blip_t *b)
{
    if (follow_hex[0] && strcmp(follow_hex, b->hex) == 0) {
        follow_hex[0] = '\0';
        lv_obj_add_flag(follow_ring, LV_OBJ_FLAG_HIDDEN);
    }
    if (b->shown && b->trail_n > 0)
        trails_dirty = true;
    b->used = false;
    b->shown = false;
    b->in_data = false;
    b->trail_n = 0;
    b->hex[0] = '\0';
    lv_obj_add_flag(b->dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(b->vec, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(b->label, LV_OBJ_FLAG_HIDDEN);
}

static bool is_emergency(const char *squawk)
{
    return strcmp(squawk, "7500") == 0 ||
           strcmp(squawk, "7600") == 0 ||
           strcmp(squawk, "7700") == 0;
}

/* Move the on-screen blip to the latest reported position */
static void paint(blip_t *b)
{
    const aircraft_t *a = &b->data;
    double fx, fy;
    to_screen(a->lat, a->lon, &fx, &fy, NULL);
    int x = (int)lround(fx);
    int y = (int)lround(fy);

    b->lat = a->lat;
    b->lon = a->lon;
    b->bearing = b->data_bearing;

    uint32_t src_rgb = cfg->sources[a->source < cfg->n_sources ? a->source : 0].colour;
    bool emerg = is_emergency(a->squawk);
    b->colour = emerg         ? COL_EMERG
              : a->on_ground  ? COL_GROUND
              :                 lv_color_hex(src_rgb);

    lv_obj_set_pos(b->dot, x - DOT_SIZE / 2, y - DOT_SIZE / 2);
    lv_obj_set_style_bg_color(b->dot, b->colour, 0);
    lv_obj_clear_flag(b->dot, LV_OBJ_FLAG_HIDDEN);

    if (a->track_valid && !a->on_ground) {
        double t = a->track * M_PI / 180.0;
        b->pts[0].x = VEC_BOX / 2;
        b->pts[0].y = VEC_BOX / 2;
        b->pts[1].x = VEC_BOX / 2 + (lv_value_precise_t)lround(sin(t) * VEC_LEN);
        b->pts[1].y = VEC_BOX / 2 - (lv_value_precise_t)lround(cos(t) * VEC_LEN);
        lv_line_set_points(b->vec, b->pts, 2);
        lv_obj_set_style_line_color(b->vec, b->colour, 0);
        lv_obj_set_pos(b->vec, x - VEC_BOX / 2, y - VEC_BOX / 2);
        lv_obj_clear_flag(b->vec, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(b->vec, LV_OBJ_FLAG_HIDDEN);
    }

    char alt[12];
    if (a->on_ground)
        snprintf(alt, sizeof alt, "GND");
    else if (a->alt_valid)
        snprintf(alt, sizeof alt, "%03d", a->alt_ft < 0 ? 0 : (a->alt_ft + 50) / 100);
    else
        snprintf(alt, sizeof alt, "---");

    const char *name = a->flight[0] ? a->flight : a->hex;
    char extra[64] = "";
    bool followed = follow_hex[0] && strcmp(follow_hex, b->hex) == 0;
    if (followed) {
        /* Distance from you, and how fast that's changing */
        double north = (a->lat - cfg->lat) * 60.0;
        double east = (a->lon - cfg->lon) * 60.0 * cos(cfg->lat * M_PI / 180.0);
        double nm = hypot(north, east), t = now_s();
        if (follow_prev_t > 0 && t - follow_prev_t >= 1)
            follow_closing_kt = (follow_prev_nm - nm) / ((t - follow_prev_t) / 3600.0);
        follow_prev_nm = nm;
        follow_prev_t = t;
        char d[24], v[24];
        fmt_dist(d, sizeof d, nm);
        if (isnan(follow_closing_kt) || fabs(follow_closing_kt) < 5) {
            snprintf(extra, sizeof extra, "\n%s", d);
        } else {
            fmt_speed(v, sizeof v, fabs(follow_closing_kt));
            snprintf(extra, sizeof extra, "\n%s, %s %s", d,
                     follow_closing_kt > 0 ? "closing" : "opening", v);
        }
        lv_obj_set_pos(follow_ring, x - 22, y - 22);
        lv_obj_clear_flag(follow_ring, LV_OBJ_FLAG_HIDDEN);
    }
    if (emerg)
        lv_label_set_text_fmt(b->label, "%s\n%s  %s%s", name, alt, a->squawk, extra);
    else
        lv_label_set_text_fmt(b->label, "%s\n%s%s", name, alt, extra);
    lv_obj_set_style_text_color(b->label, emerg ? COL_EMERG : followed ? lv_color_white() : COL_LABEL, 0);

    /* Put the label on the opposite side to the track vector */
    bool heading_east = a->track_valid && !a->on_ground &&
                        a->track > 0 && a->track < 180;
    lv_obj_set_style_text_align(b->label,
                                heading_east ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_clear_flag(b->label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align_to(b->label, b->dot,
                    heading_east ? LV_ALIGN_OUT_LEFT_MID : LV_ALIGN_OUT_RIGHT_MID,
                    heading_east ? -8 : 8, 0);

    b->shown = true;
    apply_glow(b, 255);
    if (cfg->trail_s > 0) {
        add_trail_point(b, now_s());
        trails_dirty = true;
    }
}

void radar_ui_update(const feed_snapshot_t *snap)
{
    for (int i = 0; i < FEED_MAX_AC; i++)
        blips[i].in_data = false;
    memset(in_range, 0, sizeof in_range);

    for (int i = 0; i < snap->count; i++) {
        const aircraft_t *a = &snap->ac[i];

        double fx, fy, dist;
        to_screen(a->lat, a->lon, &fx, &fy, &dist);
        if (dist > cfg->range_nm)
            continue;               /* outside the scope: its blip and tail get removed */

        blip_t *b = find_or_alloc(a->hex);
        if (!b)
            break;                                      /* pool exhausted */
        b->data = *a;
        b->in_data = true;
        b->data_bearing = bearing_of(fx, fy);
        if (a->source < MAX_SOURCES)
            in_range[a->source]++;
    }

    for (int i = 0; i < FEED_MAX_AC; i++) {
        blip_t *b = &blips[i];
        if (!b->used)
            continue;
        if (!sweep_on)
            b->in_data ? paint(b) : vanish(b);          /* no sweep: update at once */
        else if (!b->in_data && !b->shown)
            vanish(b);                                  /* never painted: just forget it */
    }

    if (!sweep_on && trails_dirty)
        draw_trails(now_s());
}

/* Each frame: paint (or remove) whatever the sweep just passed over, fade
   everything else like phosphor, and redraw the trails if they changed. */
void radar_ui_sweep_step(void)
{
    uint16_t sweep;
    if (sweep_on && face_sweep_angle(&sweep)) {
        if (!last_sweep_valid) {
            last_sweep = sweep;
            last_sweep_valid = true;
        }
        uint16_t span = (uint16_t)(sweep - last_sweep);

        for (int i = 0; i < FEED_MAX_AC; i++) {
            blip_t *b = &blips[i];
            if (!b->used)
                continue;

            /* Did the sweep cross the aircraft's bearing since last frame? */
            uint16_t target = b->in_data ? b->data_bearing : b->bearing;
            uint16_t d = (uint16_t)(target - last_sweep);
            if (span && d >= 1 && d <= span) {
                if (b->in_data)
                    paint(b);
                else
                    vanish(b);
                continue;
            }

            if (b->shown) {
                double f = (uint16_t)(sweep - b->bearing) / 65536.0;   /* 0 = just painted */
                uint8_t glow = (uint8_t)lround(255.0 * (1.0 - f) * (1.0 - f));
                if (abs((int)glow - (int)b->glow) >= 6)     /* skip invisible changes */
                    apply_glow(b, glow);
            }
        }
        last_sweep = sweep;
    }

    if (trails_dirty && cfg->trail_s > 0 && lv_tick_get() - trails_drawn_ms >= TRAIL_REDRAW_MS)
        draw_trails(now_s());
}

static void sweep_timer_cb(lv_timer_t *t)
{
    (void)t;
    radar_ui_sweep_step();
}

/* ---------- live settings ---------- */

void radar_ui_apply_settings(void)
{
    face_apply_sweep();
    bool was_on = sweep_on;
    sweep_on = cfg->sweep_trail_deg > 0 && cfg->sweep_period_s > 0;
    last_sweep_valid = false;
    int fps = cfg->sweep_fps > 0 ? cfg->sweep_fps : 20;
    if (!step_timer)
        step_timer = lv_timer_create(sweep_timer_cb, (uint32_t)(1000 / fps), NULL);
    else
        lv_timer_set_period(step_timer, (uint32_t)(1000 / fps));

    /* Sweep switched off: show everything where it is now */
    if (was_on && !sweep_on)
        for (int i = 0; i < FEED_MAX_AC; i++)
            if (blips[i].used) {
                if (blips[i].in_data)
                    paint(&blips[i]);
                else
                    vanish(&blips[i]);
            }
    if (cfg->trail_s <= 0)
        for (int i = 0; i < FEED_MAX_AC; i++)
            blips[i].trail_n = 0;
    trails_dirty = true;
}

void radar_ui_range_changed(void)
{
    face_redraw();
    trail_area_valid = false;           /* the face redraw already cleared old trails */

    for (int i = 0; i < FEED_MAX_AC; i++) {
        blip_t *b = &blips[i];
        if (!b->used)
            continue;
        double fx, fy, dist;
        to_screen(b->data.lat, b->data.lon, &fx, &fy, &dist);
        b->data_bearing = bearing_of(fx, fy);
        if (dist > cfg->range_nm) {
            vanish(b);
            continue;
        }
        if (!b->shown)
            continue;
        /* Same reported position, new scale */
        to_screen(b->lat, b->lon, &fx, &fy, NULL);
        int x = (int)lround(fx), y = (int)lround(fy);
        b->bearing = bearing_of(fx, fy);
        lv_obj_set_pos(b->dot, x - DOT_SIZE / 2, y - DOT_SIZE / 2);
        lv_obj_set_pos(b->vec, x - VEC_BOX / 2, y - VEC_BOX / 2);
        if (follow_hex[0] && strcmp(follow_hex, b->hex) == 0)
            lv_obj_set_pos(follow_ring, x - 22, y - 22);
        bool right = lv_obj_get_style_text_align(b->label, 0) == LV_TEXT_ALIGN_RIGHT;
        lv_obj_align_to(b->label, b->dot, right ? LV_ALIGN_OUT_LEFT_MID : LV_ALIGN_OUT_RIGHT_MID,
                        right ? -8 : 8, 0);
    }
    trails_dirty = true;
    if (cfg->trail_s > 0)
        draw_trails(now_s());
}

/* ---------- following ---------- */

bool radar_ui_follow(const char *hex)
{
    bool same = hex && follow_hex[0] && strcmp(hex, follow_hex) == 0;
    /* Restore the previous one's plain label at its next paint */
    follow_hex[0] = '\0';
    lv_obj_add_flag(follow_ring, LV_OBJ_FLAG_HIDDEN);
    follow_prev_t = 0;
    follow_closing_kt = NAN;
    if (!hex || same)
        return false;                   /* long-press again to stop following */

    snprintf(follow_hex, sizeof follow_hex, "%s", hex);
    for (int i = 0; i < FEED_MAX_AC; i++)
        if (blips[i].used && blips[i].shown && !strcmp(blips[i].hex, hex)) {
            paint(&blips[i]);           /* show the ring and distance now */
            break;
        }
    return true;
}

/* ---------- touch ---------- */

int radar_ui_shown(void)
{
    int n = 0;
    for (int i = 0; i < FEED_MAX_AC; i++)
        n += blips[i].used && blips[i].shown;
    return n;
}

int radar_ui_hits(int x, int y, int radius, char hex[][8], int max)
{
    int idx[FEED_MAX_AC], d2[FEED_MAX_AC], n = 0;
    for (int i = 0; i < FEED_MAX_AC; i++) {
        const blip_t *b = &blips[i];
        if (!b->used || !b->shown)
            continue;
        int bx = lv_obj_get_x(b->dot) + DOT_SIZE / 2 - x;
        int by = lv_obj_get_y(b->dot) + DOT_SIZE / 2 - y;
        int dd = bx * bx + by * by;
        if (dd > radius * radius)
            continue;
        /* insertion sort, nearest first */
        int k = n++;
        while (k > 0 && d2[k - 1] > dd) {
            idx[k] = idx[k - 1];
            d2[k] = d2[k - 1];
            k--;
        }
        idx[k] = i;
        d2[k] = dd;
    }
    if (n > max)
        n = max;
    for (int k = 0; k < n; k++)
        snprintf(hex[k], 8, "%s", blips[idx[k]].hex);
    return n;
}

void radar_ui_set_status(const bool *source_ok)
{
    char text[256];
    int n = 0;
    bool any_ok = false;
    for (int s = 0; s < cfg->n_sources; s++)
        any_ok |= source_ok[s];

    if (!any_ok) {
        n += snprintf(text, sizeof text, "#FF3B30 no data#");
    } else {
        /* One colour-coded count per source, e.g. "local 2   adsb.fi 3" */
        for (int s = 0; s < cfg->n_sources && n < (int)sizeof text; s++) {
            const source_config_t *src = &cfg->sources[s];
            if (source_ok[s])
                n += snprintf(text + n, sizeof text - (size_t)n, "%s#%06X %s %d#",
                              s ? "   " : "", (unsigned)src->colour, src->name, in_range[s]);
            else
                n += snprintf(text + n, sizeof text - (size_t)n, "%s#FF3B30 %s -#",
                              s ? "   " : "", src->name);
        }
    }
    if (cfg->credit[0] && n < (int)sizeof text)
        snprintf(text + n, sizeof text - (size_t)n, "\n%s", cfg->credit);

    /* Only touch the label when the text changes, to avoid needless redraws */
    if (strcmp(lv_label_get_text(status_label), text) != 0)
        lv_label_set_text(status_label, text);
}
