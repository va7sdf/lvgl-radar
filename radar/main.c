/*
 * radar - ADS-B radar display for the Waveshare 5" 1080x1080 round panel
 * on a Raspberry Pi Zero 2 W (LVGL on fbdev + evdev, no desktop).
 *
 * Build:  cmake -B build && cmake --build build -j4
 * Run:    sudo ./build/radar [radar.conf]
 *         (root needed for KD_GRAPHICS on the console)
 */

#include "lvgl.h"

#include "config.h"
#include "feed.h"
#include "radar_ui.h"
#include "ui.h"

#include <fcntl.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define CONSOLE_TTY  "/dev/tty1"
#define STALE_AFTER_S 30

static radar_config_t  cfg;
static feed_snapshot_t snaps[MAX_SOURCES];  /* UI thread's copy of each source */
static feed_snapshot_t merged;
static int tty_fd = -1;

/* ---------- console: stop fbcon drawing over the framebuffer ---------- */

static void console_restore(void)
{
    if (tty_fd >= 0) {
        ioctl(tty_fd, KDSETMODE, KD_TEXT);
        close(tty_fd);
        tty_fd = -1;
    }
}

static void on_signal(int sig)
{
    console_restore();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void console_graphics(void)
{
    tty_fd = open(CONSOLE_TTY, O_RDWR | O_CLOEXEC);
    if (tty_fd < 0)     /* not root: the tty group may only write to it */
        tty_fd = open(CONSOLE_TTY, O_WRONLY | O_CLOEXEC);
    if (tty_fd < 0) {
        perror("open " CONSOLE_TTY);
        return;
    }
    if (ioctl(tty_fd, KDSETMODE, KD_GRAPHICS) < 0) {
        perror("KDSETMODE");
        close(tty_fd);
        tty_fd = -1;
        return;
    }
    atexit(console_restore);

    int sigs[] = { SIGINT, SIGTERM, SIGHUP, SIGSEGV, SIGABRT };
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
        signal(sigs[i], on_signal);
}

/* ---------- LVGL glue ---------- */

static uint32_t tick_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Combine the sources into one list. Sources are in priority order, so
   an aircraft seen by more than one keeps the first source's data. */
static void merge_sources(void)
{
    merged.count = 0;
    merged.last_ok = 0;
    for (int s = 0; s < cfg.n_sources; s++) {
        if (snaps[s].last_ok > merged.last_ok)
            merged.last_ok = snaps[s].last_ok;
        for (int i = 0; i < snaps[s].count && merged.count < FEED_MAX_AC; i++) {
            const aircraft_t *a = &snaps[s].ac[i];
            aircraft_t *dup = NULL;
            for (int j = 0; j < merged.count && !dup; j++)
                if (strcmp(merged.ac[j].hex, a->hex) == 0)
                    dup = &merged.ac[j];
            if (!dup) {
                merged.ac[merged.count++] = *a;
            } else {
                /* Keep the first source's position, but borrow details it lacks
                   (your receiver doesn't know registrations; adsb.fi does). */
                if (!dup->reg[0])      memcpy(dup->reg, a->reg, sizeof dup->reg);
                if (!dup->type[0])     memcpy(dup->type, a->type, sizeof dup->type);
                if (!dup->desc[0])     memcpy(dup->desc, a->desc, sizeof dup->desc);
                if (!dup->owner[0])    memcpy(dup->owner, a->owner, sizeof dup->owner);
                if (!dup->category[0]) memcpy(dup->category, a->category, sizeof dup->category);
                dup->military |= a->military;
            }
        }
    }
    merged.gen++;
}

/* Runs on the UI thread: pull any new data and refresh the screen. */
static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    bool changed = false;
    bool ok[MAX_SOURCES];
    time_t now = time(NULL);

    for (int s = 0; s < cfg.n_sources; s++) {
        if (feed_get(s, &snaps[s], snaps[s].gen))
            changed = true;
        time_t last = feed_last_ok(s);
        ok[s] = last != 0 && now - last <= STALE_AFTER_S;
        if (!ok[s] && snaps[s].count) {     /* a dead source's aircraft go away */
            snaps[s].count = 0;
            changed = true;
        }
    }
    if (changed) {
        merge_sources();
        radar_ui_update(&merged);
        ui_update(&merged);
    }
    radar_ui_set_status(ok);
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    const char *conf_path = argc > 1 ? argv[1] : "radar.conf";
    if (config_load(conf_path, &cfg) != 0)
        return 1;
    printf("radar: %.4f, %.4f  range %g nm\n", cfg.lat, cfg.lon, cfg.range_nm);
    for (int s = 0; s < cfg.n_sources; s++)
        printf("radar: source '%s' every %ds: %s\n",
               cfg.sources[s].name, cfg.sources[s].poll_s, cfg.sources[s].url);

    console_graphics();

    lv_init();
    lv_tick_set_cb(tick_ms);

    lv_display_t *disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, cfg.fb_dev);

    if (cfg.touch_dev[0]) {
        lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, cfg.touch_dev);
        if (touch)
            lv_evdev_set_calibration(touch, 0, 0, 4096, 4096);
        else
            fprintf(stderr, "radar: touch device %s not found, continuing without touch\n",
                    cfg.touch_dev);
    }

    radar_ui_create(&cfg);
    ui_create(&cfg);

    if (feed_start(&cfg) != 0)
        return 1;
    lv_timer_create(refresh_cb, 500, NULL);

    for (;;) {
        uint32_t idle = lv_timer_handler();
        if (idle > 33)
            idle = 33;
        usleep(idle * 1000);
    }
}
