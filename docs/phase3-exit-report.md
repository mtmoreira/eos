# Phase 3 exit report: technology pilot

Date: 2026-10-26
Status: Phase 3's units are complete and every locally
performable item of the chosen route's submission checklist
is satisfied on the pilot chip, but the Phase 3 exit
condition as a whole is NOT satisfied, for two stated
reasons: four checklist items are the gated remainder
(publication, spend and licence items that no unit was
authorized to perform), and the plan Section 14
macro-bearing reference item is carried as a gap. This
report is published as a pull request and is not merged by
the build driver: closing Phase 3, and deciding the gated
remainder, is the project owner's decision.

## 1. What Phase 3 required, and the verdict

From the approved plan (Section 17, Roadmap) and the
Phase 3 breakdown:

> Phase 3 deliverable: Sky130 package qualified to the
> correlated tier, final-layout PV, extraction correlation,
> chip integration ownership, one submission route selected.
> Exit condition: the exact submission checklist for the
> chosen route is satisfied on one pilot chip.

| Requirement | Verdict | Evidence |
|---|---|---|
| Sky130 package formalized, with a consistency checker proven on synthetic packages and run on the real collateral | Met | Unit 3.0 (PR 23): package model and 13-rule checker, manifest with hashes, findings F1 to F5 recorded and never patched |
| Final-layout PV on gcd with a real LVS verdict replacing Phase 0's UNKNOWN | Met as a measured verdict set | Unit 3.1 (PR 24): DRC PASS (0 violations, 145 KLayout categories), antenna PASS (0 and 0), LVS NOT CLEAN with two named discrepancies (D1 proven extraction-model artifact, D2 unresolved, bounded to 4 transistors in 2 instances of one cell type) |
| Extraction correlation, the correlated-tier gate | Met for capacitance, as measured; timing tolerance failed for a proven construction reason | Unit 3.2 (PR 25): basis C capacitance passes T1/T2/T3 (total ratio 0.996, median 1.193, 90.8 percent within a factor of two); T4 failed as measured (setup WNS delta 0.208 ns against 0.10 ns) and a lumped control run proved the delta is construction, with capacitance-driven deltas of 0.001 ns or less on equal construction |
| One submission route selected, with its exact checklist verified live | Met | Unit 3.3 (PR 26) decision memo; Route A (Tiny Tapeout Sky130) chosen by the owner on 2026-10-26, target run TTSKY26d closing 2026-11-30 |
| Pilot chip assembled in the route's harness and hardened, with chip integration ownership named | Met | Unit 3.4 (PR 27): serializer wrapper around gcd, RTL and gate-level tests pass, LibreLane hardening with the route's own pins, precheck 15 of 15, ownership table in the unit document |
| The route's exact submission checklist satisfied on the pilot chip | NOT met as a whole | Section 2: items 2 to 7 satisfied locally or by the local equivalent the route's actions define; items 1, 8, 9 and 10 are the gated remainder, not performed (Section 7) |
| Plan Section 14 macro-bearing reference item (a sky130 SRAM-bearing block through the reference flow with PV spot checks) | NOT satisfied, carried as a stated gap | Section 5: no SRAM-bearing block was run in Phase 3; the pilot contains no macro |
| Phase 3 exit condition as a whole | NOT met as measured and as gated | The checklist cannot be fully satisfied without the gated remainder, and the Section 14 item is open; the rest of this report states the evidence, and the decision belongs to the owner |

## 2. The submission checklist, item by item

The checklist is Section 3.3 of `docs/submission-route-decision.md`
(Route A, ten items). Unit 3.5 re-ran what runs locally on
the pilot's final artifacts; the pilot project itself is
local only (no remotes, HEAD `2cfda96`), and this wake
verified that the submission package on disk is byte-identical
to the unit 3.4 evidence: SHA-256 of the GDS, LEF, OAS,
nominal SPEF, gate-level netlist and final metrics file all
match the hashes published in `docs/pilot-chip-hardening.md`.

| # | Item (memo Section 3.3) | Status | Evidence |
|---|---|---|---|
| 1 | Project repository created from the Tiny Tapeout SKY template, Actions enabled, Pages source set to GitHub Actions | Not performed. Gated remainder | Template instantiated locally only (unit 3.4); creating the public repository is a publication the standing gates reserve to the owner |
| 2 | Design sources in `src/`, top module `tt_um_*` and unique | Satisfied locally | `project.v` plus `gcd.v`, top module `tt_um_mtmoreira_gcd` (unit 3.4, Section 3) |
| 3 | `info.yaml` complete: title, author, description, language, clock_hz, tiles, top_module, source_files, pinout labels | Satisfied locally | Unit 3.4; tt_tool `--check-docs`, which validates the project data, was re-run in unit 3.5 and passed |
| 4 | `docs/info.md` filled in (how it works, how to test) | Satisfied locally, re-checked in unit 3.5 | tt_tool `--check-docs` passed in both units (re-run this wake: "Documentation check passed successfully"); unit 3.4 also ran `--create-pdf` and produced the datasheet (SHA-256 in the unit document) |
| 5 | Testbench in `test/` (cocotb) passes | Satisfied locally, re-run in unit 3.5 | RTL: PASS, TESTS=1 PASS=1 FAIL=0 SKIP=0. Gate level against the hardened netlist: PASS with the same counts, and results.xml carries errors=0 failures=0, the gl_test action's own pass criterion. Both re-runs used Icarus Verilog 13.0 (this machine has no system Icarus 12.0, which the unit 3.4 RTL run used; the gate-level run used 13.0 in both units) |
| 6 | GDS action green: LibreLane hardening, Tiny Tapeout precheck all green, gate-level test, GDS viewer | Satisfied by the local equivalent the actions define | Hardening: unit 3.4 ran the action's own tt_tool `--harden` invocation (LibreLane 3.0.14, PDK open_pdks `8afc8346`), twice with identical metrics, flow errors 0. Precheck: re-run in unit 3.5 (Section 3). Gate-level test: item 5. The GDS viewer deploy is a Pages step of the public repository and has no local equivalent; it belongs to item 1's gated publication |
| 7 | Hardening done for the shuttle's PDK (sky130A) | Satisfied locally | Hardening PDK pin `8afc8346` and precheck PDK pin `0536d02d` recorded in unit 3.4; both are sky130A builds, and the precheck's layer and cell checks run against that PDK pass |
| 8 | Tile allocation purchased in the submission app for the target run | Not performed. Gated remainder | Spend; see Section 7 |
| 9 | Project submitted in the app, a revision submitted for the current GDS, before the closing date (2026-11-30 for TTSKY26d) | Not performed. Gated remainder | Submission; see Section 7 |
| 10 | Terms accepted by submitting: design published publicly and licensed under Apache-2.0 or compatible terms | Not performed. Gated remainder | Publication and licence commitment; see Section 7. The pilot wrapper source deliberately carries no licence header |

## 3. Final-layout PV on the pilot chip (unit 3.5)

Unit 3.1's method applied to the pilot's final merged
layout (the tt_submission GDS, hash-verified above), using
the route's own pinned tooling where the route defines it:

- DRC: Magic DRC 0 errors in the hardening flow's final
  metrics, and the unit 3.5 precheck re-run repeats Magic
  DRC plus the KLayout FEOL, BEOL, offgrid, pin-label,
  zero-area and nwell/urpm checks against the final GDS:
  all pass. Final route DRC errors 0 in the same metrics
  file. This is the route's DRC coverage for a tile macro;
  it is not the 145-category KLayout signoff deck run of
  unit 3.1, which applies to the ORFS gcd layout, and the
  two are not conflated.
- LVS: the hardening flow's Netgen comparison of the
  extracted layout against the post-synthesis netlist is
  clean: every LVS counter in the final metrics file
  (device and net differences, unmatched devices, nets and
  pins, property failures, errors) reads 0, re-read from
  the metrics file this wake. The reference here is the
  flow's own netlist, not the CDL reference of unit 3.1;
  unit 3.1's NOT CLEAN verdict on the ORFS gcd layout
  stands as measured and is not revised by this result.
- Antenna: 0 violating nets and 0 violating pins in the
  final metrics, checker and routing alike.
- Precheck re-run (the route's own precheck tooling, run
  without payment as unit 3.5 requires): tt-support-tools
  `precheck.py` at `01d5d281`, PDK `0536d02d`, Magic 8.3.623
  and KLayout 0.30.7 from the LibreLane 3.0.14 devshell,
  the same pins as unit 3.4. Result: precheck passed, all
  15 sky130A checks green (Magic DRC; KLayout FEOL, BEOL,
  offgrid, pin label overlapping drawing, zero area and
  Checks; pin, boundary, power pin, layer, cell name,
  urpm/nwell and analog pin checks; Verilog syntax check).
  One environment note, stated plainly: this machine has
  no FUSE, so the devshell AppImage cannot mount; the run
  used the AppImage's extracted tree with its nix store
  linked at `/nix`, which runs the identical pinned
  binaries.
- Named raw metrics, as in unit 3.4: the flow's gated
  checkers pass, but the raw per-corner STA metrics still
  carry max slew violations (167 at ss_100C_1v60 on the
  reset distribution net, 11 at tt_025C_1v80) and one max
  fanout violation (the clock root buffer, 16 against a
  limit of 10), which the flow's sky130 checker
  configuration does not gate. Setup worst slack is
  +7.7016 ns and hold +0.1036 ns across 9 corners at the
  20 ns flow period, from the hash-verified metrics file.

## 4. Extraction correlation: carried, and the tier claim

The extraction correlation is carried from unit 3.2 and
was not re-run on the pilot. The correlation is a
package-level result: two independent extractors agreeing
on the routed gcd block. The pilot is a different routed
design in a different flow (LibreLane, not the ORFS
reference flow), and no Magic-side extraction of the pilot
layout was run; a pilot correlation would be a new study,
not a checklist item, and the route's checklist does not
demand one. The pilot's nominal SPEF exists in the
submission package (hash in unit 3.4) but carries no
correlation claim here.

The package tier claim the whole Phase 3 evidence supports
is therefore exactly the claim of unit 3.2, Section 6, no
higher: the correlated tier for extraction capacitance on
gcd, on the accounting basis matching the SPEF's declared
scope. Resistance correlation is unmeasured. The timing
tolerance failure of unit 3.2 is a proven construction
difference (lumped versus distributed SPEF), with the
distributed Magic SPEF named as the gap behind it. The
package findings of unit 3.0 stand, including F4: the EOS
package manifest carries a single Liberty corner
(tt_025C_1v80) from the ORFS platform, so multi-corner
signoff through EOS's own package model remains open even
though the pilot's LibreLane hardening ran 9 corners from
its own PDK.

## 5. The Section 14 macro-bearing item: a stated gap

The plan's qualification section names a macro-bearing
reference item: a sky130 SRAM-bearing block through the
reference flow with PV spot checks. No unit of Phase 3 ran
one. The pilot contains no macro (its 3,237 instances are
logic, tap and fill cells), the Phase 0 designs are macro
free, and the package's only macro-adjacent evidence is
finding F1, the upstream `macro_sparecell` CDL defect,
which is a cell-level defect record, not a macro-bearing
flow run. This item is therefore carried as an open gap,
not satisfied and not waived: a future unit would harden
a sky130 SRAM-bearing block through the reference flow and
spot-check its PV, and until one does, no macro-bearing
claim is made for the package or the flow.

## 6. Per-unit summary

Every increment merged on green CI, verified at the
check-run level (both jobs, "CMake build" and "Structure
and layer checks") on the pull request and again on main
after the merge, except unit 3.5, which is this report and
is not merged by the driver.

| Unit | PR | Merge commit | What landed |
|---|---|---|---|
| 3.0 Sky130 package formalized | [PR 23](https://github.com/mtmoreira/eos/pull/23) | `a433029` | Package model and consistency checker (22 new GoogleTest cases), formalized manifest, real-collateral findings F1 to F5 |
| 3.1 Final-layout PV on gcd | [PR 24](https://github.com/mtmoreira/eos/pull/24) | `b89af1e` | DRC PASS, antenna PASS, LVS NOT CLEAN with discrepancies D1 and D2 named |
| 3.2 Extraction correlation | [PR 25](https://github.com/mtmoreira/eos/pull/25) | `e4f6385` | Capacitance correlation passes on basis C; timing tolerance T4 fails for a proven construction reason |
| 3.3 Submission route decision memo | [PR 26](https://github.com/mtmoreira/eos/pull/26) | `f5a97cb` | Routes verified live; recommendation recorded; the route decision itself was the owner's (Route A, 2026-10-26) |
| 3.4 Pilot chip assembly and hardening | [PR 27](https://github.com/mtmoreira/eos/pull/27) | `e286822` | Serializer wrapper, local hardening, precheck 15 of 15, ownership table, checklist status as of that unit |
| 3.5 Checklist satisfaction and this report | This PR | Not merged (owner decision) | Checklist re-run of Section 2 and 3, correlation carried (Section 4), SRAM item carried as a gap (Section 5), this report |

## 7. The gated remainder, verbatim

The following checklist items require payment, booking,
publication or a licence commitment. No unit was
authorized to perform them, and none was performed. This
is the gated remainder, verbatim from the route decision
recorded in the Phase 3 queue:

- Checklist item 1 (public repository creation), item 8
  (tile allocation purchase), item 9 (submission in the
  app) and item 10 (terms acceptance with public
  Apache-2.0-compatible licensing of the pilot project).
- TTSKY26d's closing date is 2026-11-30: if the gated
  remainder is approved after that date, the same
  checklist carries to TTSKY27a (deadline May 2027,
  estimate) and the exit report says so.

## 8. Final test counts, from real runs

Local gates run on 2026-10-26 on this branch, before
publishing. CI on this pull request re-runs the same gates.
This unit is docs-only; counts are unchanged.

- CMake configure and build: OK. One environment note,
  stated plainly: this machine lacks the system development
  packages the CI workflow installs, so the local gates ran
  against the workspace apt prefix that supplies them, with
  the prefix's paths given to CMake, SWIG, bison and the
  test loader explicitly. CI on this pull request is the
  clean-build verdict.
- CTest: 24 of 24 suites passed, no skips.
- GoogleTest cases: 218 in total, unchanged.
- `python3 tools/check_layers.py`: OK, 56 modules,
  7 layers.
- `python3 -m pytest tools/tests -q`: 11 passed, no skips.

## 9. Known limitations and gaps

- P3-1. LVS on the ORFS gcd final layout is NOT CLEAN
  (unit 3.1): D1 is a proven extraction-model artifact;
  D2, an intra-cell VGND split in `a21oi_2` affecting 4
  transistors in 2 instances, is unresolved between a
  modeling artifact and cell geometry. The clean-pass
  path named in unit 3.1 (the open_pdks Magic extraction
  route) was not run in Phase 3.
- P3-2. Extraction timing tolerance T4 failed as measured
  (unit 3.2): the proven cause is SPEF construction
  (lumped versus distributed), and a distributed Magic
  SPEF was not attempted. Resistance correlation is
  unmeasured.
- P3-3. The Section 14 SRAM-bearing reference item is
  unsatisfied (Section 5).
- P3-4. The EOS package carries one Liberty corner
  (finding F4); multi-corner signoff through EOS's own
  package model is not qualified.
- P3-5. The pilot's raw max slew and max fanout metrics
  are nonzero as measured (Section 3); the route's flow
  does not gate them, and no stronger claim is made.
- P3-6. The precheck's local tool versions are close to
  but not identical with the GDS action's nix pins (unit
  3.4, Section 6); the hardening and precheck PDK builds
  differ from each other because the route's own actions
  pin them that way.
- P3-7. Everything about the pilot is local evidence. No
  public repository, action run, submission record or
  shuttle index entry exists, because those are the gated
  remainder (Section 7); if the owner approves them, the
  checklist items move from local equivalents to the
  route's own records at that revision.

## 10. Exit assessment

Phase 3's deliverable exists: the Sky130 package is
formalized with a checker and named findings, final-layout
PV has measured verdicts on both the reference block and
the pilot chip, extraction capacitance is correlated on
gcd, one route was selected on live evidence by the
owner, and the pilot chip is assembled, tested and
hardened in that route's harness with every locally
performable checklist item satisfied and re-verified in
this unit.

The exit condition, however, is the exact checklist
satisfied on the pilot chip, and four of its ten items
are the gated remainder: they cannot be satisfied by any
unit under the standing authorization, and this report
does not declare them. The Section 14 macro-bearing item
is likewise open. Declaring Phase 3 complete on the local
evidence alone would contradict both facts, so this
report does not declare it.

What happens next is the owner's decision: merge this
report and close Phase 3 with the gated remainder and the
Section 14 gap recorded as not met, approve the gated
remainder (before 2026-11-30 for TTSKY26d, or on the same
checklist for TTSKY27a), direct a further unit for the
SRAM-bearing item, or set a different exit basis. Nothing
in this report starts any of those.
