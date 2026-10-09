# Touch demo

The smallest useful LVGL program on a Raspberry Pi with no desktop: it
draws straight to the framebuffer (`/dev/fb0`) and reads the touch screen
through evdev. Touch the screen and it shows where, with crosshairs, the X
and Y coordinates and a trail as you drag.

```
sudo apt install git cmake build-essential
git clone --recurse-submodules https://github.com/va7sdf/lvgl-radar.git
cd lvgl-radar/touch-demo
cmake -B build && cmake --build build -j4
sudo ./build/touch-demo
```

Ctrl-C quits and gives the text console back. `sudo` is only needed to hide
the console's blinking cursor; without it the demo still runs.

It finds the touch screen by itself. To pick one, give its path:
`sudo ./build/touch-demo /dev/input/by-id/usb-...-event-if00`
(`ls /dev/input/by-id/` lists them). For a different framebuffer, set
`FBDEV=/dev/fb1`.

Everything is in [main.c](main.c), about 200 lines with comments.
