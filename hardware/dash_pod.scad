// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
// Dash pod for the 1999 RAV4 transmission controller.
//
// A slim box that sits on top of the dashboard, driver's side:
//   * flat, driver-facing front: the 2.42" OLED screen on the left, then one
//     row of controls, left to right: ECO, SPORT, MANUAL (6 mm mini toggles),
//     TEST (12 mm push button), ON/OFF (12 mm toggle with flip-up safety
//     cover). No status light: the screen says everything.
//   * a back cover with two cable glands (PG7: the switch cable and the
//     screen cable, both to the TCU box) and a hinge
//   * a separate mount: a small base with a hinge arm, screwed or taped to
//     the dash. The pod hangs on the hinge and tilts to face you.
//
// Print in PETG or ASA, NOT PLA: a parked car's dash gets far hotter than
// PLA's ~55 C softening point. Measure your parts and adjust the hole sizes
// below before printing.
//
// Render one part at a time: part = "shell", "back", "mount" (each exported
// in its print orientation, no supports needed). part = "panel_test" is a
// flat 3 mm copy of the front (about 1 hour to print): check that every
// switch and the screen fit BEFORE printing the shell.
part = "shell";  // [shell, back, mount, panel_test, assembly, fit, fit_check, build]

/* [Overall size] */
W = 200;            // width (x)
H = 64;             // height (z)
D = 64;             // depth (y), front face to back of the back cover
wall = 3;           // wall and front thickness
back_t = 4;         // back cover thickness

/* [Front panel controls] */
toggle_big_d = 12.2;     // ON/OFF: 12 mm toggle with safety cover
toggle_small_d = 6.3;    // ECO / SPORT / MANUAL: 6 mm mini toggle (MTS-102)
button_d = 12.2;         // TEST: 12 mm momentary
labels = true;
label_depth = 0.6;
label_size = 3.2;        // MANUAL is ~20 mm wide at this size: fits the 23 mm pitch
row_v = 36;              // height of the control row's centre line
label_v = 14;            // height of the labels (baseline)

/* [OLED (2.42" SSD1309, I2C)] */
// Module 61.5 x 39.5 mm, display area 55.0 x 27.5 mm. It drops into a pocket
// of ribs behind the front and is held by double-sided foam tape on the
// glass border. Check the window position with part = "panel_test".
oled_module = [61.5, 39.5];      // PCB size
oled_window = [58, 30.5];        // display area + 1.5 mm each side
oled_window_offset = [0, 0];     // window centre relative to the PCB centre (VERIFY)
oled_depth = 14;                 // glass + PCB + connector + wires, behind the front
oled_rib = [1.6, 3];             // pocket rib width and height
oled_uv = [37, 32];              // centre of the module's PCB

/* [Back cover and hinge] */
insert_d = 4.2;       // M3 heat-set insert hole in the corner columns
col_d = 9;            // corner column diameter
screw_clear = 3.4;
gland_d = 12.5;       // PG7
glands = [[22, "SWITCHES"], [52, "SCREEN"]];   // x of each gland, label
gland_z = 22;
hinge_off = 14;       // hinge axis, behind the back cover
lug_t = 6;            // the pod's two hinge lugs
knuckle_t = 10;       // the mount's knuckle between them
hinge_d = 18;         // outside diameter of lugs and knuckle
m5_clear = 5.4;
m5_nut_af = 8.4;      // across flats (+ clearance): nyloc in a hex trap
m5_nut_h = 4;         // depth of the hex trap

/* [Mount] */
base = [70, 44, 5];   // base plate
pivot_h = 50;         // hinge axis above the base's underside
base_screw = 4.5;     // two countersunk screws (or use 3M VHB tape instead)
// fit_check sweeps the pod from tilted 35 degrees back (screen facing up
// towards you) to 5 degrees forward.
tilt_toward = 35;
tilt_away = 5;

$fn = 48;

// ----------------------------------------------------------------- layout
// Front panel layout, in panel coordinates (u along the width from the left
// edge, v up from the bottom edge, mm, as you look at the front). Each control:
//   [u, v, hole diameter, label, body width, body height,
//    depth behind the front incl. terminals + wire bend]
// To change the order, change the u values (and the labels follow).
controls = [
    [86,  row_v, toggle_small_d, "ECO",    9, 14, 32],
    [110, row_v, toggle_small_d, "SPORT",  9, 14, 32],
    [134, row_v, toggle_small_d, "MANUAL", 9, 14, 32],
    [159, row_v, button_d,       "TEST",  16, 16, 38],
    [185, row_v, toggle_big_d,   "ON/OFF", 18, 22, 50]];   // 2-pole: bigger body; safety cover flips up
ON_OFF_COVER = [22, 36, 18]; // flip cover outside the front: width, height (from 10 mm below the hole), depth (VERIFY)

// Panel frame: u -> x, v -> z, outward normal -> -y (0 = outer surface).
module on_face() { rotate([90, 0, 0]) children(); }

// Corner columns run the full depth: the back cover screws into heat-set
// inserts at their ends. They overlap the walls by 1 mm so they print fused.
co = wall + col_d / 2 - 1;
col_pos = [[co, co], [W - co, co], [co, H - co], [W - co, H - co]];
inner_d = D - back_t;        // depth of the shell (front face to its open back)
pivot = [W / 2, D + hinge_off, H / 2];

// ----------------------------------------------------------------- shell
module face_cut() {
    on_face() {
        for (c = controls) translate([c[0], c[1], -10]) cylinder(h = 20, d = c[2]);
        translate([oled_uv[0] + oled_window_offset[0] - oled_window[0] / 2,
                   oled_uv[1] + oled_window_offset[1] - oled_window[1] / 2, -10])
            cube([oled_window[0], oled_window[1], 20]);
        if (labels) {
            for (c = controls)
                translate([c[0], label_v, -label_depth])
                    linear_extrude(label_depth + 1)
                        text(c[3], size = label_size, halign = "center", font = "DejaVu Sans:style=Bold");
        }
    }
}

// Four L-shaped corner ribs behind the front: the OLED module drops in
// between them (0.5 mm clearance), so it lines up with the window by itself.
module oled_ribs() {
    cw = oled_module[0] / 2 + 0.5;
    ch = oled_module[1] / 2 + 0.5;
    t = oled_rib[0];
    for (sx = [-1, 1], sy = [-1, 1])
        translate([oled_uv[0] + sx * cw, oled_uv[1] + sy * ch, -wall - oled_rib[1]]) {
            translate([sx > 0 ? 0 : -t, sy > 0 ? -10 : -t, 0]) cube([t, 10 + t, oled_rib[1] + 0.5]);
            translate([sx > 0 ? -10 : -t, sy > 0 ? 0 : -t, 0]) cube([10 + t, t, oled_rib[1] + 0.5]);
        }
}

module shell() {
    difference() {
        union() {
            difference() {
                cube([W, inner_d, H]);
                translate([wall, wall, wall]) cube([W - 2 * wall, inner_d, H - 2 * wall]);
            }
            on_face() oled_ribs();
            for (p = col_pos) translate([p[0], 0, p[1]]) rotate([-90, 0, 0]) cylinder(h = inner_d, d = col_d);
        }
        for (p = col_pos) translate([p[0], inner_d + 1, p[1]]) rotate([90, 0, 0]) cylinder(h = 13, d = insert_d);
        face_cut();
    }
}

// ----------------------------------------------------------------- back cover
module lug_shape(x0) {
    // A plate from the cover out to the hinge axis, rounded at the axis.
    hull() {
        translate([x0, D - 0.01, H / 2 - hinge_d / 2]) cube([lug_t, 0.01, hinge_d]);
        translate([x0, pivot[1], pivot[2]]) rotate([0, 90, 0]) cylinder(h = lug_t, d = hinge_d);
    }
}
lug_x = [W / 2 - knuckle_t / 2 - 0.3 - lug_t, W / 2 + knuckle_t / 2 + 0.3];

module back() {
    difference() {
        union() {
            translate([0, inner_d, 0]) cube([W, back_t, H]);
            for (x = lug_x) lug_shape(x);
        }
        for (p = col_pos) {
            translate([p[0], inner_d - 1, p[1]]) rotate([-90, 0, 0]) cylinder(h = back_t + 2, d = screw_clear);
            // countersink on the outside
            translate([p[0], D + 0.01, p[1]]) rotate([90, 0, 0]) cylinder(h = 2, d1 = 6.5, d2 = screw_clear);
        }
        for (g = glands) translate([g[0], inner_d - 1, gland_z]) rotate([-90, 0, 0]) cylinder(h = back_t + 2, d = gland_d);
        // M5 hinge bolt; hex trap for the nyloc on the right-hand lug
        translate([lug_x[0] - 1, pivot[1], pivot[2]]) rotate([0, 90, 0])
            cylinder(h = knuckle_t + 2 * lug_t + 3, d = m5_clear);
        translate([lug_x[1] + lug_t - m5_nut_h, pivot[1], pivot[2]]) rotate([0, 90, 0])
            cylinder(h = m5_nut_h + 1, d = m5_nut_af / cos(30), $fn = 6);
        if (labels) for (g = glands)
            translate([g[0], D - label_depth, gland_z + gland_d / 2 + 3]) rotate([90, 0, 180])
                linear_extrude(label_depth + 1)
                    text(g[1], size = 3.5, halign = "center", font = "DejaVu Sans:style=Bold");
    }
}

// ----------------------------------------------------------------- mount
// Base on the dash, arm up to the hinge.
module mount() {
    bz = pivot[2] - pivot_h;   // underside of the base
    difference() {
        union() {
            translate([pivot[0] - base[0] / 2, pivot[1] - base[1] / 2, bz]) cube(base);
            // arm: knuckle-wide at the top (it sits between the lugs), wider at the base
            hull() {
                translate([pivot[0] - knuckle_t / 2, pivot[1], pivot[2]]) rotate([0, 90, 0])
                    cylinder(h = knuckle_t, d = hinge_d);
                translate([pivot[0] - knuckle_t / 2, pivot[1] - 6, bz + base[2] + 6]) cube([knuckle_t, 12, 1]);
            }
            hull() {   // fillet into the base
                translate([pivot[0] - knuckle_t / 2, pivot[1] - 6, bz + base[2] + 6]) cube([knuckle_t, 12, 1]);
                translate([pivot[0] - 15, pivot[1] - 12, bz + base[2] - 0.01]) cube([30, 24, 0.01]);
            }
        }
        translate([pivot[0] - knuckle_t, pivot[1], pivot[2]]) rotate([0, 90, 0]) cylinder(h = 2 * knuckle_t, d = m5_clear);
        for (sx = [-1, 1]) {
            translate([pivot[0] + sx * 25, pivot[1], bz - 1]) cylinder(h = base[2] + 2, d = base_screw);
            translate([pivot[0] + sx * 25, pivot[1], bz + base[2] - 2.5]) cylinder(h = 2.6, d1 = base_screw, d2 = 9);
        }
    }
}

// ------------------------------------------------------------ fit check
// Everything inside the pod as solid blocks: switch and button bodies
// with their terminals and a wire bend, the OLED module, the gland locknuts
// with their cables, and a row of lever-nut connectors on the floor where
// the switch cable's cores are joined.
module control_bodies() {
    on_face() {
        for (c = controls) translate([c[0] - c[4] / 2, c[1] - c[5] / 2, -wall - 0.05 - c[6]]) cube([c[4], c[5], c[6]]);
        translate([oled_uv[0] - oled_module[0] / 2, oled_uv[1] - oled_module[1] / 2, -wall - 0.05 - oled_depth])
            cube([oled_module[0], oled_module[1], oled_depth]);
    }
}
module other_contents() {
    for (g = glands)                                               // gland locknuts + cable
        translate([g[0], inner_d - 0.01, gland_z]) rotate([90, 0, 0]) {
            cylinder(h = 9, d = 17.5);
            cylinder(h = 20, d = 7);
        }
    translate([90, 10, wall + 0.01]) cube([70, 38, 17]);           // 8 lever-nut connectors, 2 rows
}
module outside_parts() {   // the ON/OFF cover and gland heads outside the box
    c = controls[len(controls) - 1];
    on_face() translate([c[0] - ON_OFF_COVER[0] / 2, c[1] - 10, 0.05]) cube(ON_OFF_COVER);
}
module pod() { shell(); back(); }
module fit_check() {
    // Must be EMPTY.
    intersection() { shell(); union() { control_bodies(); other_contents(); } }
    intersection() { back(); union() { control_bodies(); other_contents(); } }
    intersection() { control_bodies(); other_contents(); }
    for (i = [0 : len(controls) - 2]) for (j = [i + 1 : len(controls) - 1])
        intersection() {
            on_face() translate([controls[i][0] - controls[i][4] / 2, controls[i][1] - controls[i][5] / 2,
                                 -wall - controls[i][6]]) cube([controls[i][4], controls[i][5], controls[i][6]]);
            on_face() translate([controls[j][0] - controls[j][4] / 2, controls[j][1] - controls[j][5] / 2,
                                 -wall - controls[j][6]]) cube([controls[j][4], controls[j][5], controls[j][6]]);
        }
    // the ON/OFF cover stays on the front
    intersection() { outside_parts(); shell(); }
    intersection() { outside_parts(); translate([-1, -50, H]) cube([W + 2, 60, 50]); }
    // the pod tilts through the whole range without hitting the mount
    for (a = [-tilt_toward : 5 : tilt_away])
        intersection() {
            mount();
            // the shell's outside is a plain box: use that (much faster than the shell)
            translate(pivot) rotate([a, 0, 0]) translate(-pivot) { cube([W, inner_d, H]); back(); }
        }
}

// The front, flat: every hole, the screen window and its pocket, on a 3 mm
// plate (print it outer face down).
module panel_test() {
    difference() {
        union() {
            translate([0, 0, -wall]) cube([W, H, wall]);
            oled_ribs();
        }
        rotate([-90, 0, 0]) face_cut();
    }
}

// ------------------------------------------------------------ build-book views
// part = "build", bstep = 1..5: the assembly so far, with this step's pieces
// highlighted (orange) and earlier ones grey.
bstep = 5;
module hi(n) { if (bstep == n) color("orangered") children(); else if (bstep > n) color("silver") children(); }
module build_view() {
    color("gold", bstep >= 2 && bstep <= 3 ? 0.45 : 1) shell();
    // 1: heat-set inserts, drawn half out of their holes (being pressed in)
    hi(1) for (p = col_pos) translate([p[0], inner_d + (bstep == 1 ? 6 : 0) - 6, p[1]]) rotate([-90, 0, 0])
        cylinder(h = 6, d = insert_d + 1.5);
    // 2: screen, switches, button (bodies inside, levers and caps outside)
    hi(2) {
        control_bodies();
        on_face() for (c = controls) translate([c[0], c[1], 0])
            cylinder(h = c[2] > 10 ? 9 : 12, d = c[2] > 10 ? c[2] : 3);
        outside_parts();
    }
    // 3: glands, cables, lever-nut connectors
    hi(3) other_contents();
    // 4: back cover
    hi(4) translate([0, bstep == 4 ? 25 : 0, 0]) back();
    // 5: mount and hinge bolt
    hi(5) {
        mount();
        translate([lug_x[0] - 3, pivot[1], pivot[2]]) rotate([0, 90, 0]) cylinder(h = 3, d = 9);
    }
}

// Print orientations: shell front-down, back cover outside-up, mount base-down.
if (part == "shell") translate([0, H, 0]) rotate([90, 0, 0]) shell();
else if (part == "back") translate([0, H, -inner_d]) rotate([90, 0, 0]) back();
else if (part == "mount") translate([0, 0, pivot_h - pivot[2]]) mount();
else if (part == "panel_test") translate([0, H, 0]) rotate([180, 0, 0]) panel_test();   // outer face on the bed
else if (part == "build") build_view();
else if (part == "fit_check") fit_check();
else if (part == "fit") { color("gold", 0.35) pod(); color("seagreen") control_bodies(); color("dimgray") other_contents(); color("grey") mount(); }
else if (part == "assembly") {
    color("gold") shell(); color("goldenrod") back(); color("grey") mount();
    color("firebrick") outside_parts();
    // screen glass, toggle levers and the TEST cap, so the picture isn't see-through
    color("black") on_face() translate([oled_uv[0] - oled_module[0] / 2, oled_uv[1] - oled_module[1] / 2, -wall - 2])
        cube([oled_module[0], oled_module[1], 1.9]);
    color("silver") on_face() for (c = controls) translate([c[0], c[1], 0])
        cylinder(h = c[2] > 10 ? 9 : 12, d = c[2] > 10 ? c[2] : 3);
}
