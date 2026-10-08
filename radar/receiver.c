/* Your receiver: dump1090 statistics and how far it hears in each direction. */

#include "receiver.h"

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define COL_BG    lv_color_hex(0x031A09)
#define COL_RIM   lv_color_hex(0x2FA84A)
#define COL_RING  lv_color_hex(0x1E5C2C)
#define COL_DIM   lv_color_hex(0x3C9C54)
#define COL_TEXT  lv_color_hex(0xC8FFD0)
#define COL_TODAY lv_color_hex(0x48FF6A)
#define COL_BEST  lv_color_hex(0x2E7A63)

#define PLOT_CX   540
#define PLOT_CY   410
#define PLOT_R    250
#define MAX_PLAUSIBLE_NM 400    /* ignore positions further than this */
#define SAVE_EVERY_S     300

static const radar_config_t *cfg;

/* stats.json, from the fetch thread */
static pthread_mutex_t   lock = PTHREAD_MUTEX_INITIALIZER;
static receiver_stats_t  stats;

/* range history (UI thread only) */
static double today[RANGE_BINS], best[RANGE_BINS];
static char   today_date[12];
static double far_nm, far_brg;
static char   far_who[12];
static int    seen_local, seen_total;
static bool   dirty;
static time_t saved_at;

/* screen */
static lv_obj_t *scr, *line_today, *line_best, *rings[4], *ring_labels[4], *info, *far_label;
static lv_point_precise_t pts_today[RANGE_BINS + 1], pts_best[RANGE_BINS + 1];

/* ---------- stats.json ---------- */

static double num(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : NAN;
}

bool receiver_parse_stats(const char *json, receiver_stats_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_Parse(json);
    const cJSON *m = root ? cJSON_GetObjectItemCaseSensitive(root, "last1min") : NULL;
    const cJSON *local = cJSON_IsObject(m) ? cJSON_GetObjectItemCaseSensitive(m, "local") : NULL;
    if (cJSON_IsObject(local)) {
        double secs = num(m, "end") - num(m, "start");
        double msgs = num(m, "messages");
        out->msg_per_s = secs > 0 && !isnan(msgs) ? msgs / secs : NAN;
        out->signal = num(local, "signal");
        out->noise = num(local, "noise");
        out->peak = num(local, "peak_signal");
        out->gain = num(local, "gain_db");

        double accepted = 0;
        const cJSON *acc = cJSON_GetObjectItemCaseSensitive(local, "accepted");
        const cJSON *a;
        cJSON_ArrayForEach(a, acc)
            if (cJSON_IsNumber(a))
                accepted += a->valuedouble;
        double strong = num(local, "strong_signals");
        out->strong_pct = accepted > 0 && !isnan(strong) ? 100.0 * strong / accepted : NAN;
        out->valid = true;
    }
    cJSON_Delete(root);
    return out->valid;
}

typedef struct {
    char  *data;
    size_t len;
} buffer_t;

static size_t on_data(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    buffer_t *buf = userdata;
    size_t n = size * nmemb;
    char *grown = realloc(buf->data, buf->len + n + 1);
    if (!grown)
        return 0;
    buf->data = grown;
    memcpy(buf->data + buf->len, ptr, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return n;
}

static void *stats_thread(void *arg)
{
    (void)arg;
    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;
    curl_easy_setopt(curl, CURLOPT_URL, cfg->stats_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    for (;;) {
        buffer_t buf = { 0 };
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        receiver_stats_t s;
        if (curl_easy_perform(curl) == CURLE_OK && buf.data && receiver_parse_stats(buf.data, &s)) {
            pthread_mutex_lock(&lock);
            stats = s;
            pthread_mutex_unlock(&lock);
        }
        free(buf.data);
        sleep(30);
    }
    return NULL;
}

/* ---------- range history ---------- */

static void local_date(char *out, size_t size)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(out, size, "%Y-%m-%d", &tm);
}

static void load_ranges(void)
{
    local_date(today_date, sizeof today_date);
    if (!cfg->range_file[0])
        return;
    FILE *f = fopen(cfg->range_file, "r");
    if (!f)
        return;
    char date[16] = "", key[16];
    while (fscanf(f, "%15s", key) == 1) {
        if (!strcmp(key, "date")) {
            if (fscanf(f, "%15s", date) != 1)
                break;
        } else if (!strcmp(key, "best") || !strcmp(key, "today")) {
            double *bins = !strcmp(key, "best") ? best : today;
            for (int i = 0; i < RANGE_BINS; i++)
                if (fscanf(f, "%lf", &bins[i]) != 1)
                    bins[i] = 0;
        } else if (!strcmp(key, "farthest")) {
            if (fscanf(f, "%lf %lf %11s", &far_nm, &far_brg, far_who) != 3)
                far_nm = 0;
        }
    }
    fclose(f);
    if (strcmp(date, today_date) != 0) {        /* saved on an earlier day */
        memset(today, 0, sizeof today);
        far_nm = 0;
    }
}

static void save_ranges(void)
{
    if (!cfg->range_file[0])
        return;
    FILE *f = fopen(cfg->range_file, "w");
    if (!f) {
        fprintf(stderr, "receiver: cannot write %s\n", cfg->range_file);
        return;
    }
    fprintf(f, "date %s\nbest", today_date);
    for (int i = 0; i < RANGE_BINS; i++)
        fprintf(f, " %.1f", best[i]);
    fprintf(f, "\ntoday");
    for (int i = 0; i < RANGE_BINS; i++)
        fprintf(f, " %.1f", today[i]);
    fprintf(f, "\nfarthest %.1f %.0f %s\n", far_nm, far_brg, far_who[0] ? far_who : "-");
    fclose(f);
    dirty = false;
    saved_at = time(NULL);
}

void receiver_track(const feed_snapshot_t *merged)
{
    char date[12];
    local_date(date, sizeof date);
    if (strcmp(date, today_date) != 0) {        /* midnight: a new day */
        snprintf(today_date, sizeof today_date, "%s", date);
        memset(today, 0, sizeof today);
        far_nm = 0;
        dirty = true;
    }

    const double coslat = cos(cfg->lat * M_PI / 180.0);
    seen_local = 0;
    seen_total = merged->count;
    for (int i = 0; i < merged->count; i++) {
        const aircraft_t *a = &merged->ac[i];
        if (a->source != 0)
            continue;                           /* only your own receiver counts */
        seen_local++;
        if (!isnan(a->seen_pos) && a->seen_pos > 10)
            continue;
        double n = (a->lat - cfg->lat) * 60.0, e = (a->lon - cfg->lon) * 60.0 * coslat;
        double d = hypot(n, e);
        if (d > MAX_PLAUSIBLE_NM)
            continue;
        double brg = fmod(atan2(e, n) * 180.0 / M_PI + 360.0, 360.0);
        int bin = (int)(brg / (360.0 / RANGE_BINS)) % RANGE_BINS;
        if (d > today[bin]) {
            today[bin] = d;
            dirty = true;
        }
        if (d > best[bin])
            best[bin] = d;
        if (d > far_nm) {
            far_nm = d;
            far_brg = brg;
            snprintf(far_who, sizeof far_who, "%s", a->flight[0] ? a->flight : a->hex);
        }
    }
    if (dirty && time(NULL) - saved_at >= SAVE_EVERY_S)
        save_ranges();
}

/* ---------- screen ---------- */

static double nice_step(double max_nm)
{
    static const double steps[] = { 5, 10, 25, 50, 75, 100, 150, 200 };
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++)
        if (max_nm <= steps[i] * 4)
            return steps[i];
    return 250;
}

static void plot(lv_point_precise_t *pts, const double *bins, double scale)
{
    for (int i = 0; i <= RANGE_BINS; i++) {
        int k = i % RANGE_BINS;
        double a = (k + 0.5) * (2.0 * M_PI / RANGE_BINS);   /* middle of the bin */
        double r = bins[k] * scale;
        pts[i].x = (lv_value_precise_t)lround(PLOT_CX + r * sin(a));
        pts[i].y = (lv_value_precise_t)lround(PLOT_CY - r * cos(a));
    }
}

void receiver_refresh(void)
{
    double max_nm = 0;
    for (int i = 0; i < RANGE_BINS; i++)
        max_nm = fmax(max_nm, fmax(best[i], today[i]));
    double step = nice_step(max_nm);
    double scale = PLOT_R / (step * 4);

    for (int i = 0; i < 4; i++) {
        int r = (int)lround(PLOT_R * (i + 1) / 4.0);
        lv_obj_set_size(rings[i], 2 * r, 2 * r);
        lv_obj_set_pos(rings[i], PLOT_CX - r, PLOT_CY - r);
        lv_label_set_text_fmt(ring_labels[i], "%g", step * (i + 1));
        lv_obj_set_pos(ring_labels[i], PLOT_CX + (int)(r * 0.71) + 4, PLOT_CY - (int)(r * 0.71) - 22);
    }
    plot(pts_today, today, scale);
    plot(pts_best, best, scale);
    lv_line_set_points(line_today, pts_today, RANGE_BINS + 1);
    lv_line_set_points(line_best, pts_best, RANGE_BINS + 1);

    receiver_stats_t s;
    pthread_mutex_lock(&lock);
    s = stats;
    pthread_mutex_unlock(&lock);

    char line1[160], line2[160];
    if (s.valid) {
        snprintf(line1, sizeof line1, "%.0f messages/s   gain %.1f dB", s.msg_per_s, s.gain);
        snprintf(line2, sizeof line2, "signal %.1f   noise %.1f   peak %.1f dBFS", s.signal, s.noise, s.peak);
    } else {
        snprintf(line1, sizeof line1, "%s", cfg->stats_url[0] ? "waiting for receiver stats"
                                                              : "no stats_url set");
        line2[0] = '\0';
    }
    lv_label_set_text_fmt(info, "%s\n%s\nNow: %d from your receiver, %d in total",
                          line1, line2, seen_local, seen_total);
    if (far_nm > 0)
        lv_label_set_text_fmt(far_label, "Farthest today: %s, %.0f nm at %03.0f°", far_who, far_nm, far_brg);
    else
        lv_label_set_text(far_label, "Farthest today: -");
}

static lv_obj_t *text(const lv_font_t *font, lv_color_t col, int y)
{
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, "");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static lv_obj_t *polyline(lv_color_t col, int width)
{
    lv_obj_t *l = lv_line_create(scr);
    lv_obj_set_style_line_color(l, col, 0);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

lv_obj_t *receiver_screen_create(void)
{
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

    lv_label_set_text(text(&lv_font_montserrat_24, COL_TEXT, 90), "Your receiver");

    for (int i = 0; i < 4; i++) {
        rings[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(rings[i]);
        lv_obj_set_style_radius(rings[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(rings[i], i == 3 ? COL_DIM : COL_RING, 0);
        lv_obj_set_style_border_width(rings[i], 2, 0);
        lv_obj_clear_flag(rings[i], LV_OBJ_FLAG_CLICKABLE);
        ring_labels[i] = lv_label_create(scr);
        lv_obj_set_style_text_font(ring_labels[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(ring_labels[i], COL_DIM, 0);
    }
    lv_obj_t *n = lv_label_create(scr);
    lv_obj_set_style_text_font(n, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(n, COL_DIM, 0);
    lv_label_set_text(n, "N");
    lv_obj_set_pos(n, PLOT_CX - 7, PLOT_CY - PLOT_R - 28);

    line_best = polyline(COL_BEST, 3);
    line_today = polyline(COL_TODAY, 3);

    lv_obj_t *legend = text(&lv_font_montserrat_20, COL_DIM, PLOT_CY + PLOT_R + 14);
    lv_label_set_recolor(legend, true);
    lv_label_set_text(legend, "#48FF6A today#    #2E7A63 best ever#   (nm)");

    info = text(&lv_font_montserrat_20, COL_TEXT, 712);
    far_label = text(&lv_font_montserrat_20, COL_DIM, 800);
    return scr;
}

void receiver_start(const radar_config_t *config)
{
    cfg = config;
    load_ranges();
    if (cfg->stats_url[0]) {
        pthread_t tid;
        if (pthread_create(&tid, NULL, stats_thread, NULL) == 0)
            pthread_detach(tid);
    }
}
