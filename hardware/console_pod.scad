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
part = "shell";  // [shell, bottom, assembly]

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

module controls_cut() {
    // [u, v, diameter, label]
    big = [[72, 50, toggle_big_d, "ON/OFF"]];
    small = [[100, 50, toggle_small_d, "ECO"], [126, 50, toggle_small_d, "SPORT"]];
    btns = concat([[72, 20, button_d, "TEST"]],
                  with_shift_buttons ? [[100, 20, button_d, "-"], [126, 20, button_d, "+"]] : []);
    all = concat(big, small, btns);
    on_panel() {
        for (c = all) translate([c[0], c[1], -10]) cylinder(h = 20, d = c[2]);
        // status LED under the OLED
        translate([35, 14, -10]) cylinder(h = 20, d = led_d);
        if (with_oled) {
            translate([35 + oled_window_offset[0] - oled_window[0] / 2, 47 + oled_window_offset[1] - oled_window[1] / 2, -10])
                cube([oled_window[0], oled_window[1], 20]);
            for (sx = [-1, 1], sy = [-1, 1])
                translate([35 + sx * oled_holes[0] / 2, 47 + sy * oled_holes[1] / 2, -10])
                    cylinder(h = 20, d = oled_hole_d);
        }
        if (labels) {
            for (c = all)
                translate([c[0], c[1] + max(c[2], 10) / 2 + 3, -label_depth])
                    linear_extrude(label_depth + 1)
                        text(c[3], size = 4.5, halign = "center", font = "DejaVu Sans:style=Bold");
            translate([35, 14 + led_d / 2 + 2.5, -label_depth])
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
    difference() {
        translate([wall + 0.3, wall / sin(ang) + 0.3, 0])
            cube([W - 2 * wall - 0.6, D - wall - wall / sin(ang) - 0.6, plate_t]);
        for (p = boss_pos) {
            translate([p[0], p[1], -1]) cylinder(h = plate_t + 2, d = screw_clear);
            // countersink on the underside: the screws go in from below
            translate([p[0], p[1], -0.4]) cylinder(h = 2, d1 = 6.5, d2 = screw_clear);
        }
        // slots to bolt the plate to the console first; then screw the shell on
        for (mx = [W * 0.3, W * 0.7], my = [D * 0.35, D * 0.75])
            hull() for (dx = [-1, 1] * (mount_slot[0] - mount_slot[1]) / 2)
                translate([mx + dx, my, -1]) cylinder(h = plate_t + 2, d = mount_slot[1]);
    }
}

// "shell" is exported upside down, ready to print; "assembly" shows it upright.
if (part == "shell") translate([0, D, H]) rotate([180, 0, 0]) shell();
else if (part == "bottom") bottom();
else { shell(); color("grey") translate([0, 0, -plate_t - 5]) bottom(); }
