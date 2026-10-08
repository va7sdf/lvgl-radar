/* Aircraft list: everything in range, nearest first. */

#include "list.h"

#include "units.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ROWS 64
#define ROW_H    44
#define COLS     5

#define COL_BG    lv_color_hex(0x031A09)
#define COL_RIM   lv_color_hex(0x2FA84A)
#define COL_DIM   lv_color_hex(0x3C9C54)
#define COL_TEXT  lv_color_hex(0xC8FFD0)
#define COL_EMERG lv_color_hex(0xFF3B30)

static const radar_config_t *cfg;
static void (*on_open)(const char *hex);
static lv_obj_t *title, *box, *empty;
static struct {
    lv_obj_t *row, *cell[COLS];
    char      hex[8];
} rows[MAX_ROWS];

/* Column positions inside a row, in px */
static const int col_x[COLS] = { 40, 180, 280, 415, 540 };   /* centred in the 720 px box */

typedef struct {
    const aircraft_t *a;
    double nm, brg;
} entry_t;

static int by_distance(const void *x, const void *y)
{
    double a = ((const entry_t *)x)->nm, b = ((const entry_t *)y)->nm;
    return a < b ? -1 : a > b;
}

static void row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (on_open && rows[i].hex[0])
        on_open(rows[i].hex);
}

void list_refresh(const feed_snapshot_t *merged)
{
    static entry_t e[FEED_MAX_AC];
    int n = 0;
    const double coslat = cos(cfg->lat * M_PI / 180.0);
    for (int i = 0; merged && i < merged->count; i++) {
        const aircraft_t *a = &merged->ac[i];
        double north = (a->lat - cfg->lat) * 60.0, east = (a->lon - cfg->lon) * 60.0 * coslat;
        double nm = hypot(north, east);
        if (nm > cfg->range_nm)
            continue;
        e[n].a = a;
        e[n].nm = nm;
        e[n].brg = fmod(atan2(east, north) * 180.0 / M_PI + 360.0, 360.0);
        n++;
    }
    qsort(e, (size_t)n, sizeof e[0], by_distance);

    char rng[24];
    fmt_dist(rng, sizeof rng, cfg->range_nm);
    lv_label_set_text_fmt(title, "Aircraft within %s  (%d)", rng, n);
    if (n == 0)
        lv_obj_clear_flag(empty, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(empty, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < MAX_ROWS; i++) {
        if (i >= n) {
            lv_obj_add_flag(rows[i].row, LV_OBJ_FLAG_HIDDEN);
            rows[i].hex[0] = '\0';
            continue;
        }
        const aircraft_t *a = e[i].a;
        bool emerg = !strcmp(a->squawk, "7500") || !strcmp(a->squawk, "7600") || !strcmp(a->squawk, "7700");
        char t[COLS][40];
        snprintf(t[0], sizeof t[0], "%s", a->flight[0] ? a->flight : a->hex);
        snprintf(t[1], sizeof t[1], "%s", a->type[0] ? a->type : "-");
        if (a->on_ground)
            snprintf(t[2], sizeof t[2], "ground");
        else if (a->alt_valid)
            fmt_alt(t[2], sizeof t[2], a->alt_ft);
        else
            snprintf(t[2], sizeof t[2], "-");
        if (!isnan(a->gs))
            fmt_speed(t[3], sizeof t[3], a->gs);
        else
            snprintf(t[3], sizeof t[3], "-");
        char d[24];
        fmt_dist(d, sizeof d, e[i].nm);
        snprintf(t[4], sizeof t[4], "%s  %03d°", d, (int)lround(e[i].brg) % 360);

        for (int c = 0; c < COLS; c++)
            if (strcmp(lv_label_get_text(rows[i].cell[c]), t[c]) != 0)
                lv_label_set_text(rows[i].cell[c], t[c]);
        uint32_t src = cfg->sources[a->source < cfg->n_sources ? a->source : 0].colour;
        lv_obj_set_style_text_color(rows[i].cell[0], emerg ? COL_EMERG : lv_color_hex(src), 0);
        snprintf(rows[i].hex, sizeof rows[i].hex, "%s", a->hex);
        lv_obj_clear_flag(rows[i].row, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *list_create(const radar_config_t *config, void (*open_cb)(const char *hex))
{
    cfg = config;
    on_open = open_cb;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *disc = lv_obj_create(scr);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, 1000, 1000);
    lv_obj_center(disc);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, COL_BG, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(disc, COL_RIM, 0);
    lv_obj_set_style_border_width(disc, 3, 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 120);

    /* Column headings */
    static const char *heads[COLS] = { "Flight", "Type", "Altitude", "Speed", "From you" };
    lv_obj_t *head = lv_obj_create(scr);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, 720, 30);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 172);
    for (int c = 0; c < COLS; c++) {
        lv_obj_t *h = lv_label_create(head);
        lv_obj_set_style_text_font(h, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(h, COL_DIM, 0);
        lv_label_set_text(h, heads[c]);
        lv_obj_set_pos(h, col_x[c] + 10, 0);
    }

    /* Scrolling list of rows (kept, and refilled in place) */
    box = lv_obj_create(scr);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 720, 600);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 205);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
    for (int i = 0; i < MAX_ROWS; i++) {
        lv_obj_t *r = lv_obj_create(box);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 720, ROW_H);
        lv_obj_set_style_radius(r, 8, 0);
        lv_obj_set_style_bg_color(r, COL_RIM, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(r, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(r, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        for (int c = 0; c < COLS; c++) {
            lv_obj_t *l = lv_label_create(r);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(l, COL_TEXT, 0);
            lv_label_set_text(l, "");
            lv_obj_set_pos(l, col_x[c] + 10, 8);
            rows[i].cell[c] = l;
        }
        rows[i].row = r;
    }

    empty = lv_label_create(scr);
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(empty, COL_DIM, 0);
    lv_label_set_text(empty, "Nothing in range right now");
    lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *hint = lv_label_create(scr);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(hint, COL_DIM, 0);
    lv_label_set_text(hint, "tap a row for details  •  swipe sideways to return");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 840);
    return scr;
}
