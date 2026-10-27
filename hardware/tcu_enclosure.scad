// TCU enclosure for the 1999 RAV4 transmission controller.
//
// Holds, on the floor of the box:
//   * Arduino Mega 2560 on standoffs (official hole pattern), USB-B reachable
//     through the left wall for flashing and logging
//   * the main board (perfboard 80 x 120: PROFETs, relays, ULN2803, optos,
//     LM393, screw terminals) on four M3 standoffs
//   * the 12 V -> 5 V buck converter in a cradle with zip-tie slots
//   * the MPU-6050 gyro (GY-521) on two standoffs, FLAT, so its Z axis is
//     vertical when the box is bolted in level
//   * seven cable glands in the front wall, one per connector X1-X6 plus the
//     dummy-load heatsink cable, with engraved labels
//   * mounting ears for M6 bolts, and a screw-on lid (M3 heat-set inserts)
//
// The three 15 ohm dummy resistors make 14-28 W of heat. They do NOT go in
// this box: bolt them to a finned aluminium heatsink outside it (see
// hardware/README.md). The "LOAD" gland carries their four wires.
//
// Print in PETG or ASA, NOT PLA: behind the glovebox of a car parked in the
// sun gets hotter than PLA's ~55 C softening point. Measure your boards and
// glands and adjust the sizes below before printing.
//
// Render one part at a time:  part = "box";  or  part = "lid";
part = "box";  // [box, lid, assembly]

/* [Box] */
IW = 206;          // inside width  (x)
ID = 142;          // inside depth  (y)
IH = 45;           // inside height (z)
wall = 3;
floor_t = 3;
lid_t = 3;
corner_r = 4;

/* [Arduino Mega 2560] */
mega_pos = [7.5, ID - 4 - 53.3];   // board origin (USB/DC-jack corner)
mega_holes = [[13.97, 2.54], [15.24, 50.8], [66.04, 7.62],
              [66.04, 35.56], [90.17, 50.8], [96.52, 2.54]];
mega_standoff_h = 6;
usb_center_y = 38.1;               // USB-B centre along the board edge
usb_cut = [14, 13];                // wall cut-out for the USB-B plug (y, z)

/* [Main board (perfboard)] */
main_size = [80, 120];
main_pos = [IW - 6 - 80, 16];      // 16 mm back: the glands poke ~10 mm inside
main_hole_inset = 4;
main_standoff_h = 6;

/* [Buck converter cradle] */
buck_size = [48, 27];              // inside of the cradle; measure your module
buck_pos = [8, 16];

/* [Gyro (GY-521 / MPU-6050)] */
gyro_pos = [75, 20];               // centre of the first hole
gyro_hole_spacing = 15.24;         // VERIFY on your module
gyro_standoff_h = 5;

/* [Standoffs] */
standoff_d = 7;
standoff_hole = 2.6;               // self-tapping M3 / pilot for M2.5 screw
insert_d = 4.2;                    // M3 heat-set insert
insert_depth = 8;
boss_d = 16;                       // corner bosses, centred on the inside corners
boss_off = 2.5;                    // insert offset from the inside corner (x and y)
lid_screw_clear = 3.4;

/* [Cable glands (front wall)] */
// [label, hole diameter]: PG9 = 15.2 mm, PG7 = 12.5 mm
glands = [["PWR", 15.2], ["SOL", 15.2], ["SIG", 15.2], ["LOAD", 15.2],
          ["CON", 12.5], ["OLED", 12.5], ["ATF", 12.5]];
gland_z = 20;                      // centre height above the floor top
gland_pitch = 25;

/* [Mounting] */
// Ears make the box 244 mm wide: needs a 250 mm bed. For a 220 x 220 bed set
// ears = false; the box is then 212 mm wide and bolts down through four M5
// holes in the floor, placed clear of the boards.
ears = true;
floor_holes = [[62, 66], [113, 66], [140, 7], [196, 7]];   // inside coordinates
floor_hole_d = 5.5;
ear_len = 16;
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

module box() {
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
            inside() {
                for (h = mega_holes)
                    translate([mega_pos[0] + h[0], mega_pos[1] + h[1], 0]) standoff(mega_standoff_h);
                for (x = [main_hole_inset, main_size[0] - main_hole_inset],
                     y = [main_hole_inset, main_size[1] - main_hole_inset])
                    translate([main_pos[0] + x, main_pos[1] + y, 0]) standoff(main_standoff_h);
                for (dx = [0, gyro_hole_spacing])
                    translate([gyro_pos[0] + dx, gyro_pos[1], 0]) standoff(gyro_standoff_h);
                // buck cradle: a low rim
                translate([buck_pos[0] - 2, buck_pos[1] - 2, 0]) difference() {
                    cube([buck_size[0] + 4, buck_size[1] + 4, 4]);
                    translate([2, 2, -1]) cube([buck_size[0], buck_size[1], 6]);
                }
            }
        }
        if (!ears) inside() for (h = floor_holes)
            translate([h[0], h[1], -floor_t - 1]) cylinder(h = floor_t + 2, d = floor_hole_d);
        // corner bosses: heat-set insert holes from the top
        inside() for (c = corner_boss)
            translate([c[0] + (c[0] == 0 ? boss_off : -boss_off), c[1] + (c[1] == 0 ? boss_off : -boss_off), IH - insert_depth])
                cylinder(h = insert_depth + 1, d = insert_d);
        // zip-tie slots under the buck converter
        inside() for (x = [buck_pos[0] + 8, buck_pos[0] + buck_size[0] - 12])
            for (y = [buck_pos[1] + 3, buck_pos[1] + buck_size[1] - 6])
                translate([x, y, -floor_t - 1]) cube([4, 3, floor_t + 2]);
        // USB-B access, left wall
        translate([-1, wall + mega_pos[1] + usb_center_y - usb_cut[0] / 2,
                   floor_t + mega_standoff_h + 1.6 + 5.5 - usb_cut[1] / 2])
            cube([wall + 2, usb_cut[0], usb_cut[1]]);
        // glands and their labels, front wall
        g0 = OW / 2 - (len(glands) - 1) * gland_pitch / 2;
        for (i = [0 : len(glands) - 1]) {
            x = g0 + i * gland_pitch;
            translate([x, wall + 1, floor_t + gland_z]) rotate([90, 0, 0])
                cylinder(h = wall + 2, d = glands[i][1]);
            translate([x, 0.6, floor_t + gland_z + 12]) rotate([90, 0, 0])
                linear_extrude(1) text(glands[i][0], size = 4, halign = "center",
                                       font = "DejaVu Sans:style=Bold");
        }
        // vent slots high on the right wall (the relays and buck are the warm parts)
        if (vents) for (y = [OD / 2 - 40 : 8 : OD / 2 + 40])
            translate([OW - wall - 1, y - 1.5, OH - 18]) cube([wall + 2, 3, 12]);
        // gyro orientation mark on the floor
        inside() translate([gyro_pos[0] + gyro_hole_spacing / 2, gyro_pos[1] + 14, -0.6])
            linear_extrude(1) text("GYRO: CHIP UP, FLAT", size = 3.5, halign = "center",
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

if (part == "box") box();
// The lid is exported upside down (top face on the bed, lip up) so it prints without supports.
else if (part == "lid") translate([0, OD, lid_t]) rotate([180, 0, 0]) lid();
else { box(); color("grey", 0.6) translate([0, 0, OH + 10]) lid(); }
