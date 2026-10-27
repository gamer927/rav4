#!/bin/sh
# Check that everything fits, then export the printable parts in
# hardware/*.scad to hardware/stl/ (binary STL) and render the previews.
# Needs OpenSCAD 2021.01+; previews also need xvfb-run and python3 with Pillow.
# Run tools/boards.py first if you changed a board layout.
set -e
cd "$(dirname "$0")/.."
mkdir -p hardware/stl

# Fit check: each model's "fit_check" part intersects everything that goes
# inside with the housing and with each other. It must come out empty.
fit_check() {  # file [extra-openscad-args...]
    f=$1; shift
    out=$(openscad -o /tmp/fit_check_$$.stl -D 'part="fit_check"' "$@" "hardware/$f.scad" 2>&1 || true)
    rm -f /tmp/fit_check_$$.stl
    if echo "$out" | grep -q "top level object is empty"; then
        echo "fit check OK: $f $*"
    else
        echo "FIT CHECK FAILED: $f $* (something collides; render part=\"fit_check\" to see it)" >&2
        exit 1
    fi
}
fit_check tcu_enclosure
fit_check console_pod
fit_check console_pod -D with_shift_buttons=true

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
    xvfb-run -a openscad -o hardware/fit-tcu.png -D 'part="fit"' --imgsize=1100,850 --colorscheme=Tomorrow \
        --camera=-60,-200,300,110,85,15 --viewall --autocenter hardware/tcu_enclosure.scad
    xvfb-run -a openscad -o hardware/fit-pod.png -D 'part="fit"' --imgsize=1100,850 --colorscheme=Tomorrow \
        --camera=-70,-160,170,75,60,30 --viewall --autocenter hardware/console_pod.scad
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
