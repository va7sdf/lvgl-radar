/*
 * touch-demo - the smallest useful LVGL app on a bare framebuffer.
 *
 * Draws straight to /dev/fb0 and reads the touch screen through evdev:
 * no X, no Wayland, no desktop. Touch the screen and it shows where:
 * crosshairs, the X and Y coordinates, and a trail as you drag.
 *
 * Build:  cmake -B build && cmake --build build -j4
 * Run:    sudo ./build/touch-demo                  (finds the touch screen itself)
 *         sudo ./build/touch-demo /dev/input/by-id/usb-...-event-if00
 *         Ctrl-C to quit; the text console comes back.
 */

#include "lvgl.h"

#include <fcntl.h>
#include <linux/kd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define TRAIL_DOTS 120              /* dots kept while dragging, oldest reused */

static lv_obj_t *h_line, *v_line, *ring, *coords, *hint, *count_label;
static lv_obj_t *dots[TRAIL_DOTS];
static int       next_dot, taps;
static lv_point_precise_t h_pts[2], v_pts[2];

/* ---------- the console ----------
 * The Linux text console (fbcon) shares the framebuffer and would draw its
 * blinking cursor over us. KD_GRAPHICS tells it to stand back; KD_TEXT on
 * the way out gives the console back. */

static int tty_fd = -1;

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
    tty_fd = open("/dev/tty1", O_RDWR | O_CLOEXEC);
    if (tty_fd < 0 || ioctl(tty_fd, KDSETMODE, KD_GRAPHICS) < 0) {
        perror("console (run with sudo to hide the cursor)");
        if (tty_fd >= 0)
            close(tty_fd);
        tty_fd = -1;
        return;
    }
    atexit(console_restore);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
}

/* ---------- the screen ---------- */

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t colour)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

static lv_obj_t *line(lv_obj_t *parent, lv_point_precise_t *pts)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_color(l, lv_color_hex(0x2FA84A), 0);
    lv_obj_set_style_line_width(l, 2, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    return l;
}

/* Every touch event lands here: PRESSED when a finger goes down, PRESSING
   while it stays down (and moves), RELEASED when it lifts. */
static void on_touch(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int32_t w = lv_display_get_horizontal_resolution(NULL);
    int32_t h = lv_display_get_vertical_resolution(NULL);

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        /* Crosshairs through the finger */
        h_pts[0] = (lv_point_precise_t){ 0, p.y };
        h_pts[1] = (lv_point_precise_t){ w, p.y };
        v_pts[0] = (lv_point_precise_t){ p.x, 0 };
        v_pts[1] = (lv_point_precise_t){ p.x, h };
        lv_line_set_points(h_line, h_pts, 2);
        lv_line_set_points(v_line, v_pts, 2);
        lv_obj_remove_flag(h_line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(v_line, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_pos(ring, p.x - 40, p.y - 40);
        lv_obj_remove_flag(ring, LV_OBJ_FLAG_HIDDEN);

        /* A trail of dots, reusing the oldest once we have enough */
        lv_obj_t *d = dots[next_dot];
        next_dot = (next_dot + 1) % TRAIL_DOTS;
        lv_obj_set_pos(d, p.x - 4, p.y - 4);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text_fmt(coords, "X %d   Y %d", (int)p.x, (int)p.y);
        lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
        if (code == LV_EVENT_PRESSED)
            lv_label_set_text_fmt(count_label, "touches: %d", ++taps);
    } else if (code == LV_EVENT_RELEASED) {
        lv_obj_add_flag(h_line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(v_line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_screen(const char *fb_name)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_RELEASED, NULL);

    for (int i = 0; i < TRAIL_DOTS; i++) {
        lv_obj_t *d = lv_obj_create(scr);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(0x1E6B30), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
        dots[i] = d;
    }
    h_line = line(scr, h_pts);
    v_line = line(scr, v_pts);

    ring = lv_obj_create(scr);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 80, 80);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x48FF6A), 0);
    lv_obj_set_style_border_width(ring, 4, 0);
    lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    coords = text(scr, &lv_font_montserrat_48, 0xC8FFD0);
    lv_label_set_text(coords, "Touch the screen");
    lv_obj_align(coords, LV_ALIGN_CENTER, 0, -20);

    hint = text(scr, &lv_font_montserrat_24, 0x3C9C54);
    lv_label_set_text(hint, "drag a finger to draw");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 40);

    count_label = text(scr, &lv_font_montserrat_24, 0x3C9C54);
    lv_label_set_text(count_label, "touches: 0");
    lv_obj_align(count_label, LV_ALIGN_CENTER, 0, 90);

    lv_obj_t *info = text(scr, &lv_font_montserrat_20, 0x3C9C54);
    lv_label_set_text_fmt(info, "LVGL %d.%d.%d  -  %s  %d x %d  -  no X, no Wayland",
                          LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, fb_name,
                          (int)lv_display_get_horizontal_resolution(NULL),
                          (int)lv_display_get_vertical_resolution(NULL));
    lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -160);
}

/* ---------- start-up ---------- */

static uint32_t tick_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void found_device(lv_indev_t *indev, lv_evdev_type_t type, void *user_data)
{
    (void)indev;
    (void)user_data;
    printf("touch-demo: found %s\n", type == LV_EVDEV_TYPE_ABS ? "a touch screen" :
                                     type == LV_EVDEV_TYPE_REL ? "a mouse" : "a keyboard");
}

int main(int argc, char **argv)
{
    const char *fb = getenv("FBDEV") ? getenv("FBDEV") : "/dev/fb0";

    console_graphics();
    lv_init();
    lv_tick_set_cb(tick_ms);

    /* The display: LVGL renders into memory and copies the changed parts
       into the framebuffer device. */
    lv_display_t *disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, fb);

    /* Touch: a device given on the command line, or let LVGL find every
       input device under /dev/input (and any plugged in later). */
    if (argc > 1) {
        if (!lv_evdev_create(LV_INDEV_TYPE_POINTER, argv[1]))
            fprintf(stderr, "touch-demo: can't open %s\n", argv[1]);
    } else {
        lv_evdev_discovery_start(found_device, NULL);
    }

    build_screen(fb);

    /* LVGL's main loop: run timers and redraw, then sleep until the next
       thing is due (but at least ~30 times a second to read touches). */
    for (;;) {
        uint32_t idle = lv_timer_handler();
        usleep((idle > 33 ? 33 : idle) * 1000);
    }
}
