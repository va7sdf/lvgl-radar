/* Settings: sweep, frame rate, track history and range, applied live and
   saved back to radar.conf; plus how the Pi is doing. */

#include "settings.h"

#include "face.h"
#include "pistats.h"
#include "radar_ui.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#if LV_FONT_MONTSERRAT_16
#define FONT_SETTINGS_SMALL (&lv_font_montserrat_16)
#else
#define FONT_SETTINGS_SMALL (&lv_font_montserrat_14)
#endif

#define COL_BG    lv_color_hex(0x031A09)
#define COL_RIM   lv_color_hex(0x2FA84A)
#define COL_DIM   lv_color_hex(0x3C9C54)
#define COL_TRACK lv_color_hex(0x16401F)
#define COL_TEXT  lv_color_hex(0xC8FFD0)

typedef enum { SET_PERIOD, SET_TRAIL, SET_FPS, SET_HISTORY, N_SLIDERS } slider_id_t;

typedef struct {
    const char *key;        /* radar.conf key */
    int         min, max, step;
    lv_obj_t   *slider, *label;
    bool        changed;
} slider_t;

static radar_config_t *cfg;
static void (*on_close)(void);
static void (*on_page)(int page);
static lv_obj_t *scr, *pi_label, *draw_label, *map_btn, *map_status;

/* Map rebuild, run in the background: 0 idle, 1 running, 2 done, 3 failed */
static volatile int map_job;
static time_t map_job_started;
static slider_t sliders[N_SLIDERS] = {
    [SET_PERIOD]  = { "sweep_period_s", 2, 12, 1 },
    [SET_TRAIL]   = { "sweep_trail_deg", 0, 90, 5 },
    [SET_FPS]     = { "sweep_fps", 5, 30, 1 },
    [SET_HISTORY] = { "trail_s", 0, 300, 30 },
};

static int get_value(slider_id_t id)
{
    switch (id) {
    case SET_PERIOD:  return (int)lround(cfg->sweep_period_s);
    case SET_TRAIL:   return (int)lround(cfg->sweep_trail_deg);
    case SET_FPS:     return cfg->sweep_fps;
    case SET_HISTORY: return cfg->trail_s;
    default:          return 0;
    }
}

static void set_value(slider_id_t id, int v)
{
    switch (id) {
    case SET_PERIOD:  cfg->sweep_period_s = v; break;
    case SET_TRAIL:   cfg->sweep_trail_deg = v; break;
    case SET_FPS:     cfg->sweep_fps = v; break;
    case SET_HISTORY: cfg->trail_s = v; break;
    default: break;
    }
}

static void describe(slider_id_t id, int v, char *buf, size_t size)
{
    switch (id) {
    case SET_PERIOD:  snprintf(buf, size, "Sweep: one turn every %d s", v); break;
    case SET_TRAIL:   v ? snprintf(buf, size, "Sweep trail: %d°", v)
                        : snprintf(buf, size, "Sweep: off (aircraft update at once)"); break;
    case SET_FPS:     snprintf(buf, size, "Frame rate: %d fps", v); break;
    case SET_HISTORY: v ? snprintf(buf, size, "Track history: %d s", v)
                        : snprintf(buf, size, "Track history: off"); break;
    default: buf[0] = '\0';
    }
}

static int snap(const slider_t *s, int v)
{
    v = (v - s->min + s->step / 2) / s->step * s->step + s->min;
    return v < s->min ? s->min : v > s->max ? s->max : v;
}

static void slider_cb(lv_event_t *e)
{
    slider_id_t id = (slider_id_t)(intptr_t)lv_event_get_user_data(e);
    slider_t *s = &sliders[id];
    int v = snap(s, lv_slider_get_value(s->slider));
    char buf[64];
    describe(id, v, buf, sizeof buf);
    lv_label_set_text(s->label, buf);

    /* Apply when the finger lifts, so dragging doesn't restart the sweep */
    if (lv_event_get_code(e) == LV_EVENT_RELEASED && v != get_value(id)) {
        lv_slider_set_value(s->slider, v, LV_ANIM_OFF);
        set_value(id, v);
        s->changed = true;
        radar_ui_apply_settings();
    }
}

static void *map_thread(void *arg)
{
    (void)arg;
    /* Cover the largest range with some margin, so labels near the edge
       and a later, bigger range_max still have map */
    double widest = cfg->range_nm;
    for (int i = 0; i < cfg->n_ranges; i++)
        widest = fmax(widest, cfg->ranges[i]);
    /* Started with sudo? Then build the map as you, so the files it writes
       stay yours and you can run make_map.py by hand later. */
    char as_user[96] = "";
    const char *user = getenv("SUDO_USER");
    if (user && user[0] && geteuid() == 0)
        snprintf(as_user, sizeof as_user, "runuser -u '%s' -- ", user);
    char cmd[1200];
    snprintf(cmd, sizeof cmd,
             "%spython3 make_map.py --download --conf '%s' --radius %d -o '%s' %s > map.log 2>&1",
             as_user, cfg->path, (int)ceil(fmax(50, widest * 2)), cfg->map_file, cfg->map_options);
    int rc = system(cmd);
    map_job = rc == 0 ? 2 : 3;
    return NULL;
}

static void map_cb(lv_event_t *e)
{
    (void)e;
    if (map_job == 1 || !cfg->map_file[0])
        return;
    map_job = 1;
    map_job_started = time(NULL);
    pthread_t tid;
    if (pthread_create(&tid, NULL, map_thread, NULL) == 0)
        pthread_detach(tid);
    else
        map_job = 3;
    lv_label_set_text(map_status, "Updating... the first time downloads about 130 MB");
}

/* Runs on the UI thread: notice when the rebuild has finished */
static void map_poll_cb(lv_timer_t *t)
{
    (void)t;
    if (map_job == 2) {
        map_job = 0;
        radar_ui_range_changed();           /* redraws the face from the new file */
        lv_label_set_text(map_status, "Map updated");
    } else if (map_job == 3) {
        map_job = 0;
        lv_label_set_text(map_status, "Map update failed - see map.log");
    } else if (map_job == 1) {
        lv_label_set_text_fmt(map_status, "Updating... %ld s (first time downloads about 130 MB)",
                              (long)(time(NULL) - map_job_started));
    }
}

void settings_save(void)
{
    char v[32];
    for (int i = 0; i < N_SLIDERS; i++) {
        if (!sliders[i].changed)
            continue;
        snprintf(v, sizeof v, "%d", get_value((slider_id_t)i));
        if (config_save_value(cfg->path, sliders[i].key, v) != 0)
            fprintf(stderr, "settings: could not save %s to %s\n", sliders[i].key, cfg->path);
        sliders[i].changed = false;
    }
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    settings_save();
    if (on_close)
        on_close();
}

void settings_opened(void)
{
    for (int i = 0; i < N_SLIDERS; i++) {
        char buf[64];
        int v = get_value((slider_id_t)i);
        lv_slider_set_value(sliders[i].slider, v, LV_ANIM_OFF);
        describe((slider_id_t)i, v, buf, sizeof buf);
        lv_label_set_text(sliders[i].label, buf);
    }
    settings_refresh();
}

void settings_refresh(void)
{
    pistats_t s;
    pistats_read(&s);
    char up[32];
    long d = s.uptime_s / 86400, h = s.uptime_s / 3600 % 24, m = s.uptime_s / 60 % 60;
    if (d)
        snprintf(up, sizeof up, "%ldd %ldh", d, h);
    else
        snprintf(up, sizeof up, "%ldh %ldm", h, m);

    char cpu[48] = "CPU -";
    if (!isnan(s.cpu_pct))
        snprintf(cpu, sizeof cpu, "CPU %.0f%% (radar %.0f%%)", s.cpu_pct, s.app_pct);
    char wifi[96] = "";
    if (s.wifi_dbm)
        snprintf(wifi, sizeof wifi, "\nWi-Fi %s%s%d dBm", s.ssid, s.ssid[0] ? "  " : "", s.wifi_dbm);

    char temp[16] = "";
    if (!isnan(s.temp_c))
        snprintf(temp, sizeof temp, "   %.0f°C", s.temp_c);
    lv_label_set_text_fmt(pi_label,
        "%s%s   load %.2f\n"
        "Memory %ld of %ld MB free   Disk %.1f GB free\n"
        "Power: %s   Up %s\n"
        "%s  %s%s",
        cpu, temp, s.load1,
        s.mem_avail_mb, s.mem_total_mb, s.disk_free_mb / 1024.0,
        pistats_throttle_text(&s), up,
        s.host, s.ip[0] ? s.ip : "no network", wifi);
    lv_obj_set_style_text_color(pi_label,
        s.throttle_known && (s.throttled & 0xF) ? lv_color_hex(0xFF3B30) : COL_DIM, 0);

    double ms = face_sweep_draw_ms();
    int fps = cfg->sweep_fps > 0 ? cfg->sweep_fps : 20;
    if (ms > 0)
        lv_label_set_text_fmt(draw_label, "Each sweep frame takes %.1f ms of the %d ms available",
                              ms, 1000 / fps);
    else
        lv_label_set_text(draw_label, "");
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t col, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, "");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static void page_cb(lv_event_t *e)
{
    settings_save();
    if (on_page)
        on_page((int)(intptr_t)lv_event_get_user_data(e));
}

lv_obj_t *settings_create(radar_config_t *config, void (*close)(void), void (*open_page)(int page))
{
    cfg = config;
    on_close = close;
    on_page = open_page;

    scr = lv_obj_create(NULL);
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

    lv_label_set_text(text(scr, &lv_font_montserrat_24, COL_TEXT, 105), "Settings");

    /* Sliders */
    for (int i = 0; i < N_SLIDERS; i++) {
        slider_t *s = &sliders[i];
        int y = 150 + i * 92;
        s->label = text(scr, &lv_font_montserrat_20, COL_TEXT, y);
        s->slider = lv_slider_create(scr);
        lv_obj_set_width(s->slider, 600);
        lv_obj_align(s->slider, LV_ALIGN_TOP_MID, 0, y + 40);
        lv_slider_set_range(s->slider, s->min, s->max);
        lv_obj_set_style_bg_color(s->slider, COL_TRACK, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s->slider, COL_RIM, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s->slider, COL_TEXT, LV_PART_KNOB);
        lv_obj_set_style_pad_all(s->slider, 10, LV_PART_KNOB);
        lv_obj_set_ext_click_area(s->slider, 20);
        lv_obj_add_event_cb(s->slider, slider_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(s->slider, slider_cb, LV_EVENT_RELEASED, (void *)(intptr_t)i);
    }

    draw_label = text(scr, FONT_SETTINGS_SMALL, COL_DIM, 520);

    /* Rebuild the map with make_map.py */
    /* A row of buttons: map rebuild, clock & units, credits */
    static const char *names[] = { "Update map", "Clock & units", "Credits" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_size(b, 200, 52);
        lv_obj_set_pos(b, 540 - 316 + i * 216, 560);
        lv_obj_set_style_radius(b, 26, 0);
        lv_obj_set_style_bg_color(b, COL_BG, 0);
        lv_obj_set_style_border_color(b, COL_RIM, 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(l, COL_TEXT, 0);
        lv_label_set_text(l, names[i]);
        lv_obj_center(l);
        if (i == 0) {
            map_btn = b;
            lv_obj_add_event_cb(b, map_cb, LV_EVENT_CLICKED, NULL);
        } else {
            lv_obj_add_event_cb(b, page_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i - 1));
        }
    }
    map_status = text(scr, FONT_SETTINGS_SMALL, COL_DIM, 620);

    pi_label = text(scr, &lv_font_montserrat_20, COL_DIM, 660);

    lv_obj_t *done = lv_button_create(scr);
    lv_obj_set_size(done, 200, 60);
    lv_obj_align(done, LV_ALIGN_TOP_MID, 0, 840);
    lv_obj_set_style_radius(done, 30, 0);
    lv_obj_set_style_bg_color(done, COL_RIM, 0);
    lv_obj_set_style_shadow_width(done, 0, 0);
    lv_obj_t *dl = lv_label_create(done);
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(dl, COL_BG, 0);
    lv_label_set_text(dl, "Close");
    lv_obj_center(dl);
    lv_obj_add_event_cb(done, close_cb, LV_EVENT_CLICKED, NULL);

    lv_timer_create(map_poll_cb, 1000, NULL);
    return scr;
}
