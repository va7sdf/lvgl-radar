#!/usr/bin/env python3
"""
Build the docs as one self-contained web page, and a PDF of it.

  sudo apt install python3-markdown       # once
  ./build.py                              # writes out/lvgl-radar-docs.html
  ./build.py --pdf                        # and out/lvgl-radar-docs.pdf

The PDF needs Python Playwright with Chromium; without it, open the HTML
in a browser and print it to PDF. GitHub Pages renders the same Markdown
files itself (see _config.yml), so this is for hosting elsewhere.
"""

import argparse
import base64
import os
import re
import sys

import markdown

HERE = os.path.dirname(os.path.abspath(__file__))


def chapters():
    with open(os.path.join(HERE, "_config.yml")) as f:
        text = f.read()
    block = text.split("chapters:", 1)[1]
    return re.findall(r"^\s+-\s+(\S+\.md)\s*$", block.split("\nexclude:")[0], re.M)


def anchor(name):
    return "ch-" + name[:-3]


def render(name):
    with open(os.path.join(HERE, name)) as f:
        text = f.read()
    text = re.sub(r"\A---\n.*?\n---\n", "", text, flags=re.S)      # front matter
    # Links to other chapters become links within the one page
    text = re.sub(r"\]\((\d\d-[\w-]+|index)\.md\)", lambda m: "](#" + anchor(m.group(1) + ".md") + ")", text)
    html = markdown.markdown(text, extensions=["tables", "fenced_code", "sane_lists"])
    # Images inline, so the page stands alone
    def inline(m):
        path = os.path.join(HERE, m.group(1))
        with open(path, "rb") as f:
            data = base64.b64encode(f.read()).decode()
        return 'src="data:image/png;base64,%s"' % data
    html = re.sub(r'src="(images/[^"]+\.png)"', inline, html)
    return '<section class="chapter" id="%s">\n%s\n</section>' % (anchor(name), html)


def build_html():
    with open(os.path.join(HERE, "assets", "style.css")) as f:
        css = f.read()
    body = "\n".join(render(c) for c in chapters())
    return """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>A radar for your own sky</title>
<style>%s</style></head>
<body><main>
%s
</main></body></html>
""" % (css, body)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    ap.add_argument("--pdf", action="store_true", help="also print a PDF with Chromium")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    html_path = os.path.join(a.out, "lvgl-radar-docs.html")
    with open(html_path, "w") as f:
        f.write(build_html())
    print("wrote", html_path)
    if a.pdf:
        try:
            from playwright.sync_api import sync_playwright
        except ImportError:
            sys.exit("No Playwright: open the HTML in a browser and print it to PDF instead")
        pdf_path = os.path.join(a.out, "lvgl-radar-docs.pdf")
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page()
            page.goto("file://" + html_path)
            page.emulate_media(media="print")
            page.pdf(path=pdf_path, format="A4", print_background=True,
                     display_header_footer=True, header_template="<span></span>",
                     footer_template='<div style="font-size:8pt;width:100%;text-align:center;'
                                     'color:#555">A radar for your own sky · '
                                     '<span class="pageNumber"></span> / <span class="totalPages"></span></div>',
                     margin={"top": "16mm", "bottom": "18mm", "left": "16mm", "right": "16mm"})
            browser.close()
        print("wrote", pdf_path)


if __name__ == "__main__":
    main()
