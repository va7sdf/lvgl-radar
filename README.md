# LVGL on a Raspberry Pi framebuffer

Graphical, touch-driven apps on a Raspberry Pi with no X or Wayland:
[LVGL](https://lvgl.io) draws straight to the Linux framebuffer and reads
touches from evdev, so a Pi Zero 2 boots to the app in seconds.

**Read the write-up:** [va7sdf.github.io/lvgl-radar](https://va7sdf.github.io/lvgl-radar/)
(the same pages are in [docs/](docs/)).

| Folder | What's in it |
|---|---|
| [touch-demo](touch-demo/) | The first step: touch the screen, see the coordinates |
| [radar](radar/) | A radar-style ADS-B aircraft display for a 5" round screen |
| [tools/fbmirror.py](tools/fbmirror.py) | Shows the Pi's screen live in a web browser, for projectors and video calls |
| [docs](docs/) | The write-up, for GitHub Pages, a web page or a PDF |
| `lv_conf.h` | LVGL settings shared by the projects here |
| `lvgl/` | LVGL v9.3.0, as a git submodule |

Clone with the submodule:

```
git clone --recurse-submodules https://github.com/va7sdf/lvgl-radar.git
```

BSD 2-Clause licence; see [LICENSE](LICENSE).
