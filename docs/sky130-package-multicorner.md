# Sky130 technology package: multi-corner extension (unit F1)

Date: 2026-10-27
Status: unit F1 deliverable of the EOS fix track (fixes P3-4).
Package tier after this unit: experimental (unchanged). This
document is the addendum to docs/sky130-package-findings.md for
finding F4; the unit 3.0 findings F1 to F5 stand as written.

## 1. What changed

`platforms/sky130/manifest.yaml` (package version 3.0.1 to
3.1.0) now records the full Liberty corner set of the pinned
open_pdks build, not only the single tt_025C_1v80 corner that
the ORFS v3.0 sky130hd platform ships:

- 18 new Liberty collateral entries, each with its path in the
  open_pdks tree, the version identifier
  open_pdks-8afc8346a57fe1ab7934ba5a6056ea8b43078e71 (the ciel
  build the unit 3.4 pilot hardening pinned) and its SHA-256
  computed from the local pinned tree on 2026-10-27. The 16
  standard corners are ss_100C_1v40, ss_100C_1v60,
  ss_n40C_1v28, ss_n40C_1v35, ss_n40C_1v40, ss_n40C_1v44,
  ss_n40C_1v60, ss_n40C_1v76, ff_100C_1v65, ff_100C_1v95,
  ff_n40C_1v56, ff_n40C_1v65, ff_n40C_1v76, ff_n40C_1v95,
  tt_025C_1v80 and tt_100C_1v80, plus the two ccsnoise
  variants (ss_n40C_1v60 and ff_n40C_1v95). The open_pdks
  tt_025C_1v80 file is recorded as its own entry
  (liberty_tt_025c_1v80_openpdks) because it is not the same
  file as the ORFS one; see finding M3.
- 17 new scenarios pairing PVT corners with RC corners as
  Section 11 requires: an estimate-level scenario for each of
  the 15 standard corners the package did not already cover,
  and extracted-level scenarios for ss_100C_1v60 and
  ff_n40C_1v95, the slow and fast extremes a signoff pairing
  uses first. Every scenario pairs its PVT corner with the
  typical RC corner, because that is the only RC corner in
  either pinned tree (finding M4).
- No code changed in this unit. The package model and checker
  from unit 3.0 already represent multiple Liberty corners and
  validate scenario corners by name (the scenario_unknown_corner
  rule), so the extension is data only and the test counts are
  unchanged (Section 4).

## 2. Method

The enumeration is from the pinned tree itself, not from a
catalogue: the sky130A libs.ref/sky130_fd_sc_hd/lib directory
of the local ciel open_pdks build 8afc8346 was listed, every
file was hashed, and the sky130B copy of the same directory
was hashed and compared (byte-identical for all 18 files, so
one entry per corner covers both variants). The checker rules
that need only cell inventories and headers were then applied
to the real files the same way unit 3.0 applied them: cell
name sets were extracted from every Liberty file; the unit
headers and each file's default operating condition were read
and compared against its corner name; and the open_pdks
tt_025C_1v80 file was compared line by line against the ORFS
tt_025C_1v80 file recorded in unit 3.0. As in unit 3.0, the
pin-level checker rules are proven on the synthetic CI
fixtures; the real-collateral run is at cell-inventory and
header level, and that limitation is stated, not hidden.

## 3. Findings

- M1 (enumeration): the pinned open_pdks build ships 18
  sky130_fd_sc_hd Liberty files: 16 standard PVT corners and
  2 ccsnoise variants. The ORFS v3.0 platform ships exactly 1
  of the 16 (tt_025C_1v80). No corner was invented; every
  recorded corner is a file in the pinned tree.
- M2 (pass): all 18 files carry the identical 428-cell
  inventory, and that inventory equals the ORFS tt_025C_1v80
  inventory counted in unit 3.0. All 18 headers agree on
  units (time 1ns, voltage 1V, current 1mA, capacitive load
  1pf), matching the manifest's declared units, and every
  file's default operating condition and voltage match its
  filename corner.
- M3 (inconsistency, named, never patched): the two pinned
  tt_025C_1v80 Liberty files are not the same file. They have
  the same cell inventory and the same normalized line count
  (172,722 lines), but 12,436 lines per side differ in decimal
  formatting precision alone; after canonicalizing decimal
  formatting, 507 lines per side still differ. Those are: a
  systematic pg_type disagreement on 850 of 1729 pg pins
  (every VPB pin in all 428 cells reads pwell in the ORFS
  file and nwell in the open_pdks file; 422 VNB pins read
  the reverse), the violation_delay_degrade_pct attribute
  quoted in one file and unquoted in the other (479 lines,
  same value 10.0), and a small number of last-digit numeric
  differences (for example 8.6055e-05 against 8.6054e-05).
  This is the class of disagreement the checker's
  pg_pin_mismatch rule exists to catch across views; here it
  appears between two pinned versions of the same view. Both
  files are recorded with distinct hashes so no consumer can
  silently substitute one for the other, and neither file was
  modified.
- M4 (F4 closure, partial by construction): finding F4 is
  closed for Liberty PVT coverage. The package now records
  every PVT corner the pinned collateral carries, with
  scenarios pairing them as Section 11 requires. F4 is not
  closed for RC coverage: neither pinned tree ships an ss or
  ff RC corner, so all scenarios pair with the typical RC
  corner and the manifest records that as a known gap. The
  ccsnoise variants are recorded as collateral and define no
  scenario; they carry noise models for an existing operating
  condition, not a separate PVT corner.

## 4. Counts

No code changed. Local gates on this change: cmake configure
and build exit 0; ctest 24/24 suites passed;
tools/check_layers.py OK; pytest tools/tests 11 passed. Test
counts unchanged: 218 GoogleTest cases, 24 ctest suites,
checker pytest 11.
