// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
// TCU enclosure for the 1999 RAV4 transmission controller.
//
// Holds, on the floor of the box:
//   * the Arduino Mega 2560 on six short standoffs (its official hole
//     pattern), USB-B reachable through the left wall for flashing and logging
//   * the TCU circuit board (hardware/pcb/, made by tools/pcb.py), plugged onto
//     the Mega like a big shield and screwed to five tall posts. Its screw
//     terminals run along its front edge, each group behind its gland
//   * eight cable glands in the front wall (round holes, not switches), each
//     with its name engraved above it. The box itself has no switches: they
//     are all on the dash pod (hardware/dash_pod.scad)
//   * mounting ears for M6 bolts, and a screw-on lid (M3 heat-set inserts)
//
// part = "fit_check" renders the board with every part at its height, the
// solder joints under it, the Mega and its sockets, the glands and every
// cable's wires fanned out to their terminals, intersected with the box and
// with each other, and checks there's room to work between the glands and the
// terminals: an EMPTY result means everything fits.
// part = "fit" shows the populated box.
//
// The three 15 ohm dummy resistors make 14-28 W of heat. They do NOT go in
// this box: bolt them to a finned aluminium heatsink outside it (see
// hardware/README.md). The "LOAD" gland carries their four wires.
//
// Print in PETG or ASA, NOT PLA: behind the glovebox of a car parked in the
// sun gets hotter than PLA's ~55 C softening point. Measure your glands and
// adjust the sizes below before printing.
//
// Render one part at a time:  part = "box";  or  part = "lid";
part = "box";  // [box, lid, assembly, fit, fit_check, build]

include <pcb_envelope.scad>

/* [Box] */
IW = 210;          // inside width  (x)
ID = 186;          // inside depth  (y)
IH = 45;           // inside height (z)
wall = 3;
floor_t = 3;
lid_t = 3;
corner_r = 4;

/* [Circuit board and Arduino Mega] */
// The board's front-left corner, inside the box. 26 mm back from the front
// wall: the glands poke ~9 mm inside, which leaves ~17 mm to fan each cable's
// wires out and screw them into the terminals.
pcb_pos = [5, 26];
mega_pos = pcb_pos + pcb_mega_at;     // the Mega's USB-side corner
mega_holes = [[13.97, 2.54], [15.24, 50.8], [66.04, 7.62],
              [66.04, 35.56], [90.17, 50.8], [96.52, 2.54]];
mega_standoff_h = 6;
usb_center_y = 38.1;               // USB-B centre along the board edge
usb_cut = [18, 15];                // wall cut-out: the USB-B plug's moulding must pass the wall

/* [Standoffs] */
standoff_d = 7;
standoff_hole = 2.6;               // self-tapping M3 / pilot for M2.5 screw
insert_d = 4.2;                    // M3 heat-set insert
insert_depth = 8;
boss_d = 16;                       // corner bosses, centred on the inside corners
boss_off = 2.5;                    // insert offset from the inside corner (x and y)
lid_screw_clear = 3.4;

/* [Cable glands (front wall)] */
// [label, hole diameter]: PG9 = 15.2 mm, PG7 = 12.5 mm. Each gland sits in
// front of its terminal group on the board, moved in from the corners to
// clear the lid-screw bosses.
gland_size = [["SHIFT", 12.5], ["DASH", 12.5], ["SCREEN", 12.5], ["ATF", 12.5],
              ["SIG", 15.2], ["PWR", 15.2], ["SOL", 15.2], ["LOAD", 15.2]];
gland_z = 20;                      // centre height above the floor top
gland_nut = [[12.5, 17.5], [15.2, 22.5]];   // [hole, locknut across corners]
function nut_of(d) = d < 14 ? gland_nut[0][1] : gland_nut[1][1];
function term_center(name) = let(t = pcb_terminals[search([name], pcb_terminals, 1, 0)[0]]) (t[1] + t[2]) / 2;
// x of each gland from the box's outer left edge: centred on its terminals, but
// at least a nut's half-width + 1 mm clear of the 16 mm corner bosses
function gland_x(g) = let(d = g[1], lo = wall + 8 + nut_of(d) / 2 + 1, hi = wall + IW - 8 - nut_of(d) / 2 - 1,
                          c = wall + pcb_pos[0] + term_center(g[0])) min(max(c, lo), hi);
glands = [for (g = gland_size) [g[0], g[1], gland_x(g)]];
bundle_d = 8;                      // a cable's cores, fanned out: modelled as an 8 mm bundle

/* [Mounting] */
// Ears make the box 244 mm wide: needs a 250 mm bed. For a 220 x 220 bed set
// ears = false; the box is then 216 mm wide and bolts down through four M5
// holes in the floor, placed clear of the boards.
ears = true;
floor_holes = [[60, 10], [150, 10], [45, 100], [170, 100]];  // inside coordinates
floor_hole_d = 5.5;
ear_len = 14;
ear_slot = [14, 6.6];              // M6 slots
ear_t = 5;

/* [Vents] */
vents = true;

$fn = 48;
OW = IW + 2 * wall;
OD = ID + 2 * wall;
OH = floor_t + IH;

module rbox(size, r) {
    hull() for (x = [r, size[0] - r], y = [r, size[1] - r])
        translate([x, y, 0]) cylinder(h = size[2], r = r);
}

// inside coordinates -> outside
module inside() { translate([wall, wall, floor_t]) children(); }

corner_boss = [[0, 0], [IW, 0], [0, ID], [IW, ID]];

module standoff(h, hole = standoff_hole) {
    difference() {
        cylinder(h = h, d = standoff_d);
        translate([0, 0, 1]) cylinder(h = h, d = hole);
    }
}

module box(with_standoffs = true) {
    difference() {
        union() {
            difference() {
                rbox([OW, OD, OH], corner_r);
                inside() rbox([IW, ID, IH + 1], max(corner_r - wall, 0.5));
            }
            // corner bosses for the lid screws (fused into both walls)
            intersection() {
                rbox([OW, OD, OH], corner_r);
                inside() for (c = corner_boss)
                    translate([c[0], c[1], 0]) cylinder(h = IH, d = boss_d);
            }
            // mounting ears on the short sides
            if (ears) for (x = [-ear_len, OW]) translate([x, OD / 2 - 15, 0]) difference() {
                hull() {
                    cube([ear_len, 30, ear_t]);
                    translate([x < 0 ? ear_len - 1 : 0, 0, 0]) cube([1, 30, ear_t + 8]);
                }
                translate([ear_len / 2, 15, -1]) hull() for (dy = [-1, 1] * (ear_slot[0] - ear_slot[1]) / 2)
                    translate([0, dy, 0]) cylinder(h = ear_t + 2, d = ear_slot[1]);
            }
            if (with_standoffs) inside() {
                for (h = mega_holes)
                    translate([mega_pos[0] + h[0], mega_pos[1] + h[1], 0]) standoff(mega_standoff_h);
                for (h = pcb_holes)
                    translate([pcb_pos[0] + h[0], pcb_pos[1] + h[1], 0]) standoff(pcb_standoff_h);
            }
        }
        if (!ears) inside() for (h = floor_holes)
            translate([h[0], h[1], -floor_t - 1]) cylinder(h = floor_t + 2, d = floor_hole_d);
        // corner bosses: heat-set insert holes from the top
        inside() for (c = corner_boss)
            translate([c[0] + (c[0] == 0 ? boss_off : -boss_off), c[1] + (c[1] == 0 ? boss_off : -boss_off), IH - insert_depth])
                cylinder(h = insert_depth + 1, d = insert_d);
        // USB-B access, left wall
        translate([-1, wall + mega_pos[1] + usb_center_y - usb_cut[0] / 2,
                   floor_t + mega_standoff_h + 1.6 + 5.5 - usb_cut[1] / 2])
            cube([wall + 2, usb_cut[0], usb_cut[1]]);
        // glands and their labels, front wall
        for (g = glands) {
            translate([g[2], wall + 1, floor_t + gland_z]) rotate([90, 0, 0])
                cylinder(h = wall + 2, d = g[1]);
            translate([g[2], 0.6, floor_t + gland_z + 14]) rotate([90, 0, 0])
                linear_extrude(1) text(g[0], size = 4, halign = "center",
                                       font = "DejaVu Sans:style=Bold");
        }
        // vent slots high on the right wall (the relays and the regulator are the warm parts)
        if (vents) for (y = [OD / 2 - 40 : 8 : OD / 2 + 40])
            translate([OW - wall - 1, y - 1.5, OH - 18]) cube([wall + 2, 3, 12]);
        // where the Mega goes, on the floor
        inside() translate([mega_pos[0] + 50.8, mega_pos[1] + 26.7, -0.6])
            linear_extrude(1) text("MEGA (USB TO THE LEFT)", size = 5, halign = "center", valign = "center",
                                   font = "DejaVu Sans:style=Bold");
    }
}

module lid() {
    difference() {
        union() {
            rbox([OW, OD, lid_t], corner_r);
            // locating lip that drops inside the walls
            translate([wall + 0.4, wall + 0.4, -2])
                difference() {
                    rbox([IW - 0.8, ID - 0.8, 2], max(corner_r - wall, 0.5));
                    translate([2, 2, -1]) rbox([IW - 4.8, ID - 4.8, 4], 0.5);
                }
        }
        for (c = corner_boss)
            translate([wall + c[0] + (c[0] == 0 ? boss_off : -boss_off), wall + c[1] + (c[1] == 0 ? boss_off : -boss_off), -3]) {
                cylinder(h = lid_t + 4, d = lid_screw_clear);
                translate([0, 0, 3 + lid_t - 1.8]) cylinder(h = 2, d1 = lid_screw_clear, d2 = 6.5);
            }
        // notches in the lip clear the corner bosses
        for (c = corner_boss)
            translate([wall + c[0], wall + c[1], -3]) cylinder(h = 3, d = boss_d + 1);
        translate([OW / 2, OD / 2, lid_t - 0.6]) linear_extrude(1)
            text("RAV4 TCU", size = 10, halign = "center", valign = "center",
                 font = "DejaVu Sans:style=Bold");
        translate([OW / 2, OD / 2 - 14, lid_t - 0.6]) linear_extrude(1)
            text("RELAYS OFF = FACTORY ECU", size = 4.5, halign = "center", valign = "center",
                 font = "DejaVu Sans:style=Bold");
    }
}

// ------------------------------------------------------------ fit check
// Everything that goes inside, as solid blocks: the board with each part at
// its height and 2 mm of solder joints under it, the Mega with its parts and
// jacks, and each gland's locknut plus cable entry.
pcb_top = pcb_standoff_h + 1.6;
module pcb_shape(h) linear_extrude(h) polygon(pcb_outline);
module board_contents() {
    translate([pcb_pos[0], pcb_pos[1], 0]) {
        translate([0, 0, pcb_standoff_h - 2]) pcb_shape(2);          // solder joints, trimmed leads
        translate([0, 0, pcb_standoff_h]) pcb_shape(1.6);            // the board
        for (p = pcb_parts) translate([p[1], p[2], pcb_top]) cube([p[3] - p[1], p[4] - p[2], p[5]]);
    }
}
module mega_contents() {
    translate(mega_pos) {
        translate([0, 0, mega_standoff_h]) cube([101.6, 53.3, 1.6]);
        // its parts (all under 6 mm), and the sockets the board plugs into
        translate([0, 0, mega_standoff_h + 1.6]) cube([101.6, 53.3, 6]);
        // USB-B and power jacks: taller, under the board's cut-away corner
        translate([-6.2, usb_center_y - 6, mega_standoff_h + 1.6]) cube([16, 12, 11]);
        translate([-1.8, 3.3, mega_standoff_h + 1.6]) cube([14.2, 9, 11]);
    }
}
module gland_contents() {
    for (g = glands) {
        translate([g[2] - wall, 0.01, gland_z]) rotate([-90, 0, 0]) {
            cylinder(h = 9, d = nut_of(g[1]));     // thread + locknut
            cylinder(h = 14, d = 8);               // cable on its way to the board
        }
    }
}
// The wires: each cable's cores fan from its gland to its terminal group,
// ending in 6 mm of straight wire into the terminals' openings.
function tgroup(name) = pcb_terminals[search([name], pcb_terminals, 1, 0)[0]];
module terminal_entry(t) {
    translate([pcb_pos[0] + t[1] - 1.75, pcb_pos[1] + t[3] - 6, pcb_top + 2]) cube([t[2] - t[1] + 3.5, 5.99, 5]);
}
module dot(p) translate(p) sphere(d = bundle_d, $fn = 16);
module wiring() {
    for (g = glands) hull() { dot([g[2] - wall, 14, gland_z]); terminal_entry(tgroup(g[0])); }
}
// Room to work: between the gland nuts (9 mm in from the wall) and the
// board's front edge there must be at least min_work_gap mm of clear space
// across the whole box, for fingers, a small screwdriver and the wires.
min_work_gap = 15;
work_gap = pcb_pos[1] - 9;
module work_room() { translate([9, 9.01, 0.01]) cube([IW - 18, work_gap - 0.02, IH - 3]); }

module all_contents() {
    board_contents();
    mega_contents();
    gland_contents();
}
module fit_check() {
    // Each line must be EMPTY.
    // (the standoffs are left out here: the board and the Mega sit on them by design)
    intersection() { box(false); inside() all_contents(); }
    inside() {
        intersection() { board_contents(); mega_contents(); }
        intersection() { gland_contents(); union() { board_contents(); mega_contents(); } }
        // nothing may reach the lid's locating lip (top 3 mm)
        intersection() { all_contents(); translate([-10, -10, IH - 3]) cube([IW + 20, ID + 20, 10]); }
        // the wires: through the air, not through parts or the lid lip
        intersection() { wiring(); union() { board_contents(); mega_contents(); } }
        intersection() { wiring(); translate([-10, -10, IH - 3]) cube([IW + 20, ID + 20, 10]); }
        intersection() { work_room(); union() { board_contents(); mega_contents(); } }
    }
    intersection() { box(false); inside() union() { wiring(); work_room(); } }
    assert(work_gap >= min_work_gap, str("only ", work_gap, " mm to wire the terminals"));
    // neighbouring glands' nuts must not touch
    for (k = [0 : len(glands) - 2])
        assert(glands[k + 1][2] - glands[k][2] >= (nut_of(glands[k][1]) + nut_of(glands[k + 1][1])) / 2 + 1,
               str("glands ", glands[k][0], " and ", glands[k + 1][0], " too close"));
}
module populated() {
    color("gold") box();
    inside() {
        color("darkgreen") board_contents();
        color("steelblue") mega_contents();
        color("dimgray") gland_contents();
        color("crimson") wiring();
    }
}

// ------------------------------------------------------------ build-book views
// part = "build", bstep = 1..4: the assembly so far, with this step's pieces
// highlighted (orange) and earlier ones grey.
bstep = 4;
module hi(n) { if (bstep == n) color("orangered") children(); else if (bstep > n) color("silver") children(); }
module build_view() {
    color("gold") box();
    // 1: heat-set inserts in the corner bosses
    hi(1) inside() for (c = corner_boss)
        // drawn half out of their holes, as if being pressed in (step 1 only)
        translate([c[0] + (c[0] == 0 ? boss_off : -boss_off), c[1] + (c[1] == 0 ? boss_off : -boss_off),
                   IH - (bstep == 1 ? 1 : 6)])
            cylinder(h = 6.2, d = insert_d + 1.5);
    // 2: glands (dome outside, locknut inside)
    hi(2) for (g = glands) {
        nut = g[1] < 14 ? gland_nut[0][1] : gland_nut[1][1];
        translate([g[2], 0, floor_t + gland_z]) rotate([90, 0, 0]) {
            cylinder(h = 10, d = nut - 3, $fn = 6);
            translate([0, 0, 10]) cylinder(h = 8, d1 = nut - 3, d2 = g[1] - 4);
        }
        translate([g[2], wall, floor_t + gland_z]) rotate([-90, 0, 0]) cylinder(h = 5, d = nut, $fn = 6);
    }
    // 3: the Mega, then the board plugged onto it
    hi(3) inside() {
        board_contents();
        mega_contents();
    }
    // 4: the lid, lifted
    hi(4) translate([0, 0, OH + 25]) lid();
}

if (part == "box") box();
// The lid is exported upside down (top face on the bed, lip up) so it prints without supports.
else if (part == "lid") translate([0, OD, lid_t]) rotate([180, 0, 0]) lid();
else if (part == "fit_check") fit_check();
else if (part == "fit") populated();
else if (part == "build") build_view();
else if (part == "assembly") { box(); color("grey", 0.6) translate([0, 0, OH + 10]) lid(); }
