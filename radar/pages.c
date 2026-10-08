/* Clock & units page and the credits screen. */

#include "pages.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define COL_BG    lv_color_hex(0x031A09)
#define COL_RIM   lv_color_hex(0x2FA84A)
#define COL_DIM   lv_color_hex(0x3C9C54)
#define COL_TEXT  lv_color_hex(0xC8FFD0)

static lv_obj_t *round_page(void)
{
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
    return scr;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t col, int y, const char *s)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, s);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static lv_obj_t *pill_button(lv_obj_t *parent, const char *s, int w, int y, bool filled)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, 56);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_bg_color(b, filled ? COL_RIM : COL_BG, 0);
    lv_obj_set_style_border_color(b, COL_RIM, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, filled ? COL_BG : COL_TEXT, 0);
    lv_label_set_text(l, s);
    lv_obj_center(l);
    return b;
}

/* ---------- clock & units ---------- */

typedef struct {
    const char *title;
    const char *key;            /* radar.conf key */
    const char *map[5];         /* button labels, "" terminated */
    const char *values[4];      /* what's saved for each choice */
    int        *field;          /* where the choice lives in the config */
    lv_obj_t   *bm;
} choice_t;

static radar_config_t *ccfg;
static void (*cu_done)(void);
static int  utc_choice, h24_choice;     /* bools held as ints for the table */
static choice_t choices[] = {
    { "Big clock",   "clock_main",    { "UTC", "Local", "" },          { "utc", "local" }, NULL, NULL },
    { "Hours",       "clock_24h",     { "24 h", "12 h", "" },          { "yes", "no" }, NULL, NULL },
    { "Temperature", "unit_temp",     { "°C", "°F", "" },              { "C", "F" }, NULL, NULL },
    { "Speed",       "unit_speed",    { "kt", "km/h", "mph", "" },     { "kt", "km/h", "mph" }, NULL, NULL },
    { "Altitude",    "unit_alt",      { "ft", "m", "" },               { "ft", "m" }, NULL, NULL },
    { "Distance",    "unit_dist",     { "nm", "km", "mi", "" },        { "nm", "km", "mi" }, NULL, NULL },
    { "Pressure",    "unit_pressure", { "hPa", "kPa", "inHg", "" },    { "hPa", "kPa", "inHg" }, NULL, NULL },
};
#define N_CHOICES (int)(sizeof choices / sizeof choices[0])

static void sync_from_config(void)
{
    utc_choice = ccfg->clock_utc ? 0 : 1;
    h24_choice = ccfg->clock_24h ? 0 : 1;
}

static void choice_cb(lv_event_t *e)
{
    choice_t *c = lv_event_get_user_data(e);
    uint32_t sel = lv_buttonmatrix_get_selected_button(c->bm);
    if (sel == LV_BUTTONMATRIX_BUTTON_NONE)
        return;
    *c->field = (int)sel;
    ccfg->clock_utc = utc_choice == 0;
    ccfg->clock_24h = h24_choice == 0;
    if (config_save_value(ccfg->path, c->key, c->values[sel]) != 0)
        fprintf(stderr, "clock & units: could not save %s\n", c->key);
}

static void cu_close(lv_event_t *e)
{
    (void)e;
    if (cu_done)
        cu_done();
}

void clockunits_opened(void)
{
    sync_from_config();
    for (int i = 0; i < N_CHOICES; i++) {
        int n = 0;
        while (choices[i].map[n][0])
            n++;
        for (int k = 0; k < n; k++)
            lv_buttonmatrix_clear_button_ctrl(choices[i].bm, (uint32_t)k, LV_BUTTONMATRIX_CTRL_CHECKED);
        lv_buttonmatrix_set_button_ctrl(choices[i].bm, (uint32_t)*choices[i].field,
                                        LV_BUTTONMATRIX_CTRL_CHECKED);
    }
}

lv_obj_t *clockunits_create(radar_config_t *cfg, void (*done_cb)(void))
{
    ccfg = cfg;
    cu_done = done_cb;
    choices[0].field = &utc_choice;
    choices[1].field = &h24_choice;
    choices[2].field = &cfg->unit_temp;
    choices[3].field = &cfg->unit_speed;
    choices[4].field = &cfg->unit_alt;
    choices[5].field = &cfg->unit_dist;
    choices[6].field = &cfg->unit_pressure;

    lv_obj_t *scr = round_page();
    text(scr, &lv_font_montserrat_24, COL_TEXT, 82, "Clock & units");

    for (int i = 0; i < N_CHOICES; i++) {
        choice_t *c = &choices[i];
        int y = 128 + i * 88;
        text(scr, &lv_font_montserrat_20, COL_DIM, y, c->title);

        int n = 0;
        while (c->map[n][0])
            n++;
        c->bm = lv_buttonmatrix_create(scr);
        lv_buttonmatrix_set_map(c->bm, c->map);
        lv_buttonmatrix_set_button_ctrl_all(c->bm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
        lv_buttonmatrix_set_one_checked(c->bm, true);
        lv_obj_set_size(c->bm, n * 130, 52);
        lv_obj_align(c->bm, LV_ALIGN_TOP_MID, 0, y + 26);
        lv_obj_set_style_bg_opa(c->bm, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c->bm, 0, 0);
        lv_obj_set_style_pad_all(c->bm, 0, 0);
        lv_obj_set_style_pad_gap(c->bm, 10, 0);
        lv_obj_set_style_bg_color(c->bm, COL_BG, LV_PART_ITEMS);
        lv_obj_set_style_border_color(c->bm, COL_RIM, LV_PART_ITEMS);
        lv_obj_set_style_border_width(c->bm, 2, LV_PART_ITEMS);
        lv_obj_set_style_radius(c->bm, 24, LV_PART_ITEMS);
        lv_obj_set_style_shadow_width(c->bm, 0, LV_PART_ITEMS);
        lv_obj_set_style_text_color(c->bm, COL_TEXT, LV_PART_ITEMS);
        lv_obj_set_style_text_font(c->bm, &lv_font_montserrat_20, LV_PART_ITEMS);
        lv_obj_set_style_bg_color(c->bm, COL_RIM, LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(c->bm, COL_BG, LV_PART_ITEMS | LV_STATE_CHECKED);
        lv_obj_add_event_cb(c->bm, choice_cb, LV_EVENT_VALUE_CHANGED, c);
    }

    lv_obj_t *done = pill_button(scr, "Close", 200, 760, true);
    lv_obj_add_event_cb(done, cu_close, LV_EVENT_CLICKED, NULL);
    text(scr, &lv_font_montserrat_20, COL_DIM, 830,
         "Range rings stay in nautical miles");
    return scr;
}

/* ---------- credits ---------- */

static void (*cr_done)(void);

static void cr_close(lv_event_t *e)
{
    (void)e;
    if (cr_done)
        cr_done();
}

static const char *credits_text =
    "#C8FFD0 AIRCRAFT#\n"
    "Your own receiver (dump1090-fa)\n"
    "adsb.fi open data - personal, non-commercial use\n"
    "adsbdb.com - aircraft and route lookups\n"
    "\n"
    "#C8FFD0 WEATHER#\n"
    "Open-Meteo.com - CC BY 4.0\n"
    "aviationweather.gov (NOAA) - METARs\n"
    "\n"
    "#C8FFD0 MAP#\n"
    "GSHHG shorelines and rivers - P. Wessel and W. H. F. Smith, LGPL\n"
    "OurAirports - airports, runways, frequencies and navaids,\n"
    "public domain\n"
    "GeoNames - towns and peaks, if used - CC BY 4.0\n"
    "\n"
    "#C8FFD0 SOFTWARE AND FONTS#\n"
    "LVGL graphics library - MIT licence\n"
    "libcurl - curl licence    cJSON - MIT licence\n"
    "DSEG fonts - keshikan, SIL Open Font License 1.1\n"
    "Weather Icons - Erik Flowers and Lukas Bischoff, SIL OFL 1.1\n"
    "Montserrat - The Montserrat Project Authors, SIL OFL 1.1";

lv_obj_t *credits_create(void (*done_cb)(void))
{
    cr_done = done_cb;
    lv_obj_t *scr = round_page();
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, cr_close, LV_EVENT_CLICKED, NULL);

    text(scr, &lv_font_montserrat_24, COL_TEXT, 120, "Credits");
    lv_obj_t *body = text(scr, &lv_font_montserrat_20, COL_DIM, 175, "");
    lv_obj_set_width(body, 760);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_recolor(body, true);
    lv_obj_set_style_text_line_space(body, 4, 0);
    lv_label_set_text(body, credits_text);
    text(scr, &lv_font_montserrat_20, COL_DIM, 870, "tap to return");
    return scr;
}
