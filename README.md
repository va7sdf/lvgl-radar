# LVGL on a Raspberry Pi framebuffer

Graphical, touch-driven apps on a Raspberry Pi with no X or Wayland:
[LVGL](https://lvgl.io) draws straight to the Linux framebuffer and reads
touches from evdev, so a Pi Zero 2 boots to the app in seconds.

| Folder | What's in it |
|---|---|
| [touch-demo](touch-demo/) | The first step: touch the screen, see the coordinates |
| [radar](radar/) | A radar-style ADS-B aircraft display for a 5" round screen |
| `lv_conf.h` | LVGL settings shared by the projects here |
| `lvgl/` | LVGL v9.3.0, as a git submodule |

Clone with the submodule:

```
git clone --recurse-submodules https://github.com/<you>/lvgl-radar.git
```

BSD 2-Clause licence; see [LICENSE](LICENSE).
