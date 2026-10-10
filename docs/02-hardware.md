---
title: The hardware
---

# 2. The hardware

## Parts

| Part | Notes |
|---|---|
| Raspberry Pi Zero 2 WH | The "H" has the GPIO header soldered on. Any Pi works; a Pi 4 or 5 has power to spare |
| Waveshare 5" round LCD, 1080 × 1080 | HDMI for the picture, USB for the 10-point capacitive touch |
| Mini-HDMI to HDMI cable (or adapter) | The Zero has a mini-HDMI socket |
| Micro-USB OTG cable | From the Zero's USB data port to the screen's touch port |
| 5 V 2.5 A power supply | Into the Zero's power port |
| A second 5 V supply | For the screen, into its power port |
| microSD card, 16 GB or more | Raspberry Pi OS Lite (64-bit) |
| An ADS-B receiver (optional) | Mine is dump1090-fa on another Pi. Without one, the radar can use adsb.fi alone |

I power the screen from a second Raspberry Pi power supply I had to hand.
The screen can also take power through its touch USB port. Whether one
supply can run both the Pi and the screen that way, with touch still
working, is something to try for yourself.

> **Lesson learned:** when the screen went blank one evening, after a lot of
> head-scratching the mini-HDMI plug turned out to be half out of the
> socket. Check the cable first.

## Install Raspberry Pi OS Lite

Use Raspberry Pi Imager to write **Raspberry Pi OS Lite (64-bit)**. In its
settings, set the hostname (mine is `radar`), your user, Wi-Fi and SSH, so
the Pi is on the network on first boot and you can work over SSH.

## Tell the Pi about the round screen

A square 1080 × 1080 HDMI panel isn't a mode the Pi guesses on its own.
Add this to the end of the single line in `/boot/firmware/cmdline.txt`
(not on a new line):

```
video=HDMI-A-1:1080x1080@60
```

> **Lesson learned:** guides for older Pi OS versions put `hdmi_group`,
> `hdmi_mode` and `hdmi_timings` in `config.txt`. With today's KMS
> graphics driver the `video=` setting in `cmdline.txt` is what counts.

### Keep boot messages off the screen

By default the kernel prints its boot messages on the screen, and they can
end up showing around the edges of the app. In the same line of
`cmdline.txt`, change `console=tty1` to `console=tty3`, so the messages go
to a console nobody looks at, and add:

```
quiet loglevel=3 logo.nologo vt.global_cursor_default=0
```

These print only serious kernel messages, drop the Raspberry Pi logos at
boot, and hide the console's blinking cursor. Leave
`console=serial0,115200` alone if it's there, and keep everything on one
line. You can still see the boot messages with `dmesg` or `journalctl -b`.

Reboot, and check the framebuffer agrees:

```
cat /sys/class/graphics/fb0/virtual_size     # should say 1080,1080
cat /sys/class/graphics/fb0/bits_per_pixel   # 16
```

The screen's backlight button cycles brightness, and one press turns it
off. If the picture vanishes, it may just be dark.

## Find the touch panel

Install `evtest`, run it, and pick the Waveshare device from its list:

```
sudo apt install evtest
sudo evtest
```

Touch the screen and you'll see `ABS_X`, `ABS_Y` and `BTN_TOUCH` events
scroll by. Corners show the range of the numbers: this panel reports
roughly 0 to 4095 in each direction, which LVGL scales to 0–1079.

Device numbers like `event2` can change between boots, so it's better to
use the stable name under `/dev/input/by-id/`:

```
ls /dev/input/by-id/
# usb-Waveshare_Waveshare_-079-HD-event-if00
```

## Permissions

Your user needs to be in the `video` group (the framebuffer) and the
`input` group (the touch screen). The default Pi OS user is in both.
Check with `groups`.
