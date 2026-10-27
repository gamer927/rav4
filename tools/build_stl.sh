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
fit_check dash_pod
fit_check shifter_base

export_part() {  # file part [suffix extra-openscad-args...]
    f=$1; p=$2; suffix=$3; shift 2; [ $# -gt 0 ] && shift
    openscad --export-format binstl -o "hardware/stl/${f}_${p}${suffix}.stl" -D "part=\"$p\"" "$@" "hardware/$f.scad"
}
export_part dash_pod shell
export_part dash_pod back
export_part dash_pod mount
export_part dash_pod panel_test
export_part shifter_base box
export_part shifter_base bottom
export_part tcu_enclosure box
export_part tcu_enclosure box _no_ears -D ears=false   # for 220 mm beds
export_part tcu_enclosure lid

if command -v xvfb-run >/dev/null 2>&1; then
    tmp=$(mktemp -d)
    # Preview mode (no --render) keeps the colours: the real shifter shows green and black.
    xvfb-run -a openscad -o "$tmp/pod.png" -D 'part="assembly"' --colorscheme=Tomorrow --imgsize=900,750 \
        --camera=-90,-230,120,100,60,20 --viewall --autocenter hardware/dash_pod.scad
    xvfb-run -a openscad -o "$tmp/shift.png" -D 'part="assembly"' --colorscheme=Tomorrow --imgsize=900,750 \
        --camera=-260,-300,260,75,50,140 --viewall --autocenter hardware/shifter_base.scad
    xvfb-run -a openscad -o "$tmp/tcu.png" -D 'part="assembly"' --colorscheme=Tomorrow --imgsize=900,750 \
        --camera=-80,-180,260,110,75,20 --viewall --autocenter hardware/tcu_enclosure.scad
    xvfb-run -a openscad -o hardware/fit-tcu.png -D 'part="fit"' --imgsize=1100,850 --colorscheme=Tomorrow \
        --camera=-60,-200,300,110,85,15 --viewall --autocenter hardware/tcu_enclosure.scad
    xvfb-run -a openscad -o hardware/fit-pod.png -D 'part="fit"' --imgsize=1100,850 --colorscheme=Tomorrow \
        --camera=150,260,140,105,40,30 --viewall --autocenter hardware/dash_pod.scad
    python3 - "$tmp" <<'PY'
import sys
from PIL import Image, ImageDraw, ImageFont
d = sys.argv[1]
imgs = [Image.open(d + f) for f in ("/pod.png", "/shift.png", "/tcu.png")]
titles = ["1. Dash pod (dash top)", "2. Shifter base + Beavis shifter (console)",
          "3. TCU box (under the passenger dash)"]
w = imgs[0].width
out = Image.new("RGB", (w * 3, imgs[0].height + 50), "white")
draw = ImageDraw.Draw(out)
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 26)
except OSError:
    font = ImageFont.load_default()
for i, (im, t) in enumerate(zip(imgs, titles)):
    out.paste(im, (w * i, 50))
    draw.text((w * i + 20, 12), t, fill="black", font=font)
out = out.convert("RGB").quantize(colors=128)
out.save("hardware/preview.png", optimize=True)
PY
    rm -rf "$tmp"

    # Build-book pictures: the assembly so far, this step's pieces in orange.
    mkdir -p docs/build
    for n in 1 2 3 4; do
        xvfb-run -a openscad -o "docs/build/box-$n.png" -D 'part="build"' -D "bstep=$n" --imgsize=900,700 \
            --colorscheme=Tomorrow --camera=-70,-220,280,110,85,15 --viewall --autocenter hardware/tcu_enclosure.scad
    done
    rm -f docs/build/pod-*.png
    for n in 1 2 3 4 5; do
        case $n in 1|3|4) cam=170,300,130,105,40,30 ;; *) cam=-90,-230,120,105,60,20 ;; esac
        xvfb-run -a openscad -o "docs/build/pod-$n.png" -D 'part="build"' -D "bstep=$n" --imgsize=900,700 \
            --colorscheme=Tomorrow --camera=$cam --viewall --autocenter hardware/dash_pod.scad
    done
    xvfb-run -a openscad -o docs/build/shifter.png -D 'part="assembly"' --imgsize=900,900 \
        --colorscheme=Tomorrow --camera=-260,-300,260,75,50,140 --viewall --autocenter hardware/shifter_base.scad
    python3 - <<'PY'
import glob
from PIL import Image
for f in glob.glob("docs/build/box-*.png") + glob.glob("docs/build/pod-*.png") + ["docs/build/shifter.png"]:
    Image.open(f).convert("RGB").quantize(colors=64).save(f, optimize=True)
PY
fi
