#!/usr/bin/env python3
"""Extract the IC icon table from the HTML prototype and rasterize each icon to a white 64x64 PNG.

Usage: make_icons.py <index.html> <out_dir>   (needs rsvg-convert from librsvg2-bin)
"""
import os
import re
import subprocess
import sys

src, out = sys.argv[1], sys.argv[2]
html = open(src, encoding="utf-8").read()
block = html[html.index("const IC={"):]
block = block[: block.index("\n};")]
icons = dict(re.findall(r"^\s*([a-z0-9]+):'(.*?)',?$", block, re.M))
os.makedirs(out, exist_ok=True)
for name, body in icons.items():
    svg = (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="64" height="64" '
        'fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" color="#ffffff">'
        + body.replace("currentColor", "#ffffff")
        + "</svg>"
    )
    subprocess.run(["rsvg-convert", "-w", "64", "-h", "64", "-o", os.path.join(out, name + ".png")], input=svg.encode(), check=True)
print(f"{len(icons)} icons -> {out}")
