/* Aircraft details and clock/weather screens, and switching between them. */

#include "ui.h"

#include "alerts.h"
#include "face.h"
#include "list.h"
#include "lookup.h"
#include "map.h"
#include "pages.h"
#include "receiver.h"
#include "settings.h"
#include "lvgl.h"
#include "radar_ui.h"
#include "units.h"
#include "weather.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_PAGES   6       /* aircraft per tap */
#define TAP_RADIUS  45      /* px around the finger */
#define PLACE_RADIUS 30     /* map places need a closer tap than aircraft */
#define N_FIELDS    10
#define PAGE_TIMEOUT_S 60   /* stats and settings return to the radar */

#if LV_FONT_MONTSERRAT_16
#define FONT_SMALL (&lv_font_montserrat_16)
#else
#define FONT_SMALL (&lv_font_montserrat_14)
#endif

#define COL_BG       lv_color_hex(0x031A09)
#define COL_RIM      lv_color_hex(0x2FA84A)
#define COL_DIM      lv_color_hex(0x3C9C54)
#define COL_TEXT     lv_color_hex(0xC8FFD0)
#define COL_EMERG    lv_color_hex(0xFF3B30)

typedef enum { S_RADAR, S_DETAIL, S_WEATHER, S_STATS, S_SETTINGS, S_LIST, S_UNITS, S_CREDITS } state_t;

typedef struct {
    char      hex[8];
    lv_obj_t *callsign, *source, *ident, *owner, *route, *route_cities;
    lv_obj_t *value[N_FIELDS];
} page_t;

static radar_config_t *cfg;
static const feed_snapshot_t *latest;
static state_t   state = S_RADAR;
static lv_obj_t *radar_scr, *detail_scr, *weather_scr, *stats_scr, *settings_scr;
static lv_obj_t *list_scr, *units_scr, *credits_scr;
static uint32_t  last_slow_refresh;
static page_t    pages[MAX_PAGES];
static int       n_pages;
static bool      place_open;            /* the details screen shows a map place */
static map_place_t place;
static lv_obj_t *place_metar, *place_board;
static bool      swiped;
static bool      weather_manual;        /* opened by a swipe, not by idling */
static uint32_t  empty_since;           /* when the radar last became empty, 0 = not empty */

static const lv_font_t *font_big = &lv_font_montserrat_48;    /* clock */
static const lv_font_t *font_mid = &lv_font_montserrat_48;    /* temperature */
static const lv_font_t *font_local = &lv_font_montserrat_24;  /* local/UTC line */
static const lv_font_t *font_icon_big;                         /* Weather Icons, NULL = none */
static const lv_font_t *font_icon_small;

/* Clock/weather widgets */
static lv_obj_t *w_local;
static lv_obj_t *w_date, *w_time, *w_temp, *w_cond, *w_wind, *w_metar, *w_note;
static lv_obj_t *w_icon;                /* beside the temperature */

/* Forecast rows, in columns: icon, day, high/low, conditions */
#define FC_ROWS (WEATHER_DAYS - 1)
static lv_obj_t *fc_icon[FC_ROWS], *fc_day[FC_ROWS], *fc_temp[FC_ROWS], *fc_cond[FC_ROWS];

static const char *field_names[N_FIELDS] = {
    "Altitude", "Vertical", "Ground speed", "Track", "From you",
    "Squawk", "Selected", "Category", "Hex", "Signal"
};

/* ---------- helpers ---------- */

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t col,
                       lv_align_t align, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    /* Clear LVGL's "Text" placeholder first: the icon font has no letters,
       and Tiny TTF logs "cache not allocated" for each missing glyph */
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, align, x, y);
    return l;
}

static void round_screen(lv_obj_t *scr)
{
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
}

static const aircraft_t *find(const char *hex)
{
    if (!latest)
        return NULL;
    for (int i = 0; i < latest->count; i++)
        if (strcmp(latest->ac[i].hex, hex) == 0)
            return &latest->ac[i];
    return NULL;
}

static bool is_emergency(const char *sq)
{
    return !strcmp(sq, "7500") || !strcmp(sq, "7600") || !strcmp(sq, "7700");
}

static const char *category_name(const char *c)
{
    static const struct { const char *code, *name; } t[] = {
        { "A1", "light" }, { "A2", "small" }, { "A3", "large" }, { "A4", "B757" },
        { "A5", "heavy" }, { "A6", "high perf." }, { "A7", "rotorcraft" },
        { "B1", "glider" }, { "B2", "balloon" }, { "B4", "ultralight" },
        { "B6", "drone" }, { "C1", "emergency vehicle" }, { "C2", "service vehicle" },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcmp(c, t[i].code))
            return t[i].name;
    return "";
}

/* Avoid redrawing (large) labels when nothing changed */
static void set_text(lv_obj_t *l, const char *text)
{
    if (strcmp(lv_label_get_text(l), text) != 0)
        lv_label_set_text(l, text);
}

/* ---------- switching ---------- */

static void show(state_t s)
{
    state = s;
    lv_obj_t *scr = s == S_RADAR ? radar_scr : s == S_DETAIL ? detail_scr :
                    s == S_WEATHER ? weather_scr : s == S_STATS ? stats_scr :
                    s == S_LIST ? list_scr : s == S_UNITS ? units_scr :
                    s == S_CREDITS ? credits_scr : settings_scr;
    face_set_visible(s == S_RADAR);
    lv_screen_load(scr);
    if (s == S_RADAR)
        empty_since = 0;            /* give the radar a full idle period again */
}

/* ---------- aircraft details ---------- */

static void fill_page(page_t *p)
{
    const aircraft_t *a = find(p->hex);
    char buf[128], v[N_FIELDS][48];

    if (!a) {
        lv_obj_set_style_text_color(p->callsign, COL_DIM, 0);
        lv_label_set_text(p->source, "no longer in range");
        for (int i = 0; i < N_FIELDS; i++)
            lv_obj_set_style_text_opa(p->value[i], LV_OPA_50, 0);
        return;
    }

    bool emerg = is_emergency(a->squawk);
    const source_config_t *src = &cfg->sources[a->source < cfg->n_sources ? a->source : 0];
    lv_label_set_text(p->callsign, a->flight[0] ? a->flight : a->hex);
    lv_obj_set_style_text_color(p->callsign, emerg ? COL_EMERG : lv_color_hex(src->colour), 0);
    const char *why = alerts_reason(a);
    if (why) {
        lv_label_set_text_fmt(p->source, "ALERT: %s", why);
        lv_obj_set_style_text_color(p->source, COL_EMERG, 0);
    } else {
        lv_label_set_text_fmt(p->source, "via %s", src->name);
        lv_obj_set_style_text_color(p->source, lv_color_hex(src->colour), 0);
    }

    /* Identity: from the feed, else looked up online */
    lookup_aircraft_t info = { 0 };
    lookup_state_t ls = LOOKUP_NONE;
    if (!a->reg[0] || !a->desc[0])
        ls = lookup_aircraft(a->hex, &info);
    const char *reg = a->reg[0] ? a->reg : info.reg;
    char type[96];
    if (a->desc[0])
        snprintf(type, sizeof type, "%s", a->desc);
    else if (info.type[0])
        snprintf(type, sizeof type, "%s %s", info.manufacturer, info.type);
    else
        snprintf(type, sizeof type, "%s", a->type);
    if (reg[0] && type[0])
        snprintf(buf, sizeof buf, "%s  \u2022  %s", type, reg);
    else
        snprintf(buf, sizeof buf, "%s", reg[0] ? reg : type[0] ? type :
                 ls == LOOKUP_PENDING ? "looking up..." : "unknown aircraft");
    lv_label_set_text(p->ident, buf);
    lv_label_set_text(p->owner, a->owner[0] ? a->owner : info.owner);

    /* Scheduled route for airline callsigns */
    lookup_route_t route;
    lookup_state_t rs = a->flight[0] ? lookup_route(a->flight, &route) : LOOKUP_NONE;
    if (rs == LOOKUP_FOUND) {
        lv_label_set_text_fmt(p->route, "%s  >  %s", route.origin_icao, route.dest_icao);
        lv_label_set_text_fmt(p->route_cities, "%s to %s%s%s", route.origin_city, route.dest_city,
                              route.airline[0] ? "\n" : "", route.airline);
    } else {
        lv_label_set_text(p->route, rs == LOOKUP_PENDING ? "route: looking up..." : "");
        lv_label_set_text(p->route_cities, "");
    }

    /* Flight data */
    for (int i = 0; i < N_FIELDS; i++)
        snprintf(v[i], sizeof v[i], "-");
    if (a->on_ground)
        snprintf(v[0], sizeof v[0], "on ground");
    else if (a->alt_valid)
        fmt_alt(v[0], sizeof v[0], a->alt_ft);
    if (!isnan(a->vrate))
        fmt_vrate(v[1], sizeof v[1], a->vrate);
    if (!isnan(a->gs))
        fmt_speed(v[2], sizeof v[2], a->gs);
    if (a->track_valid)
        snprintf(v[3], sizeof v[3], "%03d°", (int)lround(a->track) % 360);

    double north = (a->lat - cfg->lat) * 60.0;
    double east = (a->lon - cfg->lon) * 60.0 * cos(cfg->lat * M_PI / 180.0);
    double brg = fmod(atan2(east, north) * 180.0 / M_PI + 360.0, 360.0);
    char dist[24];
    fmt_dist(dist, sizeof dist, hypot(north, east));
    snprintf(v[4], sizeof v[4], "%s  %03d°", dist, (int)lround(brg) % 360);

    if (a->squawk[0])
        snprintf(v[5], sizeof v[5], "%s%s", a->squawk, emerg ? "  !" : "");
    if (!isnan(a->sel_alt) && a->sel_alt > 0)
        fmt_alt(v[6], sizeof v[6], a->sel_alt);
    if (a->category[0])
        snprintf(v[7], sizeof v[7], "%s %s", a->category, category_name(a->category));
    snprintf(v[8], sizeof v[8], "%s", a->hex);
    if (!isnan(a->rssi))
        snprintf(v[9], sizeof v[9], "%.1f dBFS", a->rssi);

    for (int i = 0; i < N_FIELDS; i++) {
        lv_label_set_text(p->value[i], v[i]);
        lv_obj_set_style_text_opa(p->value[i], LV_OPA_COVER, 0);
    }
    lv_obj_set_style_text_color(p->value[5], emerg ? COL_EMERG : COL_TEXT, 0);
}

static void back_to_radar(lv_event_t *e)
{
    (void)e;
    show(S_RADAR);
}

static void build_page(lv_obj_t *tile, page_t *p, int index, int count)
{
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, back_to_radar, LV_EVENT_CLICKED, NULL);

    p->source       = label(tile, &lv_font_montserrat_20, COL_DIM, LV_ALIGN_TOP_MID, 0, 110);
    p->callsign     = label(tile, &lv_font_montserrat_48, COL_TEXT, LV_ALIGN_TOP_MID, 0, 140);
    p->ident        = label(tile, &lv_font_montserrat_24, COL_TEXT, LV_ALIGN_TOP_MID, 0, 205);
    p->owner        = label(tile, &lv_font_montserrat_20, COL_DIM, LV_ALIGN_TOP_MID, 0, 240);
    p->route        = label(tile, &lv_font_montserrat_24, COL_TEXT, LV_ALIGN_TOP_MID, 0, 290);
    p->route_cities = label(tile, &lv_font_montserrat_20, COL_DIM, LV_ALIGN_TOP_MID, 0, 322);
    lv_obj_set_width(p->ident, 720);
    lv_obj_set_width(p->owner, 720);
    lv_label_set_long_mode(p->ident, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(p->owner, LV_LABEL_LONG_DOT);

    /* Two columns of name / value pairs */
    for (int i = 0; i < N_FIELDS; i++) {
        int col = i / 5, row = i % 5;
        int x = col == 0 ? 190 : 560;
        int y = 420 + row * 72;
        lv_obj_t *n = lv_label_create(tile);
        lv_obj_set_style_text_font(n, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(n, COL_DIM, 0);
        lv_label_set_text(n, field_names[i]);
        lv_obj_set_pos(n, x, y);
        p->value[i] = lv_label_create(tile);
        lv_obj_set_style_text_font(p->value[i], &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(p->value[i], COL_TEXT, 0);
        lv_obj_set_pos(p->value[i], x, y + 24);
    }

    /* Page dots when there's more than one aircraft */
    if (count > 1) {
        int w = count * 22;
        for (int i = 0; i < count; i++) {
            lv_obj_t *d = lv_obj_create(tile);
            lv_obj_remove_style_all(d);
            lv_obj_set_size(d, 12, 12);
            lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(d, i == index ? COL_TEXT : COL_DIM, 0);
            lv_obj_set_pos(d, 540 - w / 2 + i * 22 + 5, 815);
            lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    lv_obj_t *hint = label(tile, FONT_SMALL, COL_DIM, LV_ALIGN_TOP_MID, 0, 845);
    lv_label_set_text(hint, count > 1 ? "swipe for the others  \u2022  tap to return" : "tap to return");
}

static void open_details(char hex[][8], int n)
{
    /* Throw away the previous details screen (an alert can arrive while it's up) */
    if (detail_scr) {
        if (lv_screen_active() == detail_scr)
            lv_screen_load(radar_scr);
        lv_obj_delete_async(detail_scr);
    }

    detail_scr = lv_obj_create(NULL);
    round_screen(detail_scr);
    lv_obj_t *tv = lv_tileview_create(detail_scr);
    lv_obj_set_size(tv, 1080, 1080);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    n_pages = n;
    place_open = false;
    place_metar = NULL;
    for (int i = 0; i < n; i++) {
        lv_obj_t *tile = lv_tileview_add_tile(tv, (uint8_t)i, 0,
                                              (i > 0 ? LV_DIR_LEFT : 0) | (i < n - 1 ? LV_DIR_RIGHT : 0));
        memset(&pages[i], 0, sizeof pages[i]);
        snprintf(pages[i].hex, sizeof pages[i].hex, "%s", hex[i]);
        build_page(tile, &pages[i], i, n);
        fill_page(&pages[i]);
    }
    lv_display_trigger_activity(NULL);      /* the close timer starts now */
    show(S_DETAIL);
}

/* ---------- map places (airports, navaids, peaks, towns) ---------- */

static void metar_text(char *out, size_t size, const metar_t *m)
{
    const char *c = m->category;
    const char *col = !strcmp(c, "VFR") ? "48FF6A" : !strcmp(c, "MVFR") ? "38C8FF" :
                      !strcmp(c, "IFR") ? "FF3B30" : !strcmp(c, "LIFR") ? "FF4FD8" : NULL;
    if (col)
        snprintf(out, size, "#%s %s#  %s", col, c, m->raw);
    else
        snprintf(out, size, "%s", m->raw);
}

static bool place_has_metar(void)
{
    /* Airports with a four-letter ICAO code may report one */
    size_t n = strcspn(place.label, " ");
    return !strcmp(place.kind, "airport") && n == 4;
}

/* Live board: tracked aircraft whose scheduled route ends or starts here */
static void fill_board(const char *icao)
{
    char in[3][96], out[3][96];
    int n_in = 0, n_out = 0, pending = 0;
    const double coslat = cos(place.lat * M_PI / 180.0);
    for (int i = 0; latest && i < latest->count; i++) {
        const aircraft_t *a = &latest->ac[i];
        if (!a->flight[0])
            continue;
        lookup_route_t r;
        lookup_state_t s = lookup_route(a->flight, &r);
        if (s == LOOKUP_PENDING)
            pending++;
        if (s != LOOKUP_FOUND)
            continue;
        bool arriving = !strcmp(r.dest_icao, icao), leaving = !strcmp(r.origin_icao, icao);
        if (!arriving && !leaving)
            continue;
        /* Distance from the airport, not from you */
        double nm = hypot((a->lat - place.lat) * 60.0, (a->lon - place.lon) * 60.0 * coslat);
        char d[24], alt[24] = "";
        fmt_dist(d, sizeof d, nm);
        if (a->on_ground)
            snprintf(alt, sizeof alt, "on ground");
        else if (a->alt_valid)
            fmt_alt(alt, sizeof alt, a->alt_ft);
        if (arriving && n_in < 3)
            snprintf(in[n_in++], sizeof in[0], "%-8s from %s   %s out   %s", a->flight,
                     r.origin_iata[0] ? r.origin_iata : r.origin_icao, d, alt);
        else if (leaving && n_out < 3)
            snprintf(out[n_out++], sizeof out[0], "%-8s to %s   %s out   %s", a->flight,
                     r.dest_iata[0] ? r.dest_iata : r.dest_icao, d, alt);
    }

    char t[640];
    int k = 0;
    if (!n_in && !n_out) {
        snprintf(t, sizeof t, "%s", pending ? "Arrivals and departures: looking up routes..."
                                            : "No tracked arrivals or departures right now");
    } else {
        k += snprintf(t + k, sizeof t - (size_t)k, "#C8FFD0 Arriving#");
        for (int i = 0; i < n_in; i++)
            k += snprintf(t + k, sizeof t - (size_t)k, "\n%s", in[i]);
        if (!n_in)
            k += snprintf(t + k, sizeof t - (size_t)k, "\n-");
        k += snprintf(t + k, sizeof t - (size_t)k, "\n#C8FFD0 Departing#");
        for (int i = 0; i < n_out; i++)
            k += snprintf(t + k, sizeof t - (size_t)k, "\n%s", out[i]);
        if (!n_out)
            snprintf(t + k, sizeof t - (size_t)k, "\n-");
    }
    set_text(place_board, t);
}

static void fill_place(void)
{
    if (!place_metar || !place_has_metar())
        return;
    char icao[8];
    snprintf(icao, sizeof icao, "%.4s", place.label);
    if (place_board && lookup_enabled())
        fill_board(icao);
    metar_t m;
    lookup_state_t s = lookup_metar(icao, &m);
    char t[320];
    if (s == LOOKUP_FOUND)
        metar_text(t, sizeof t, &m);
    else
        snprintf(t, sizeof t, "%s", s == LOOKUP_PENDING ? "METAR: looking up..." : "");
    set_text(place_metar, t);
}

static void place_tap(lv_event_t *e)
{
    (void)e;
    show(S_RADAR);
}

static void open_place(const map_place_t *p)
{
    if (detail_scr) {
        if (lv_screen_active() == detail_scr)
            lv_screen_load(radar_scr);
        lv_obj_delete_async(detail_scr);
    }
    place = *p;
    n_pages = 0;
    place_open = true;

    detail_scr = lv_obj_create(NULL);
    round_screen(detail_scr);
    lv_obj_add_event_cb(detail_scr, place_tap, LV_EVENT_CLICKED, NULL);

    static const struct { const char *kind, *name; } kinds[] = {
        { "airport", "Airport" }, { "vor", "VOR" }, { "ndb", "NDB" },
        { "peak", "Peak" }, { "town", "Town" },
    };
    const char *kind = p->kind;
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
        if (!strcmp(p->kind, kinds[i].kind))
            kind = kinds[i].name;

    /* Title: the code for airports and navaids, else the name */
    char title[64];
    bool coded = !strcmp(p->kind, "airport") || !strcmp(p->kind, "vor") || !strcmp(p->kind, "ndb");
    if (coded)
        snprintf(title, sizeof title, "%.*s", (int)strcspn(p->label, " "), p->label);
    else
        snprintf(title, sizeof title, "%.*s", (int)strcspn(p->details, "\n"), p->details);

    lv_label_set_text(label(detail_scr, &lv_font_montserrat_20, COL_DIM, LV_ALIGN_TOP_MID, 0, 120), kind);
    lv_label_set_text(label(detail_scr, &lv_font_montserrat_48, COL_TEXT, LV_ALIGN_TOP_MID, 0, 150), title);

    /* Detail lines: the first (the name) a little larger */
    const char *rest = p->details;
    if (!coded && strchr(rest, '\n'))
        rest = strchr(rest, '\n') + 1;            /* the name is already the title */
    else if (!coded)
        rest = "";
    char first[128] = "";
    if (coded) {
        snprintf(first, sizeof first, "%.*s", (int)strcspn(rest, "\n"), rest);
        rest += strlen(first);
        if (*rest == '\n')
            rest++;
    }
    lv_obj_t *name = label(detail_scr, &lv_font_montserrat_24, COL_TEXT, LV_ALIGN_TOP_MID, 0, 220);
    lv_obj_set_width(name, 760);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text(name, first);

    lv_obj_t *body = label(detail_scr, &lv_font_montserrat_20, COL_TEXT, LV_ALIGN_TOP_MID, 0, 275);
    lv_obj_set_width(body, 760);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(body, 8, 0);
    lv_label_set_text(body, rest);

    /* Where it is from you */
    double north = (p->lat - cfg->lat) * 60.0;
    double east = (p->lon - cfg->lon) * 60.0 * cos(cfg->lat * M_PI / 180.0);
    double brg = fmod(atan2(east, north) * 180.0 / M_PI + 360.0, 360.0);
    lv_obj_t *from = label(detail_scr, &lv_font_montserrat_24, COL_DIM, LV_ALIGN_TOP_MID, 0,
                           place_has_metar() ? 548 : 640);
    char dist[24];
    fmt_dist(dist, sizeof dist, hypot(north, east));
    lv_label_set_text_fmt(from, "%s from you, bearing %03d°", dist, (int)lround(brg) % 360);

    place_metar = NULL;
    place_board = NULL;
    if (place_has_metar()) {
        place_board = label(detail_scr, &lv_font_montserrat_20, COL_DIM, LV_ALIGN_TOP_MID, 0, 590);
        lv_label_set_recolor(place_board, true);
        place_metar = label(detail_scr, &lv_font_montserrat_20, COL_DIM, LV_ALIGN_TOP_MID, 0, 770);
        lv_obj_set_width(place_metar, 720);
        lv_label_set_long_mode(place_metar, LV_LABEL_LONG_WRAP);
        lv_label_set_recolor(place_metar, true);
        fill_place();
    }
    lv_label_set_text(label(detail_scr, FONT_SMALL, COL_DIM, LV_ALIGN_TOP_MID, 0, 845), "tap to return");

    lv_display_trigger_activity(NULL);
    show(S_DETAIL);
}

/* ---------- clock and weather ---------- */

static const char *compass(int deg)
{
    static const char *pts[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    return pts[((deg % 360 + 360) % 360 + 22) / 45 % 8];
}

#define ICON_GAP  28     /* between the big icon and the temperature */
#define ICON_DY   8      /* nudge for the icon font's baseline */
#define FC_Y      724    /* first forecast row */
#define FC_ROW_H  34

static int text_width(const lv_font_t *font, const char *txt)
{
    if (!txt || !txt[0])
        return 0;
    lv_point_t sz;
    lv_text_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

static void fill_weather(void)
{
    time_t now = time(NULL);
    struct tm loc, utc;
    localtime_r(&now, &loc);
    gmtime_r(&now, &utc);
    char buf[160];

    /* Big clock: UTC (Zulu) by default; the other time on the line below */
    const struct tm *big = cfg->clock_utc ? &utc : &loc;
    strftime(buf, sizeof buf, "%A, %B %-d, %Y", big);
    set_text(w_date, buf);
    strftime(buf, sizeof buf, cfg->clock_24h ? "%H:%M" : "%l:%M", big);
    const char *hhmm = buf[0] == ' ' ? buf + 1 : buf;
    set_text(w_time, hhmm);


    /* Local zone as "PDT (UTC-7)" or "NDT (UTC-2:30)" */
    char zone[48], hm[16], other[96];
    long off = loc.tm_gmtoff / 60;
    if (off % 60)
        snprintf(zone, sizeof zone, "%s (UTC%+ld:%02ld)", loc.tm_zone, off / 60, labs(off % 60));
    else
        snprintf(zone, sizeof zone, "%s (UTC%+ld)", loc.tm_zone, off / 60);
    const char *ampm_big = cfg->clock_24h ? "" : (big->tm_hour < 12 ? "AM  \u2022  " : "PM  \u2022  ");
    if (cfg->clock_utc) {
        strftime(hm, sizeof hm, cfg->clock_24h ? "%H:%M" : "%l:%M %p", &loc);
        snprintf(other, sizeof other, "%s %s", hm[0] == ' ' ? hm + 1 : hm, zone);
        snprintf(buf, sizeof buf, "%s%s", ampm_big, other);
    } else {
        strftime(hm, sizeof hm, "%H:%MZ", &utc);
        snprintf(buf, sizeof buf, "%s%s  \u2022  %s", ampm_big, zone, hm);
    }
    set_text(w_local, buf);

    metar_t m;
    if (metar_get(&m)) {
        char t[320];
        metar_text(t, sizeof t, &m);
        set_text(w_metar, t);
    } else {
        set_text(w_metar, "");
    }

    weather_t w;
    if (!weather_get(&w)) {
        set_text(w_temp, "");
        set_text(w_cond, cfg->weather_url[0] ? "waiting for weather" : "");
        set_text(w_wind, "");
        set_text(w_icon, "");
        for (int i = 0; i < FC_ROWS; i++) {
            set_text(fc_icon[i], "");
            set_text(fc_day[i], "");
            set_text(fc_temp[i], "");
            set_text(fc_cond[i], "");
        }
        return;
    }

    char t[256], sp[24], gu[24], pr[24];
    fmt_temp(t, sizeof t, w.temp_c, false);
    set_text(w_temp, t);

    /* Icon left of the temperature, the pair centred on the dial. Night
       icons when Open-Meteo says so (or, without is_day, 20:00 to 06:00). */
    int temp_y = 532 - lv_font_get_line_height(font_mid) / 2;
    if (font_icon_big) {
        bool day = w.is_day >= 0 ? w.is_day : (loc.tm_hour >= 6 && loc.tm_hour < 20);
        const char *ic = weather_icon(w.code, day);
        set_text(w_icon, ic);
        int iw = text_width(font_icon_big, ic), tw = text_width(font_mid, t);
        int total = iw + ICON_GAP + tw;
        lv_obj_align(w_icon, LV_ALIGN_TOP_MID, -total / 2 + iw / 2,
                     532 - lv_font_get_line_height(font_icon_big) / 2 + ICON_DY);
        lv_obj_align(w_temp, LV_ALIGN_TOP_MID, total / 2 - tw / 2, temp_y);
    } else {
        lv_obj_align(w_temp, LV_ALIGN_TOP_MID, 0, temp_y);
    }
    set_text(w_cond, weather_text(w.code));
    fmt_speed(sp, sizeof sp, w.wind_kt);
    fmt_speed(gu, sizeof gu, isnan(w.gust_kt) ? 0 : w.gust_kt);
    if (w.wind_kt < 1)
        snprintf(buf, sizeof buf, "Calm");
    else if (!isnan(w.gust_kt) && w.gust_kt >= w.wind_kt + 10)
        snprintf(buf, sizeof buf, "Wind %s %s, gusting %s", compass(w.wind_dir), sp, gu);
    else
        snprintf(buf, sizeof buf, "Wind %s %s", compass(w.wind_dir), sp);
    fmt_pressure(pr, sizeof pr, w.pressure_hpa);
    snprintf(t, sizeof t, "%s\nHumidity %d%%   Pressure %s", buf, w.humidity, pr);
    set_text(w_wind, t);

    /* Today's high/low on the first line, then the next days, in columns
       that are as wide as their widest entry, the block centred */
    int col_w[4] = { 0 };
    for (int i = 0; i < FC_ROWS; i++) {
        if (i >= w.n_days) {
            set_text(fc_icon[i], "");
            set_text(fc_day[i], "");
            set_text(fc_temp[i], "");
            set_text(fc_cond[i], "");
            continue;
        }
        char pop[16] = "", hi[16], lo[16];
        if (w.day_pop[i] >= 20)
            snprintf(pop, sizeof pop, "  %d%%", w.day_pop[i]);
        fmt_temp(hi, sizeof hi, w.day_max[i], false);
        fmt_temp(lo, sizeof lo, w.day_min[i], false);
        set_text(fc_icon[i], font_icon_small ? weather_icon(w.day_code[i], true) : "");
        set_text(fc_day[i], i == 0 ? "Today" : w.day[i]);
        snprintf(t, sizeof t, "%s / %s", hi, lo);
        set_text(fc_temp[i], t);
        snprintf(t, sizeof t, "%s%s", weather_text(w.day_code[i]), pop);
        set_text(fc_cond[i], t);

        lv_obj_t *cells[4] = { fc_icon[i], fc_day[i], fc_temp[i], fc_cond[i] };
        for (int c = 0; c < 4; c++) {
            int cw = text_width(lv_obj_get_style_text_font(cells[c], 0), lv_label_get_text(cells[c]));
            if (cw > col_w[c])
                col_w[c] = cw;
        }
    }
    int gap[4] = { 0, col_w[0] ? 14 : 0, 28, 28 };
    int total = 0;
    for (int c = 0; c < 4; c++)
        total += gap[c] + col_w[c];
    for (int i = 0; i < FC_ROWS; i++) {
        lv_obj_t *cells[4] = { fc_icon[i], fc_day[i], fc_temp[i], fc_cond[i] };
        int x = 540 - total / 2;
        for (int c = 0; c < 4; c++) {
            x += gap[c];
            int y = FC_Y + i * FC_ROW_H;
            if (c == 0)     /* the icon font sits differently; centre it on the row */
                y += (lv_font_get_line_height(&lv_font_montserrat_24) -
                      (font_icon_small ? lv_font_get_line_height(font_icon_small) : 0)) / 2;
            lv_obj_set_pos(cells[c], x, y);
            x += col_w[c];
        }
    }
}

static void weather_tap(lv_event_t *e)
{
    weather_manual = false;
    show(S_RADAR);
    if (lv_event_get_code(e) == LV_EVENT_GESTURE)
        lv_indev_wait_release(lv_indev_active());
}

static void build_weather(void)
{
    weather_scr = lv_obj_create(NULL);
    round_screen(weather_scr);
    lv_obj_add_event_cb(weather_scr, weather_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(weather_scr, weather_tap, LV_EVENT_GESTURE, NULL);

    w_date = label(weather_scr, &lv_font_montserrat_24, COL_DIM, LV_ALIGN_TOP_MID, 0, 126);
    /* Centred on fixed lines, whatever size the fonts ended up */
    w_time = label(weather_scr, font_big, COL_TEXT, LV_ALIGN_TOP_MID, 0,
                   272 - lv_font_get_line_height(font_big) / 2);
    w_local = label(weather_scr, font_local, COL_DIM, LV_ALIGN_TOP_MID, 0, 372);
    w_temp = label(weather_scr, font_mid, COL_TEXT, LV_ALIGN_TOP_MID, 0,
                   532 - lv_font_get_line_height(font_mid) / 2);
    w_cond = label(weather_scr, &lv_font_montserrat_24, COL_TEXT, LV_ALIGN_TOP_MID, 0, 612);
    w_wind = label(weather_scr, &lv_font_montserrat_20, COL_DIM, LV_ALIGN_TOP_MID, 0, 650);
    w_icon = label(weather_scr, font_icon_big ? font_icon_big : &lv_font_montserrat_24, COL_RIM,
                   LV_ALIGN_TOP_MID, 0, 0);
    for (int i = 0; i < FC_ROWS; i++) {
        fc_icon[i] = label(weather_scr, font_icon_small ? font_icon_small : &lv_font_montserrat_24,
                           COL_RIM, LV_ALIGN_TOP_LEFT, 0, 0);
        fc_day[i] = label(weather_scr, &lv_font_montserrat_24, COL_TEXT, LV_ALIGN_TOP_LEFT, 0, 0);
        fc_temp[i] = label(weather_scr, &lv_font_montserrat_24, COL_TEXT, LV_ALIGN_TOP_LEFT, 0, 0);
        fc_cond[i] = label(weather_scr, &lv_font_montserrat_24, COL_DIM, LV_ALIGN_TOP_LEFT, 0, 0);
    }
    w_metar = label(weather_scr, &lv_font_montserrat_20, COL_DIM, LV_ALIGN_TOP_MID, 0, 826);
    lv_obj_set_width(w_metar, 660);
    lv_label_set_long_mode(w_metar, LV_LABEL_LONG_WRAP);
    lv_label_set_recolor(w_metar, true);
    w_note = label(weather_scr, FONT_SMALL, COL_DIM, LV_ALIGN_TOP_MID, 0, 895);
    lv_label_set_text(w_note, cfg->weather_url[0] ? "weather: Open-Meteo.com" : "");
}

/* ---------- radar touch ---------- */

static void radar_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();

    if (code == LV_EVENT_PRESSED) {
        swiped = false;
    } else if (code == LV_EVENT_LONG_PRESSED && !swiped) {
        /* Long-press an aircraft to follow it; again (or on nothing) to stop */
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        char hex[1][8];
        radar_ui_follow(radar_ui_hits(p.x, p.y, TAP_RADIUS, hex, 1) ? hex[0] : NULL);
        swiped = true;                      /* the release isn't a tap */
    } else if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        swiped = true;
        if (dir == LV_DIR_LEFT) {                   /* clock and weather */
            weather_manual = true;
            fill_weather();
            show(S_WEATHER);
        } else if (dir == LV_DIR_RIGHT) {           /* receiver stats */
            receiver_refresh();
            show(S_STATS);
        } else if (dir == LV_DIR_TOP) {             /* swipe up: settings */
            settings_opened();
            show(S_SETTINGS);
        } else if (dir == LV_DIR_BOTTOM) {          /* swipe down: aircraft list */
            list_refresh(latest);
            show(S_LIST);
        }
        /* The finger is still down: ignore it until it lifts, or the new
           screen would take the rest of the swipe as a tap or drag */
        lv_indev_wait_release(indev);
    } else if (code == LV_EVENT_CLICKED && !swiped) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        char hex[MAX_PAGES][8];
        int n = radar_ui_hits(p.x, p.y, TAP_RADIUS, hex, MAX_PAGES);
        map_place_t mp;
        if (n > 0) {
            open_details(hex, n);
        } else if (map_hit(p.x, p.y, PLACE_RADIUS, &mp)) {
            open_place(&mp);
        } else if (hypot(p.x - 540, p.y - 540) < 500 && cfg->n_ranges > 1) {
            /* Empty scope: step to the next range */
            int next = 0;
            for (int i = 0; i < cfg->n_ranges; i++)
                if (fabs(cfg->ranges[i] - cfg->range_nm) < 0.01)
                    next = (i + 1) % cfg->n_ranges;
            cfg->range_nm = cfg->ranges[next];
            radar_ui_range_changed();
            char v[16];
            snprintf(v, sizeof v, "%g", cfg->range_nm);
            config_save_value(cfg->path, "range_nm", v);
        }
    }
}

/* ---------- timing ---------- */

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    uint32_t inactive = lv_display_get_inactive_time(NULL);
    bool aircraft = radar_ui_shown() > 0;

    switch (state) {
    case S_RADAR:
        if (aircraft || cfg->idle_after_s <= 0) {
            empty_since = 0;
        } else if (!empty_since) {
            empty_since = now;
        } else if (now - empty_since >= (uint32_t)cfg->idle_after_s * 1000) {
            weather_manual = false;
            fill_weather();
            show(S_WEATHER);
        }
        break;
    case S_DETAIL:
        if (inactive >= (uint32_t)cfg->detail_timeout_s * 1000)
            show(S_RADAR);
        else if (place_open)
            fill_place();
        else
            for (int i = 0; i < n_pages; i++)
                fill_page(&pages[i]);       /* pick up lookups as they arrive */
        break;
    case S_STATS:
        if (inactive >= PAGE_TIMEOUT_S * 1000)
            show(S_RADAR);
        else if (now - last_slow_refresh >= 2000) {
            receiver_refresh();
            last_slow_refresh = now;
        }
        break;
    case S_LIST:
        if (inactive >= PAGE_TIMEOUT_S * 1000)
            show(S_RADAR);
        else if (now - last_slow_refresh >= 2000) {
            list_refresh(latest);
            last_slow_refresh = now;
        }
        break;
    case S_UNITS:
    case S_CREDITS:
        if (inactive >= PAGE_TIMEOUT_S * 1000)
            show(S_RADAR);
        break;
    case S_SETTINGS:
        if (inactive >= PAGE_TIMEOUT_S * 1000) {
            settings_save();
            show(S_RADAR);
        } else if (now - last_slow_refresh >= 2000) {
            settings_refresh();
            last_slow_refresh = now;
        }
        break;
    case S_WEATHER:
        fill_weather();
        if (weather_manual && inactive >= (uint32_t)cfg->weather_view_s * 1000)
            weather_manual = false;                 /* now behaves as if idle */
        if (!weather_manual && aircraft)
            show(S_RADAR);
        break;
    }
}

void ui_update(const feed_snapshot_t *merged)
{
    latest = merged;
    receiver_track(merged);

    char hex[1][8];
    if (state != S_SETTINGS && alerts_check(merged, hex[0]))
        open_details(hex, 1);
    if (state == S_DETAIL)
        for (int i = 0; i < n_pages; i++)
            fill_page(&pages[i]);
}

/* ---------- fonts ---------- */

#if LV_USE_TINY_TTF
/* Largest size up to `px` at which `sample` fits in max_w x max_h */
static lv_font_t *fit_font(const void *data, size_t size, int px, const char *sample,
                           int max_w, int max_h, int *px_out)
{
    lv_font_t *f = lv_tiny_ttf_create_data(data, size, px);
    if (!f)
        return NULL;
    lv_point_t s;
    lv_text_get_size(&s, sample, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    double k = 1.0;
    if (s.x > max_w)
        k = (double)max_w / s.x;
    if (s.y * k > max_h)
        k = (double)max_h / s.y;
    if (k < 0.99) {
        lv_tiny_ttf_destroy(f);
        px = (int)(px * k);
        f = lv_tiny_ttf_create_data(data, size, px);
    }
    if (px_out)
        *px_out = px;
    return f;
}
#endif

#if LV_USE_TINY_TTF
/* Read a whole font file; the data must stay for the program's life */
static void *read_font(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ui: font %s not found, using the built-in font\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    void *data = n > 0 ? malloc((size_t)n) : NULL;
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return data;
}
#endif

#define ICON_BIG_PX   110
#define CLOCK_MAX_W   640   /* the clock's box, centred on y = 272 */
#define CLOCK_MAX_H   250
#define ICON_SMALL_PX 26

static void load_fonts(void)
{
#if LV_USE_TINY_TTF
    /* Fonts differ a lot in width (DSEG7 is far wider than Montserrat at
       the same size), so each is sized to fit its space on the dial */
    size_t size;
    void *clock = read_font(cfg->font_file, &size);
    if (clock) {
        lv_font_t *big = fit_font(clock, size, 280, "00:00", CLOCK_MAX_W, CLOCK_MAX_H, NULL);
        if (big)
            font_big = big;
    }
    const char *tf = cfg->temp_font_file[0] ? cfg->temp_font_file : cfg->font_file;
    void *temp = strcmp(tf, cfg->font_file) == 0 ? clock : read_font(tf, &size);
    if (temp) {
        lv_font_t *mid = fit_font(temp, size, 170, "-00°", 420, 165, NULL);
        if (mid)
            font_mid = mid;
    }

    /* Weather Icons: one size beside the temperature, one for the forecast */
    if (cfg->icon_font_file[0]) {
        void *icons = read_font(cfg->icon_font_file, &size);
        if (icons) {
            font_icon_big = lv_tiny_ttf_create_data(icons, size, ICON_BIG_PX);
            font_icon_small = lv_tiny_ttf_create_data(icons, size, ICON_SMALL_PX);
        }
    }
#else
    printf("ui: LV_USE_TINY_TTF is off in lv_conf.h, so the clock uses the 48 px font\n");
#endif
}

static void stats_tap(lv_event_t *e)
{
    show(S_RADAR);
    if (lv_event_get_code(e) == LV_EVENT_GESTURE)
        lv_indev_wait_release(lv_indev_active());
}

static void settings_closed(void)
{
    show(S_RADAR);
}

static void settings_page(int page)
{
    if (page == 0) {
        clockunits_opened();
        show(S_UNITS);
    } else {
        show(S_CREDITS);
    }
}

static void back_to_settings(void)
{
    settings_opened();
    show(S_SETTINGS);
}

static void list_open(const char *hex)
{
    char h[1][8];
    snprintf(h[0], sizeof h[0], "%s", hex);
    open_details(h, 1);
}

static void list_swipe(lv_event_t *e)
{
    (void)e;
    show(S_RADAR);
    lv_indev_wait_release(lv_indev_active());
}

void ui_create(radar_config_t *config)
{
    cfg = config;
    radar_scr = lv_screen_active();
    lv_obj_add_event_cb(radar_scr, radar_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(radar_scr, radar_event, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(radar_scr, radar_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(radar_scr, radar_event, LV_EVENT_LONG_PRESSED, NULL);

    units_init(cfg);
    load_fonts();
    metar_set_source(cfg->metar_url);
    lookup_start(cfg->lookup_url);
    weather_start(cfg->weather_url, cfg->weather_every_s);
    metar_start(cfg->metar_station);
    alerts_init(cfg);
    receiver_start(cfg);
    build_weather();

    stats_scr = receiver_screen_create();
    lv_obj_add_event_cb(stats_scr, stats_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(stats_scr, stats_tap, LV_EVENT_GESTURE, NULL);
    settings_scr = settings_create(cfg, settings_closed, settings_page);
    units_scr = clockunits_create(cfg, back_to_settings);
    credits_scr = credits_create(back_to_settings);
    list_scr = list_create(cfg, list_open);
    lv_obj_add_event_cb(list_scr, list_swipe, LV_EVENT_GESTURE, NULL);

    lv_timer_create(tick_cb, 500, NULL);
}
