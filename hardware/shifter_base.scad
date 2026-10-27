// Shifter base for the 1999 RAV4 transmission controller.
//
// A low box that the Beavis Motorsport DIY sequential shifter bolts onto,
// mounted on the centre console next to the factory gear lever:
//   * top deck with the shifter's bolt pattern (4 x M6, 116 x 74 mm, nuts
//     trapped underneath) and a hole for the shifter's two switch wires
//   * inside: three lever-nut connectors join the switch wires to the cable
//   * one PG7 gland at the back for the 3-core cable to the TCU box (X7)
//   * a bottom plate that bolts to the console (or a steel bracket) first;
//     the box then screws onto it from below
//
// Print in PETG or ASA, NOT PLA. Print the box upside down (deck on the bed).
part = "box";  // [box, bottom, assembly, fit, fit_check]

/* [Overall size] */
W = 150;            // width (x)
D = 100;            // depth (y)
H = 26;             // height (z), bottom plate not included
wall = 3;
deck_t = 6;         // holds the M6 nut traps

/* [Shifter] */
shifter_holes = [116, 74];   // Beavis shifter M6 pattern, centre to centre
m6_clear = 6.6;
m6_nut_af = 10.2;            // across flats (+ clearance)
m6_nut_h = 5.2;
wire_hole = [26, 14];        // the shifter's switch wires come down through here
shifter_offset = [0, 0];     // move the shifter on the deck (e.g. towards the driver)

/* [Cable] */
gland_d = 12.5;              // PG7
gland_x = 0.75;              // fraction of W
gland_z = 11;

/* [Bottom plate] */
insert_d = 4.2;              // M3 heat-set inserts in the corner bosses
boss_d = 9;
plate_t = 3;
screw_clear = 3.4;
mount_slot = [12, 5.5];      // slots for M5 bolts / screws into the console
mount_pos = [[0.12, 0.3], [0.88, 0.3], [0.12, 0.7], [0.88, 0.7]];   // fractions of W, D

$fn = 48;
cx = W / 2 + shifter_offset[0];
cy = D / 2 + shifter_offset[1];
bo = wall + boss_d / 2 - 1;
boss_pos = [[bo, bo], [W - bo, bo], [bo, D - bo], [W - bo, D - bo]];

module box() {
    difference() {
        union() {
            difference() {
                cube([W, D, H]);
                translate([wall, wall, -1]) cube([W - 2 * wall, D - 2 * wall, H - deck_t + 1]);
            }
            for (p = boss_pos) translate([p[0], p[1], 0]) cylinder(h = H, d = boss_d);
        }
        for (p = boss_pos) translate([p[0], p[1], -1]) cylinder(h = 11, d = insert_d);
        for (sx = [-1, 1], sy = [-1, 1]) {
            translate([cx + sx * shifter_holes[0] / 2, cy + sy * shifter_holes[1] / 2, H - deck_t - 1])
                cylinder(h = deck_t + 2, d = m6_clear);
            // hex nut trap from underneath
            translate([cx + sx * shifter_holes[0] / 2, cy + sy * shifter_holes[1] / 2, H - deck_t - 0.01])
                cylinder(h = m6_nut_h, d = m6_nut_af / cos(30), $fn = 6);
        }
        translate([cx - wire_hole[0] / 2, cy - wire_hole[1] / 2, H - deck_t - 1])
            cube([wire_hole[0], wire_hole[1], deck_t + 2]);
        translate([W * gland_x, D + 1, gland_z]) rotate([90, 0, 0]) cylinder(h = wall + 2, d = gland_d);
        translate([W * gland_x, D - 0.6, gland_z + gland_d / 2 + 2.5]) rotate([90, 0, 180])
            linear_extrude(1) text("TO TCU", size = 3.5, halign = "center", font = "DejaVu Sans:style=Bold");
    }
}

module bottom() {
    difference() {
        cube([W, D, plate_t]);
        for (p = boss_pos) {
            translate([p[0], p[1], -1]) cylinder(h = plate_t + 2, d = screw_clear);
            translate([p[0], p[1], -0.4]) cylinder(h = 2, d1 = 6.5, d2 = screw_clear);
        }
        for (m = mount_pos)
            hull() for (dx = [-1, 1] * (mount_slot[0] - mount_slot[1]) / 2)
                translate([W * m[0] + dx, D * m[1], -1]) cylinder(h = plate_t + 2, d = mount_slot[1]);
    }
}

// ------------------------------------------------------------ fit check
// Inside: the M6 bolt tips (cut the bolts to shifter base thickness + 6 mm
// so they end within 4 mm of the deck's underside), the mounting bolt heads
// with washers, the gland locknut and cable, and three lever-nut connectors.
module contents() {
    for (sx = [-1, 1], sy = [-1, 1])
        translate([cx + sx * shifter_holes[0] / 2, cy + sy * shifter_holes[1] / 2, H - deck_t - 4])
            cylinder(h = 4, d = 7);
    for (m = mount_pos) translate([W * m[0], D * m[1], 0.01]) cylinder(h = 6, d = 14);
    translate([W * gland_x, D - wall - 0.01, gland_z]) rotate([90, 0, 0]) {
        cylinder(h = 9, d = 17.5);
        cylinder(h = 25, d = 7);
    }
    translate([cx - 25, cy - 8, 0.01]) cube([50, 16, 9]);   // 3 lever-nut connectors (WAGO 221-413)
}
module fit_check() {
    // Must be EMPTY.
    intersection() { box(); contents(); }
}

if (part == "box") translate([0, D, H]) rotate([180, 0, 0]) box();
else if (part == "bottom") bottom();
else if (part == "fit_check") fit_check();
else if (part == "fit") { color("gold", 0.35) box(); color("dimgray") contents(); }
else if (part == "assembly") {
    color("gold") box();
    color("grey") translate([0, 0, -plate_t - 5]) bottom();
    // the shifter itself (rough outline)
    color("dimgray") translate([cx, cy, H]) {
        translate([-67, -45, 0]) cube([134, 90, 10]);
        translate([0, 0, 10]) cylinder(h = 140, d = 12);
        translate([0, 0, 155]) sphere(d = 40);
    }
}
