# Hardware notice

The case files in this directory are a **derivative work of the
[Ploopy Adept Trackball](https://github.com/ploopyco/adept-trackball)**
(Rev3 mechanicals) by Ploopy Corporation, licensed under the
**CERN Open Hardware Licence Version 2 – Strongly Reciprocal (CERN-OHL-S-2.0)**.
These derivative files are released under the same licence (see `LICENSE`).

## Modifications made to the original design

- Whole assembly uniformly scaled by **1.27×** (45 mm → 57.15 mm ball, i.e. a No. 57 pool ball).
- Bearing pockets re-cut for **MR63ZZ** bearings: dowel holes Ø3.81 → Ø3.00,
  bearing axes moved to 31.575 mm from the ball centre (three pockets, 120° apart).
- Top shell: trimmed in the button area for clearance (`stl/top-shell-57.stl`).
- Bottom shell:
  - assembly rotated −10° so the base is flat on the printer bed;
  - six pockets for **Omron D2FC-F-7N** switches (seat = button pad − 7.70 mm),
    leg channels, wire notches and slots between them;
  - wire channels routed at the depth of the switch legs;
  - recessed cradle for an **RP2040-Zero** at the original USB exit, with
    relief under the USB-C plug.
- Printed bearing dowels: Ø3.05 × 9.8 mm (tuned for the author's printer).

## Source files

- `source/bottom-shell-57.f3d` — editable Fusion 360 archive of the bottom shell
  (the archive also contains the scaled assembly it was built from).
- The top shell is provided as STL. It is the scaled Ploopy Adept Rev3 top shell
  with the bearing-pocket changes above plus manual trimming; the original
  editable source is Ploopy's `Adept-Rev3.STEP`.
