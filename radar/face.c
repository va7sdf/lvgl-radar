/*
 * Radar face: disc, range rings, axes and labels drawn once onto a
 * full-screen canvas, plus the rotating sweep.
 *
 * The sweep is rendered straight into the canvas pixels. A pristine copy
 * of the face ("base") is kept, and each frame only the wedge-shaped area
 * the sweep has just passed over is recomputed as base + green at an
 * opacity that fades with the angle behind the leading edge. A lookup
 * table holds each pixel's bearing from the centre, so the per-pixel work
 * is a subtraction, a table lookup and a blend.
 */

#include "face.h"
#include "map.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N_RINGS      4
#define R_SWEEP      (FACE_R_OUTER - 3)   /* stay inside the outer ring line */

#define COL_DISC      lv_color_hex(0x031A09)
#define COL_RING      lv_color_hex(0x1E5C2C)
#define COL_RING_OUT  lv_color_hex(0x2FA84A)
#define COL_TEXT_DIM  lv_color_hex(0x3C9C54)
#define SWEEP_RGB     0x39FF5A

#define ALPHA_STEPS  1024      /* resolution of the fade across the trail */
#define TRAIL_PEAK   170       /* opacity just behind the leading edge */
#define EDGE_ALPHA   235       /* the bright leading line */
#define EDGE_DEG     0.8

static lv_obj_t      *canvas;
static lv_draw_buf_t *face;          /* what's on screen */
static uint16_t      *still;         /* the bare face: disc, rings, labels */
static uint16_t      *base;          /* still + overlay (trails), without the sweep */
static uint32_t       stride_px;     /* pixels per row in both buffers */

static uint16_t      *bearing;       /* per-pixel bearing, 0..65535 = 0..360 deg */
static int16_t        row_half[FACE_H];  /* half-width of the sweep disc per row, -1 = outside */

static uint32_t       period_ms;
static uint16_t       trail_u;       /* trail length in bearing units */
static uint16_t       edge_u;
static uint32_t       alpha_scale;   /* maps d in [0, trail_u) to [0, ALPHA_STEPS) */
static uint8_t        alpha_tab[ALPHA_STEPS];
static uint16_t       prev_angle;
static bool           sweep_started;
static bool           visible = true;    /* draw the sweep only while on screen */
static int            sw_r, sw_g, sw_b;  /* sweep colour as RGB565 components */
static lv_timer_t    *sweep_timer;
static const radar_config_t *conf;
static double         draw_ms_avg;   /* smoothed time to draw one sweep frame */

/* ---------- static face ---------- */

static void draw_label(lv_layer_t *layer, const char *text, const lv_font_t *font,
                       lv_color_t col, lv_text_align_t align,
                       int x1, int y1, int x2, int y2)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = text;
    d.text_local = 1;               /* copy: drawing happens later */
    d.font = font;
    d.color = col;
    d.align = align;
    lv_area_t a = { x1, y1, x2, y2 };
    lv_draw_label(layer, &d, &a);
}

static void draw_face(const radar_config_t *cfg)
{
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    /* Dark green disc */
    lv_draw_rect_dsc_t disc;
    lv_draw_rect_dsc_init(&disc);
    disc.bg_color = COL_DISC;
    disc.bg_opa = LV_OPA_COVER;
    disc.radius = LV_RADIUS_CIRCLE;
    lv_area_t da = { FACE_CX - FACE_R_OUTER, FACE_CY - FACE_R_OUTER,
                     FACE_CX + FACE_R_OUTER - 1, FACE_CY + FACE_R_OUTER - 1 };
    lv_draw_rect(&layer, &disc, &da);
    lv_canvas_finish_layer(canvas, &layer);

    /* Outline map on the disc, under everything else. Its labels keep
       clear of the range labels and the status line. */
    lv_area_t reserved[N_RINGS + 1];
    for (int i = 1; i <= N_RINGS; i++) {
        int r = FACE_R_OUTER * i / N_RINGS;
        int x = FACE_CX + (int)(r * 0.5) + 6;
        int y = FACE_CY - (int)(r * 0.866) - 24;
        reserved[i - 1] = (lv_area_t){ x - 4, y, x + 90, y + 24 };
    }
    reserved[N_RINGS] = (lv_area_t){ FACE_CX - 190, FACE_H - 140, FACE_CX + 190, FACE_H - 60 };
    map_draw(canvas, cfg, FACE_CX, FACE_CY, FACE_R_OUTER, reserved, N_RINGS + 1);
    lv_canvas_init_layer(canvas, &layer);

    /* Axes */
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = COL_RING;
    line.width = 1;
    line.p1.x = FACE_CX; line.p1.y = FACE_CY - FACE_R_OUTER;
    line.p2.x = FACE_CX; line.p2.y = FACE_CY + FACE_R_OUTER;
    lv_draw_line(&layer, &line);
    line.p1.x = FACE_CX - FACE_R_OUTER; line.p1.y = FACE_CY;
    line.p2.x = FACE_CX + FACE_R_OUTER; line.p2.y = FACE_CY;
    lv_draw_line(&layer, &line);

    /* Range rings with labels at a bearing of 30 degrees */
    for (int i = 1; i <= N_RINGS; i++) {
        int r = FACE_R_OUTER * i / N_RINGS;
        bool outer = (i == N_RINGS);

        lv_draw_arc_dsc_t arc;
        lv_draw_arc_dsc_init(&arc);
        arc.color = outer ? COL_RING_OUT : COL_RING;
        arc.width = outer ? 3 : 2;
        arc.start_angle = 0;
        arc.end_angle = 360;
        arc.center.x = FACE_CX;
        arc.center.y = FACE_CY;
        arc.radius = (uint16_t)r;
        lv_draw_arc(&layer, &arc);

        char text[16];
        snprintf(text, sizeof text, outer ? "%g nm" : "%g", cfg->range_nm * i / N_RINGS);
        int x = FACE_CX + (int)(r * 0.5) + 6;
        int y = FACE_CY - (int)(r * 0.866) - 24;
        draw_label(&layer, text, &lv_font_montserrat_20, COL_TEXT_DIM,
                   LV_TEXT_ALIGN_LEFT, x, y, x + 120, y + 24);
    }

    /* Compass points, outside the outer ring */
    const lv_font_t *f = &lv_font_montserrat_24;
    draw_label(&layer, "N", f, COL_RING_OUT, LV_TEXT_ALIGN_CENTER,
               FACE_CX - 20, 8, FACE_CX + 20, 36);
    draw_label(&layer, "S", f, COL_RING_OUT, LV_TEXT_ALIGN_CENTER,
               FACE_CX - 20, FACE_H - 36, FACE_CX + 20, FACE_H - 8);
    draw_label(&layer, "E", f, COL_RING_OUT, LV_TEXT_ALIGN_RIGHT,
               FACE_W - 40, FACE_CY - 14, FACE_W - 12, FACE_CY + 14);
    draw_label(&layer, "W", f, COL_RING_OUT, LV_TEXT_ALIGN_LEFT,
               12, FACE_CY - 14, 52, FACE_CY + 14);

    /* Station marker */
    lv_draw_rect_dsc_t me;
    lv_draw_rect_dsc_init(&me);
    me.bg_color = lv_color_white();
    me.bg_opa = LV_OPA_COVER;
    me.radius = LV_RADIUS_CIRCLE;
    lv_area_t ma = { FACE_CX - 6, FACE_CY - 6, FACE_CX + 5, FACE_CY + 5 };
    lv_draw_rect(&layer, &me, &ma);

    lv_canvas_finish_layer(canvas, &layer);
}

/* ---------- sweep ---------- */

static inline uint16_t blend565(uint16_t bg, uint8_t a)
{
    int r = bg >> 11, g = (bg >> 5) & 0x3F, b = bg & 0x1F;
    r += ((sw_r - r) * a) >> 8;
    g += ((sw_g - g) * a) >> 8;
    b += ((sw_b - b) * a) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void point_at(uint16_t u, int *x, int *y)
{
    double rad = u * (2.0 * M_PI / 65536.0);
    *x = FACE_CX + (int)lround(R_SWEEP * sin(rad));
    *y = FACE_CY - (int)lround(R_SWEEP * cos(rad));
}

/* Bounding box of the sector from bearing `start` clockwise by `span`. */
static void sector_bbox(uint16_t start, uint32_t span, lv_area_t *a)
{
    if (span >= 16384) {            /* a quarter turn or more: just take the disc */
        a->x1 = FACE_CX - R_SWEEP; a->x2 = FACE_CX + R_SWEEP;
        a->y1 = FACE_CY - R_SWEEP; a->y2 = FACE_CY + R_SWEEP;
        return;
    }
    int x, y;
    a->x1 = a->x2 = FACE_CX;
    a->y1 = a->y2 = FACE_CY;

    uint16_t ends[2] = { start, (uint16_t)(start + span) };
    for (int i = 0; i < 2; i++) {
        point_at(ends[i], &x, &y);
        if (x < a->x1) a->x1 = x;
        if (x > a->x2) a->x2 = x;
        if (y < a->y1) a->y1 = y;
        if (y > a->y2) a->y2 = y;
    }
    /* Cardinal extremes inside the span push the box to the edge */
    for (uint32_t k = 0; k < 4; k++) {
        uint16_t c = (uint16_t)(k * 16384);
        if ((uint16_t)(c - start) <= span) {
            if (k == 0) a->y1 = FACE_CY - R_SWEEP;
            if (k == 1) a->x2 = FACE_CX + R_SWEEP;
            if (k == 2) a->y2 = FACE_CY + R_SWEEP;
            if (k == 3) a->x1 = FACE_CX - R_SWEEP;
        }
    }
    a->x1 -= 2; a->y1 -= 2; a->x2 += 2; a->y2 += 2;
    if (a->x1 < 0) a->x1 = 0;
    if (a->y1 < 0) a->y1 = 0;
    if (a->x2 > FACE_W - 1) a->x2 = FACE_W - 1;
    if (a->y2 > FACE_H - 1) a->y2 = FACE_H - 1;
}

static void render_area(const lv_area_t *a, uint16_t angle)
{
    uint8_t *data = face->data;
    uint32_t stride = face->header.stride;

    for (int y = a->y1; y <= a->y2; y++) {
        int h = row_half[y];
        if (h < 0)
            continue;
        int xs = FACE_CX - h > a->x1 ? FACE_CX - h : a->x1;
        int xe = FACE_CX + h < a->x2 ? FACE_CX + h : a->x2;

        uint16_t       *dst = (uint16_t *)(data + (size_t)y * stride);
        const uint16_t *src = base + (size_t)y * stride_px;
        const uint16_t *brg = bearing + (size_t)y * FACE_W;

        for (int x = xs; x <= xe; x++) {
            uint16_t d = (uint16_t)(angle - brg[x]);    /* how far behind the sweep */
            if (d < trail_u) {
                uint8_t al = d < edge_u ? EDGE_ALPHA
                                        : alpha_tab[((uint32_t)d * alpha_scale) >> 16];
                dst[x] = blend565(src[x], al);
            } else {
                dst[x] = src[x];
            }
        }
    }
}

void face_sweep_step(uint32_t now_ms)
{
    if (!trail_u)
        return;

    uint16_t angle = (uint16_t)(((uint64_t)(now_ms % period_ms) << 16) / period_ms);
    if (!sweep_started) {
        prev_angle = angle;
        sweep_started = true;
    }

    /* Everything from the old tail to the new leading edge changes */
    uint16_t start = (uint16_t)(prev_angle - trail_u - 1);
    uint32_t span = (uint16_t)(angle - prev_angle) + (uint32_t)trail_u + 1;

    if (visible) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        lv_area_t a;
        sector_bbox(start, span, &a);
        render_area(&a, angle);
        lv_obj_invalidate_area(canvas, &a);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        draw_ms_avg = draw_ms_avg ? draw_ms_avg * 0.95 + ms * 0.05 : ms;
    }
    prev_angle = angle;
}

double face_sweep_draw_ms(void)
{
    return draw_ms_avg;
}

void face_set_visible(bool on)
{
    if (on == visible)
        return;
    visible = on;
    if (on && base) {
        /* Coming back: the sweep has moved on, so start from the clean face */
        memcpy(face->data, base, (size_t)face->header.stride * FACE_H);
        if (trail_u && sweep_started) {
            lv_area_t a;
            sector_bbox((uint16_t)(prev_angle - trail_u), trail_u, &a);
            render_area(&a, prev_angle);
        }
        lv_obj_invalidate(canvas);
    }
}

bool face_sweep_angle(uint16_t *angle)
{
    if (!trail_u || !sweep_started)
        return false;
    *angle = prev_angle;
    return true;
}

/* ---------- overlay (aircraft trails) ---------- */

void face_overlay_begin(lv_layer_t *layer)
{
    /* Start again from the bare face, dropping the old overlay and sweep */
    memcpy(face->data, still, (size_t)face->header.stride * FACE_H);
    lv_canvas_init_layer(canvas, layer);
}

void face_overlay_end(lv_layer_t *layer, const lv_area_t *dirty)
{
    /* Same as lv_canvas_finish_layer(), but without invalidating the whole
       canvas: only the area that actually changed gets redrawn. */
    while (layer->draw_task_head) {
        lv_draw_dispatch_wait_for_request();
        if (!lv_draw_dispatch_layer(lv_obj_get_display(canvas), layer)) {
            lv_draw_wait_for_finish();
            lv_draw_dispatch_request();
        }
    }

    /* The face with the new overlay becomes the base the sweep blends onto */
    memcpy(base, face->data, (size_t)face->header.stride * FACE_H);

    /* Put the sweep back where it was */
    if (visible && trail_u && sweep_started) {
        lv_area_t a;
        sector_bbox((uint16_t)(prev_angle - trail_u), trail_u, &a);
        render_area(&a, prev_angle);
        lv_obj_invalidate_area(canvas, &a);
    }
    if (dirty)
        lv_obj_invalidate_area(canvas, dirty);
}

static void sweep_timer_cb(lv_timer_t *t)
{
    (void)t;
    face_sweep_step(lv_tick_get());
}

static void build_tables(void)
{
    sw_r = (SWEEP_RGB >> 16 & 0xFF) >> 3;
    sw_g = (SWEEP_RGB >> 8 & 0xFF) >> 2;
    sw_b = (SWEEP_RGB & 0xFF) >> 3;
    edge_u = (uint16_t)(EDGE_DEG / 360.0 * 65536.0);
    for (int i = 0; i < ALPHA_STEPS; i++) {
        double t = 1.0 - (double)i / ALPHA_STEPS;       /* 1 at the edge, 0 at the tail */
        alpha_tab[i] = (uint8_t)lround(TRAIL_PEAK * pow(t, 1.5));
    }

    bearing = malloc(sizeof(uint16_t) * FACE_W * FACE_H);
    if (!bearing) {
        fprintf(stderr, "face: out of memory for sweep table\n");
        return;
    }
    for (int y = 0; y < FACE_H; y++) {
        int dy = y - FACE_CY;
        row_half[y] = dy * dy <= R_SWEEP * R_SWEEP
                    ? (int16_t)sqrt((double)(R_SWEEP * R_SWEEP - dy * dy)) : -1;
        for (int x = 0; x < FACE_W; x++) {
            double b = atan2(x - FACE_CX, -(double)dy) / (2.0 * M_PI);   /* 0 = north, clockwise */
            if (b < 0)
                b += 1.0;
            bearing[(size_t)y * FACE_W + x] = (uint16_t)((uint32_t)lround(b * 65536.0) & 0xFFFF);
        }
    }
}

/* Clear any sweep pixels and show the face as it is underneath */
static void show_base(void)
{
    memcpy(face->data, base, (size_t)face->header.stride * FACE_H);
    lv_obj_invalidate(canvas);
}

void face_apply_sweep(void)
{
    const radar_config_t *cfg = conf;
    bool on = cfg->sweep_trail_deg > 0 && cfg->sweep_period_s > 0 && bearing;
    if (!on) {
        if (trail_u)
            show_base();
        trail_u = 0;
        sweep_started = false;
    } else {
        double trail = cfg->sweep_trail_deg > 180 ? 180 : cfg->sweep_trail_deg;
        uint16_t new_u = (uint16_t)(trail / 360.0 * 65536.0);
        if (trail_u && new_u < trail_u)
            show_base();                /* a narrower trail would leave old pixels */
        trail_u = new_u;
        alpha_scale = (uint32_t)(((uint64_t)ALPHA_STEPS << 16) / trail_u);
        uint32_t p = (uint32_t)(cfg->sweep_period_s * 1000.0);
        if (p != period_ms) {
            period_ms = p;
            sweep_started = false;      /* the angle jumps; start the trail afresh */
            show_base();
        }
    }
    int fps = cfg->sweep_fps > 0 ? cfg->sweep_fps : 20;
    if (!sweep_timer)
        sweep_timer = lv_timer_create(sweep_timer_cb, (uint32_t)(1000 / fps), NULL);
    else
        lv_timer_set_period(sweep_timer, (uint32_t)(1000 / fps));
}

void face_redraw(void)
{
    /* New range (or map settings): draw the face again from scratch. The
       trails overlay is lost here; radar_ui redraws it straight after. */
    draw_face(conf);
    size_t bytes = (size_t)face->header.stride * FACE_H;
    memcpy(still, face->data, bytes);
    memcpy(base, face->data, bytes);
    if (visible && trail_u && sweep_started) {
        lv_area_t a;
        sector_bbox((uint16_t)(prev_angle - trail_u), trail_u, &a);
        render_area(&a, prev_angle);
    }
    lv_obj_invalidate(canvas);
}

void face_create(lv_obj_t *parent, const radar_config_t *cfg)
{
    conf = cfg;
    face = lv_draw_buf_create(FACE_W, FACE_H, LV_COLOR_FORMAT_RGB565, 0);
    canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(canvas, face);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);

    draw_face(cfg);

    stride_px = face->header.stride / 2;
    size_t bytes = (size_t)face->header.stride * FACE_H;
    base = malloc(bytes);
    still = malloc(bytes);
    if (!base || !still) {
        fprintf(stderr, "face: out of memory for face images\n");
        return;
    }
    memcpy(base, face->data, bytes);
    memcpy(still, face->data, bytes);

    build_tables();
    face_apply_sweep();
}
