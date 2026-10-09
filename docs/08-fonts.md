---
title: Fonts and icons
---

# 8. Fonts and icons

## Built-in fonts

LVGL fonts are normally **bitmaps compiled into the program**: every
character pre-drawn at one size. `lv_conf.h` picks which sizes of the
Montserrat font to include:

```c
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_48 1
```

They're fast and need no files, but each size costs memory, they stop at
48 pixels, and Montserrat is the only face. For a clock you can read from
across the room, that's not enough.

## TrueType at runtime: Tiny TTF

LVGL includes **Tiny TTF**, which loads an ordinary `.ttf` file and draws
its characters at any size as they're needed, keeping recent ones in a
cache:

```c
// in lv_conf.h:  #define LV_USE_TINY_TTF 1
lv_font_t *big = lv_tiny_ttf_create_data(ttf_bytes, ttf_size, 240);
lv_obj_set_style_text_font(clock_label, big, 0);
```

The radar uses three TrueType fonts, all under the SIL Open Font Licence
and all in `radar/fonts`:

| Font | Used for |
|---|---|
| [DSEG7 Classic](https://www.keshikan.net/fonts-e.html) Bold | The big clock: a seven-segment display look |
| Montserrat Medium | The temperature |
| [Weather Icons](https://erikflowers.github.io/weather-icons/) | The weather symbols |

Any `.ttf` can replace the first two: set `font_file` and `temp_font_file`
in `radar.conf`.

## Fonts that size themselves

Fonts vary a lot in width. At the same size, DSEG7's digits are far wider
than Montserrat's, and the first try ran the clock off the edge of the
round screen. So the radar measures a sample ("00:00") and shrinks the font
until it fits a box that clears the curved edge (simplified here):

```c
lv_font_t *f = lv_tiny_ttf_create_data(data, size, 280);
lv_text_get_size(&s, "00:00", f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
if (s.x > max_w)                            // too wide: scale it down
    f = lv_tiny_ttf_create_data(data, size, 280 * max_w / s.x);
```

Swap in any font and it still fits.

> **Lesson learned:** I wanted "01:58Z" for Zulu time, but seven segments
> can't draw a Z: DSEG7's Z is the same shape as a 2. The Z went, and the
> time zone is on the line below instead.

## Weather icons are a font too

[Erik Flowers' Weather Icons](https://erikflowers.github.io/weather-icons/)
come as a font: each icon is a character in Unicode's *private use area*,
code points set aside for exactly this. Sunny is U+F00D, a night-time
shower U+F029, and so on. To show one, put that character in a label set
in the icon font. In C that means writing the character as UTF-8, three
bytes for these code points:

```c
// U+F00D "day-sunny" as UTF-8
lv_label_set_text(icon, "\xEF\x80\x8D");
```

Open-Meteo reports the weather as a **WMO weather code** (0 clear, 3
overcast, 61 light rain, 95 thunderstorm...), and a table maps each to a
day or night icon. Because icons are font characters, they take the label's
colour, so they're drawn in the same green as everything else, at any
size.

## A misleading error

After adding the icons, the log filled with:

```
ttf_get_glyph_dsc_cb: cache not allocated
```

It wasn't about a cache. A new LVGL label starts with the placeholder text
"Text", and the code set the icon font *before* clearing that text. LVGL
then measured "T", "e", "x", "t" in a font that has only weather symbols,
and Tiny TTF reports a missing character with that message. Clearing the
text before setting the font fixed it.
