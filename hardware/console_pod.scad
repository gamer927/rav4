// Console pod for the 1999 RAV4 transmission controller.
//
// A wedge-shaped pod for the centre console:
//   * sloped, driver-facing front panel: ON/OFF (12 mm toggle with flip-up
//     safety cover), ECO and SPORT (6 mm mini toggles), TEST (12 mm push
//     button), status LED (8 mm bezel), 0.96" OLED window, optional [+]/[-]
//   * flat top deck with the Beavis Motorsport DIY sequential shifter's
//     bolt pattern (4 x M6, 116 x 74 mm) and a slot for its switch wires
//   * rear cable glands (PG7) for the console cable and the OLED cable
//   * separate bottom plate with mounting slots
//
// Print in PETG or ASA, NOT PLA: a parked car's interior gets hotter than
// PLA's ~55 C softening point. Measure your parts and adjust the hole sizes
// below before printing.
//
// Print the shell upside down (top deck on the bed): the sloped front panel
// is then a 30-degree overhang and needs no supports.
//
// Render one part at a time:  part = "shell";  or  part = "bottom";
part = "shell";  // [shell, bottom, assembly, fit, fit_check]

/* [Overall size] */
W = 150;            // width (x)
D = 130;            // depth (y), front to back
H = 70;             // height (z)
R = 40;             // horizontal run of the sloped front panel
wall = 3;           // wall thickness
deck_t = 6;         // top deck thickness (holds M6 nut traps)

/* [Shifter deck] */
with_shifter_deck = true;
shifter_holes = [116, 74];   // Beavis shifter M6 pattern, centre to centre
m6_clear = 6.6;
m6_nut_af = 10.2;            // across flats (+ clearance)
m6_nut_h = 5.2;
shifter_wire_slot = [26, 14];

/* [Front panel controls] */
toggle_big_d = 12.2;     // ON/OFF: 12 mm toggle with safety cover
toggle_small_d = 6.3;    // ECO / SPORT: 6 mm mini toggle (MTS-101/102)
button_d = 12.2;         // TEST and optional [+]/[-]: 12 mm momentary
led_d = 8.0;             // 5 mm LED in an 8 mm panel bezel
with_shift_buttons = false;   // true = [+]/[-] buttons as well as / instead of the lever
labels = true;
label_depth = 0.6;

/* [OLED (0.96" SSD1306 I2C)] */
with_oled = true;
oled_window = [25, 14];        // visible window
oled_window_offset = [0, 2.5]; // window centre relative to the PCB centre
oled_holes = [23.5, 23.8];     // M2 mounting hole spacing
oled_hole_d = 2.4;

/* [Rear cable glands] */
gland_d = 12.5;   // PG7
gland_z = 22;

/* [Bottom plate] */
insert_d = 4.2;       // M3 heat-set insert hole in the corner bosses
boss_d = 9;
boss_h = 12;
plate_t = 3;
screw_clear = 3.4;
mount_slot = [12, 5.5];   // slots to bolt the pod to a bracket / console

$fn = 48;
ang = atan(H / R);               // slope of the front panel from horizontal
L = sqrt(H * H + R * R);         // slant length of the front panel

// ----------------------------------------------------------------- geometry
module wedge(inset_x, inset_back, front_shift, z0, z1) {
    // front plane: z = (y - y0) * tan(ang)
    y0 = front_shift;
    hull() {
        translate([inset_x, y0 + z0 / tan(ang), z0])
            cube([W - 2 * inset_x, D - inset_back - (y0 + z0 / tan(ang)), 0.01]);
        translate([inset_x, y0 + z1 / tan(ang), z1 - 0.01])
            cube([W - 2 * inset_x, D - inset_back - (y0 + z1 / tan(ang)), 0.01]);
    }
}

// Local frame on the front panel: x along the width, y up the slope from the
// bottom edge, z = outward normal (0 = outer surface).
module on_panel() { rotate([ang, 0, 0]) children(); }

// Bosses overlap the walls by 1 mm so they print fused to them.
bo = wall + boss_d / 2 - 1;
boss_pos = [[bo, R * 0.6], [W - bo, R * 0.6], [bo, D - bo], [W - bo, D - bo]];

// Front panel layout, in panel coordinates (u along the width, v up the
// slope from the bottom edge, mm). Each control:
//   [u, v, hole diameter, label, label offset (v, from the centre),
//    body width, body height, depth behind the panel incl. terminals + wire bend]
// The lowest controls sit high enough up the slope that their bodies clear
// the bottom plate (checked by part = "fit_check").
controls = concat(
    [[60, 50, toggle_big_d,   "ON/OFF", -31, 14, 20, 50],     // label below: the flip cover hides above
     [86, 63, toggle_small_d, "ECO",     6.5, 9, 14, 32],
     [108, 63, toggle_small_d, "SPORT",  6.5, 9, 14, 32],
     [86, 38, button_d,       "TEST",    9.5, 16, 16, 38]],
    with_shift_buttons ? [[108, 38, button_d, "-", 9.5, 16, 16, 38],
                          [130, 38, button_d, "+", 9.5, 16, 16, 38]] : []);
led_uv = [28, 30];
oled_uv = [28, 56];          // centre of the OLED module's PCB
oled_module = [27.5, 28, 16];  // PCB size + depth behind the panel (glass, PCB, soldered wires)
led_depth = 30;              // bezel + LED legs + wires

module controls_cut() {
    on_panel() {
        for (c = controls) translate([c[0], c[1], -10]) cylinder(h = 20, d = c[2]);
        translate([led_uv[0], led_uv[1], -10]) cylinder(h = 20, d = led_d);
        if (with_oled) {
            translate([oled_uv[0] + oled_window_offset[0] - oled_window[0] / 2,
                       oled_uv[1] + oled_window_offset[1] - oled_window[1] / 2, -10])
                cube([oled_window[0], oled_window[1], 20]);
            for (sx = [-1, 1], sy = [-1, 1])
                translate([oled_uv[0] + sx * oled_holes[0] / 2, oled_uv[1] + sy * oled_holes[1] / 2, -10])
                    cylinder(h = 20, d = oled_hole_d);
        }
        if (labels) {
            for (c = controls)
                translate([c[0], c[1] + c[4], -label_depth])
                    linear_extrude(label_depth + 1)
                        text(c[3], size = 4.5, halign = "center", font = "DejaVu Sans:style=Bold");
            translate([led_uv[0], led_uv[1] + led_d / 2 + 2.5, -label_depth])
                linear_extrude(label_depth + 1) text("STATUS", size = 3.5, halign = "center",
                                                     font = "DejaVu Sans:style=Bold");
        }
    }
}

module shifter_deck_cut() {
    cx = W / 2;
    cy = R + (D - R) / 2;
    for (sx = [-1, 1], sy = [-1, 1]) {
        translate([cx + sx * shifter_holes[0] / 2, cy + sy * shifter_holes[1] / 2, H - deck_t - 1])
            cylinder(h = deck_t + 2, d = m6_clear);
        // hex nut trap from underneath
        translate([cx + sx * shifter_holes[0] / 2, cy + sy * shifter_holes[1] / 2, H - deck_t - 0.01])
            cylinder(h = m6_nut_h, d = m6_nut_af / cos(30), $fn = 6);
    }
    translate([cx - shifter_wire_slot[0] / 2, cy - shifter_wire_slot[1] / 2, H - deck_t - 1])
        cube([shifter_wire_slot[0], shifter_wire_slot[1], deck_t + 2]);
}

module shell() {
    difference() {
        union() {
            difference() {
                wedge(0, 0, 0, 0, H);
                wedge(wall, wall, wall / sin(ang), -1, H - deck_t);
            }
            // corner bosses for the bottom plate, each with a 45-degree gusset
            // into the side wall so the shell prints upside down without supports
            for (p = boss_pos) {
                translate([p[0], p[1], 0]) cylinder(h = boss_h, d = boss_d);
                hull() {
                    translate([p[0], p[1], boss_h - 0.01]) cylinder(h = 0.01, d = boss_d);
                    translate([p[0] < W / 2 ? 0 : W - wall, p[1] - boss_d / 2, boss_h + boss_d])
                        cube([wall, boss_d, 0.01]);
                }
            }
        }
        for (p = boss_pos) translate([p[0], p[1], -1]) cylinder(h = boss_h - 2, d = insert_d);
        controls_cut();
        if (with_shifter_deck) shifter_deck_cut();
        // rear cable glands: console cable and OLED cable go to the TCU
        for (gx = [W * 0.3, W * 0.7])
            translate([gx, D + 1, gland_z]) rotate([90, 0, 0]) cylinder(h = wall + 2, d = gland_d);
    }
}

module bottom() {
    // Full footprint: the shell's walls stand on it.
    difference() {
        cube([W, D, plate_t]);
        for (p = boss_pos) {
            translate([p[0], p[1], -1]) cylinder(h = plate_t + 2, d = screw_clear);
            // countersink on the underside: the screws go in from below
            translate([p[0], p[1], -0.4]) cylinder(h = 2, d1 = 6.5, d2 = screw_clear);
        }
        // slots to bolt the plate to the console first; then screw the shell on
        for (mx = [W * 0.2, W * 0.8], my = [D * 0.55, D * 0.85])
            hull() for (dx = [-1, 1] * (mount_slot[0] - mount_slot[1]) / 2)
                translate([mx + dx, my, -1]) cylinder(h = plate_t + 2, d = mount_slot[1]);
    }
}

// ------------------------------------------------------------ fit check
// Everything inside the pod as solid blocks, measured inward from the panel's
// inner face: switch and button bodies with their terminals and a wire bend,
// the LED, the OLED module, the shifter's bolt tips under the deck, the
// mounting-slot bolt heads, the rear glands, and a cluster of lever-nut
// connectors where the console cable is joined.
module control_bodies() {
    on_panel() {
        for (c = controls) translate([c[0] - c[5] / 2, c[1] - c[6] / 2, -wall - 0.05 - c[7]]) cube([c[5], c[6], c[7]]);
        translate([led_uv[0] - 5, led_uv[1] - 5, -wall - 0.05 - led_depth]) cube([10, 10, led_depth]);
        if (with_oled)
            translate([oled_uv[0] - oled_module[0] / 2, oled_uv[1] - oled_module[1] / 2, -wall - 0.05 - oled_module[2]])
                cube(oled_module);
    }
}
module other_contents() {
    cx = W / 2;
    cy = R + (D - R) / 2;
    // M6 bolt tips: cut the bolts to (shifter base thickness + 6 mm) so they end
    // within 4 mm of the deck's underside.
    if (with_shifter_deck) for (sx = [-1, 1], sy = [-1, 1])
        translate([cx + sx * shifter_holes[0] / 2, cy + sy * shifter_holes[1] / 2, H - deck_t - 4])
            cylinder(h = 4, d = 7);
    for (mx = [W * 0.2, W * 0.8], my = [D * 0.55, D * 0.85])     // mounting bolt heads + washers
        translate([mx, my, 0]) cylinder(h = 6, d = 14);
    for (gx = [W * 0.3, W * 0.7])                                 // gland locknuts + cable
        translate([gx, D - wall - 0.01, gland_z]) rotate([90, 0, 0]) {
            cylinder(h = 9, d = 17.5);
            cylinder(h = 16, d = 7);
        }
    translate([W / 2 - 30, D - wall - 45, 0.01]) cube([60, 22, 20]);  // lever-nut connectors
}
module fit_check() {
    // Must be EMPTY.
    intersection() { shell(); union() { control_bodies(); other_contents(); } }
    intersection() { translate([-1, -1, -10]) cube([W + 2, D + 2, 10]); control_bodies(); }  // through the plate
    intersection() { control_bodies(); other_contents(); }
    for (i = [0 : len(controls) - 2]) for (j = [0 : len(controls) - 1])
        if (j > i) intersection() {
            on_panel() translate([controls[i][0] - controls[i][5] / 2, controls[i][1] - controls[i][6] / 2,
                                  -wall - controls[i][7]]) cube([controls[i][5], controls[i][6], controls[i][7]]);
            on_panel() translate([controls[j][0] - controls[j][5] / 2, controls[j][1] - controls[j][6] / 2,
                                  -wall - controls[j][7]]) cube([controls[j][5], controls[j][6], controls[j][7]]);
        }
}

// "shell" is exported upside down, ready to print; "assembly" shows it upright.
if (part == "shell") translate([0, D, H]) rotate([180, 0, 0]) shell();
else if (part == "bottom") bottom();
else if (part == "fit_check") fit_check();
else if (part == "fit") { color("gold", 0.35) shell(); color("seagreen") control_bodies(); color("dimgray") other_contents(); }
else if (part == "assembly") { shell(); color("grey") translate([0, 0, -plate_t - 5]) bottom(); }
