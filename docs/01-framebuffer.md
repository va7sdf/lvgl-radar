---
title: Why skip the desktop?
---

# 1. Why skip the desktop?

A Raspberry Pi with a desktop runs a display server (X11 or Wayland), a
window manager, a panel, and then your program in a window. That's the
right choice for a general-purpose computer. For a gadget with one job,
like a clock, a dashboard or a radar, it's a lot of machinery between your
program and the glass:

- **Memory.** The Pi Zero 2 W has 512 MB. A desktop takes a good share
  of that before your program starts.
- **Boot time.** Pi OS Lite goes straight from boot to your app, with no desktop to load first.
- **Simplicity.** No auto-login, no hiding the mouse pointer, no screen
  blanking to fight with, no window to make full-screen.

Linux already has everything needed to draw and to read touches without
any of it.

## The framebuffer: the screen as a file

`/dev/fb0` is the screen's memory, presented as a file. Write bytes to it
and pixels change. On this screen each pixel is two bytes in a format
called RGB565 (5 bits red, 6 green, 5 blue), so 1080 × 1080 pixels is
2,332,800 bytes. You can try it from the shell:

```
cat /sys/class/graphics/fb0/virtual_size     # 1080,1080
cat /sys/class/graphics/fb0/bits_per_pixel   # 16
cat /dev/urandom > /dev/fb0                  # snow! (Ctrl-C)
```

It even works from Python:

```python
# Paint the top 100 rows green: RGB565 green is 0x07E0
with open("/dev/fb0", "wb") as fb:
    fb.write(b"\xe0\x07" * 1080 * 100)
```

Drawing text, buttons and animations by poking bytes gets old quickly,
though. That's what LVGL is for.

## evdev: touches as a stream of events

Every input device (keyboard, mouse, touch screen) appears under
`/dev/input/` as an *event device*. Reading it gives a stream of small
records: "X is now 2048", "Y is now 1310", "finger down", "sync". The
`evtest` tool prints them, which is the best way to check a touch screen
works (chapter 2).

## LVGL: a GUI library for small screens

[LVGL](https://lvgl.io) (Light and Versatile Graphics Library) is a C
library for graphical interfaces on microcontrollers and small Linux
boards. It's MIT-licensed and used in everything from smart watches to
3D printers. It gives you:

- **Widgets**: labels, buttons, sliders, lists, charts, tabs, canvases and more
- **Styles**: colours, borders, radius, shadows, opacity, like a small CSS
- **Events**: pressed, clicked, long-pressed, gestures (swipes), value changed
- **Fonts**: built-in, converted bitmaps, or TrueType rendered at runtime
- **Drivers**: for the Linux framebuffer and evdev, among many others

LVGL never talks to the hardware directly. It draws into a buffer in
memory and hands the changed rectangles to a *display driver*, which here
copies them into `/dev/fb0`. Touches come from an *input driver*, here
evdev. Swap the drivers and the same program runs on an ESP32, in an SDL
window on your laptop, or on a Linux DRM display.

### The main loop

An LVGL program builds its screens, then loops forever, letting LVGL do
whatever is due:

```c
for (;;) {
    uint32_t idle = lv_timer_handler();   // redraw, read touch, run timers
    usleep(idle * 1000);                  // sleep until the next job
}
```

Python folks will recognise it: it's an event loop, like `asyncio` or
Tkinter's `mainloop()`. Your code runs in *callbacks*: functions LVGL calls
when a button is pressed or a timer fires.

### Configuring LVGL

LVGL is compiled with your program, and a header file, `lv_conf.h`,
switches its features on and off. The ones that matter here:

```c
#define LV_COLOR_DEPTH      16   // match the framebuffer's RGB565
#define LV_USE_LINUX_FBDEV   1   // display driver: /dev/fb0
#define LV_USE_EVDEV         1   // input driver: /dev/input/event*
#define LV_USE_TINY_TTF      1   // TrueType fonts at runtime (chapter 8)
#define LV_FONT_MONTSERRAT_24 1  // built-in fonts, one per size
```

The repo has one `lv_conf.h` at the top, shared by both projects.

## The console gets in the way

The Linux text console also draws on the framebuffer. Start a graphical
program from it and the console's blinking cursor keeps punching a hole
in your picture. The fix is one `ioctl` call that tells the console to
stand back, and another to give the screen back on the way out:

```c
int tty = open("/dev/tty1", O_RDWR);
ioctl(tty, KDSETMODE, KD_GRAPHICS);   // console: hands off the screen
/* ... run ... */
ioctl(tty, KDSETMODE, KD_TEXT);       // console back (also on Ctrl-C)
```

It needs root, or the `CAP_SYS_TTY_CONFIG` capability, which is how the
radar's systemd service does it without running as root (chapter 11).

## What about Python?

LVGL has an official MicroPython binding, and on a full Pi there are other
routes to a desktop-free screen: pygame (SDL2) can draw through KMS/DRM,
and simple dashboards can write images straight to `/dev/fb0` with Pillow.
I chose C and LVGL for speed on the Zero 2 (the radar sweep redraws a lot
of pixels many times a second) and because the same skills carry over to
microcontrollers.
