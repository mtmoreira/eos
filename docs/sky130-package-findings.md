# Sky130 technology package: formalization and findings (unit 3.0)

Date: 2026-10-26
Status: unit 3.0 deliverable. Package tier after this unit:
experimental (unchanged). The correlated tier is gated on units
3.1 and 3.2, not on this unit.

## 1. What was built

- `src/tech/package` now implements the plan Section 11 package
  model and consistency checker: `eos::tech::TechnologyPackage`
  (collateral items with immutable version and SHA-256, per-view
  cell and pin sets, macros with required views and black-box
  contracts, special cells, scenarios pairing PVT corners with RC
  corners, units, supply aliases) and `CheckPackage`, which
  returns findings under 13 named rules: missing_version,
  missing_hash, duplicate_collateral_id, cell_missing_in_lef,
  cell_missing_in_liberty, pin_mismatch, pg_pin_mismatch,
  supply_alias_ambiguous, unit_mismatch, macro_missing_view,
  black_box_without_contract, special_cell_unknown and
  scenario_unknown_corner. Findings are sorted by (rule, subject)
  so results are reproducible.
- CI tests run on synthetic packages: a consistent package passes
  with zero findings, and each injected inconsistency fails under
  its rule name (22 GoogleTest cases in
  tests/unit/tech/package/package_test.cc).
- `platforms/sky130/manifest.yaml` is formalized as data: 15
  collateral items the reference flow consumes, each with kind,
  path, the ORFS v3.0 pin (commit 181e91337761) as its version
  identifier and its SHA-256 computed from the local pinned tree
  on 2026-10-26; the special-cell list (ties, taps, fillers,
  decaps, antenna diode, CTS buffer and inverter); two scenarios
  pairing the tt_025C_1v80 PVT corner with the typical RC corner
  at estimate and extracted parasitics levels.

## 2. Real-collateral run: method

The checker rules that need only cell inventories and headers
were applied to the real sky130hd collateral in the local ORFS
v3.0 tree (the same tree the Phase 0 baseline used) on
2026-10-26: cell name sets were extracted from the Liberty
(428 cells), the merged cell LEF (441 macros) and the cell CDL
(437 subcircuits); units were read from the Liberty header; the
known CDL defect site was re-read directly. The full pin-level
comparison across all 428 cells is not claimed here: pin sets
were compared at cell-name level for this run, and the pin-level
rules are proven on the synthetic CI fixtures. That limitation
is stated, not hidden.

## 3. Findings

- F1 (defect, upstream, known from Phase 0): the cell CDL is
  internally inconsistent at `sky130_fd_sc_hd__macro_sparecell`
  (CDL line 11167). Its instance XI1 presents 6 nets plus the
  "/" separator to `sky130_fd_sc_hd__conb_1`, whose definition
  (CDL line 4943) has 6 pins (VGND VNB VPB VPWR HI LO); the
  KLayout netlist reader counts the separator and rejects the
  file (7 presented against 6 defined). Recorded, never
  patched. This is the defect that blocked KLayout LVS in
  Phase 0 and motivates the Netgen path in unit 3.1.
- F2 (inconsistency): four LEF macros exist in neither the
  Liberty nor the CDL: sky130_ef_sc_hd__decap_12,
  sky130_ef_sc_hd__fakediode_2, sky130_ef_sc_hd__fill_12 and
  sky130_ef_sc_hd__fill_8. They carry the ef library prefix in
  an fd library's merged LEF. A design instantiating one would
  have a physical view with no timing or LVS view.
- F3 (declared physical-only set): nine fd cells are in the LEF
  and the CDL but have no Liberty view: the fill cells
  (fill_1, fill_2, fill_4, fill_8) and the tap cells (tap_1,
  tap_2, tapvgnd_1, tapvgnd2_1, tapvpwrvgnd_1). These are
  physical-only cells with no timing arcs; the package records
  them as the declared physical-only set rather than treating
  their Liberty absence as an error. Every one of the 428
  Liberty cells is present in both the LEF and the CDL, and
  every CDL subcircuit is present in the LEF.
- F4 (coverage gap): the ORFS v3.0 sky130hd platform ships
  exactly one Liberty corner, tt_025C_1v80. SS and FF corners
  are absent from this collateral set, so the package's
  scenarios pair the single PVT corner with the typical RC
  corner only. Multi-corner signoff needs collateral beyond
  this platform tree (the open_pdks build), which unit 3.1
  identifies for the LVS path.
- F5 (pass): units are consistent in the Liberty header (time
  1ns, voltage 1V, capacitive load in pf), matching the
  manifest's declared units. No endcap cell is declared in the
  ORFS sky130hd platform; the tap cells serve the well-tap role
  in this flow, recorded in the manifest as an explicit none
  rather than an omission.

## 4. Counts

Before this unit: 196 GoogleTest cases, 23 ctest suites,
checker pytest 11. After: 218 GoogleTest cases (+22), 24 ctest
suites (+1, tech.package), checker pytest 11 (unchanged).
