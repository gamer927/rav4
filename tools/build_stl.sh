#!/bin/sh
# Export the printable parts in hardware/*.scad to hardware/stl/ (binary STL)
# and render hardware/preview.png. Needs OpenSCAD 2021.01+; previews also need
# xvfb-run and python3 with Pillow.
set -e
cd "$(dirname "$0")/.."
mkdir -p hardware/stl
export_part() {  # file part [suffix extra-openscad-args...]
    f=$1; p=$2; suffix=$3; shift 2; [ $# -gt 0 ] && shift
    openscad --export-format binstl -o "hardware/stl/${f}_${p}${suffix}.stl" -D "part=\"$p\"" "$@" "hardware/$f.scad"
}
export_part console_pod shell
export_part console_pod bottom
export_part tcu_enclosure box
export_part tcu_enclosure box _no_ears -D ears=false   # for 220 mm beds
export_part tcu_enclosure lid

if command -v xvfb-run >/dev/null 2>&1; then
    tmp=$(mktemp -d)
    xvfb-run -a openscad -o "$tmp/pod.png" -D 'part="assembly"' --render --imgsize=900,750 \
        --camera=-60,-170,200,75,55,20 --viewall --autocenter hardware/console_pod.scad
    xvfb-run -a openscad -o "$tmp/tcu.png" -D 'part="assembly"' --render --imgsize=900,750 \
        --camera=-80,-180,260,110,75,20 --viewall --autocenter hardware/tcu_enclosure.scad
    python3 - "$tmp" <<'PY'
import sys
from PIL import Image, ImageDraw, ImageFont
d = sys.argv[1]
a, b = Image.open(d + "/pod.png"), Image.open(d + "/tcu.png")
out = Image.new("RGB", (a.width + b.width, a.height + 50), "white")
out.paste(a, (0, 50)); out.paste(b, (a.width, 50))
draw = ImageDraw.Draw(out)
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 26)
except OSError:
    font = ImageFont.load_default()
draw.text((20, 12), "Console pod (shifter deck on top, bottom plate below)", fill="black", font=font)
draw.text((a.width + 20, 12), "TCU enclosure (lid lifted)", fill="black", font=font)
out.save("hardware/preview.png", optimize=True)
PY
    rm -rf "$tmp"
fi
