# Phase 0 exit report: upstream ORFS baseline

Date: 2026-10-24
Status: Phase 0 exit condition met, with one disclosed gap (LVS, item L3).

## 1. What Phase 0 required, and the verdict

From the approved plan (Section 17, Roadmap):

> Phase 0 deliverable: pinned tools, licences audited, one PDK, 3 to 5
> reference blocks run through upstream ORFS and one PV reference flow.
> Exit condition: reproducible outputs, known limitations list, baseline
> QoR and runtime metrics recorded.

| Requirement | Verdict | Evidence |
|---|---|---|
| Pinned tools | Met | `third_party_manifest.yaml` (PR 2), Section 4 below |
| Licences audited | Met | `docs/licence-audit.md` (PR 2) |
| One PDK | Met | Sky130, ORFS sky130hd platform collateral |
| 3 to 5 reference blocks through upstream ORFS | Met | gcd, aes, ibex; all 14 of ORFS's own regression rules pass for each (Section 2) |
| One PV reference flow | Met with a gap | gcd: GDS stream-out works, KLayout DRC clean, KLayout LVS blocked by an upstream collateral defect (Section 5, item L3) |
| Reproducible outputs | Met | gcd run twice end to end; headline metrics bit-identical across the two passes |
| Known limitations list | Met | Section 6 |
| Baseline QoR and runtime recorded | Met | Sections 2 and 3 |

## 2. Baseline results

Method: unmodified ORFS v3.0 flow (tag v3.0, commit 181e91337761),
validated with ORFS's own `genMetrics.py` and `checkMetadata.py` against
each design's `rules-base.json`. All 14 rules pass for all three designs.
Areas in µm², slack in ns, power in W, wirelength in µm.

Headline metrics:

| Metric | gcd | ibex | aes |
|---|---|---|---|
| Synth stdcell area | 2,623.77 | 133,615.65 | 85,862.35 |
| Finish setup worst slack | +0.1010 | +0.4728 | +0.3427 |
| Finish setup TNS | 0 | 0 | 0 |
| Finish instance area | 3,200.57 | 183,903 | 120,253 |
| Finish instance count | 433 | 19,313 | 17,853 |
| Core area | 6,349.84 | 294,554 | 427,310 |
| Die area | 6,887.34 | 299,105 | 434,571 |
| Total power | 0.00193 | 0.09039 | 0.15131 |
| Detailed route DRC errors | 0 | 0 | 0 |
| Detailed route wirelength | 6,567 | 646,224 | 528,418 |

Full rule tables (measured value vs the bound ORFS enforces):

gcd:
| Metric | Value | Bound |
|---|---|---|
| synth__design__instance__area__stdcell | 2623.7664 | <= 2725.24 |
| constraints__clocks__count | 1 | == 1 |
| placeopt__design__instance__area | 3014.14 | <= 3032 |
| placeopt__design__instance__count__stdcell | 426 | <= 454 |
| detailedplace__design__violations | 0 | == 0 |
| cts__design__instance__count__setup_buffer | 1 | <= 40 |
| cts__design__instance__count__hold_buffer | 0 | <= 40 |
| detailedroute__route__wirelength | 6567 | <= 6849 |
| detailedroute__route__drc_errors | 0 | <= 0 |
| finish__timing__setup__ws | 0.100989 | >= -0.05 |
| finish__design__instance__area | 3200.57 | <= 3685 |
| finish__timing__drv__setup_violation_count | 0 | <= 20 |
| finish__timing__drv__hold_violation_count | 0 | <= 100 |
| finish__timing__wns_percent_delay | 3.856434 | >= -10 |

ibex:
| Metric | Value | Bound |
|---|---|---|
| synth__design__instance__area__stdcell | 133615.648 | <= 153658 |
| constraints__clocks__count | 1 | == 1 |
| placeopt__design__instance__area | 162802 | <= 180835 |
| placeopt__design__instance__count__stdcell | 18527 | <= 21306 |
| detailedplace__design__violations | 0 | == 0 |
| cts__design__instance__count__setup_buffer | 0 | <= 1853 |
| cts__design__instance__count__hold_buffer | 0 | <= 1853 |
| detailedroute__route__wirelength | 646224 | <= 743158 |
| detailedroute__route__drc_errors | 0 | <= 0 |
| finish__timing__setup__ws | 0.472832 | >= -0.27 |
| finish__design__instance__area | 183903 | <= 211488 |
| finish__timing__drv__setup_violation_count | 0 | <= 926 |
| finish__timing__drv__hold_violation_count | 0 | <= 100 |
| finish__timing__wns_percent_delay | 4.101603 | >= -10 |

aes:
| Metric | Value | Bound |
|---|---|---|
| synth__design__instance__area__stdcell | 85862.3488 | <= 130102.1 |
| constraints__clocks__count | 1 | == 1 |
| placeopt__design__instance__area | 113931 | <= 166508 |
| placeopt__design__instance__count__stdcell | 17569 | <= 25938 |
| detailedplace__design__violations | 0 | == 0 |
| cts__design__instance__count__setup_buffer | 0 | <= 2256 |
| cts__design__instance__count__hold_buffer | 0 | <= 2256 |
| detailedroute__route__wirelength | 528418 | <= 911303 |
| detailedroute__route__drc_errors | 0 | <= 0 |
| finish__timing__setup__ws | 0.342688 | >= -0.31 |
| finish__design__instance__area | 120253 | <= 168776 |
| finish__timing__drv__setup_violation_count | 0 | <= 1128 |
| finish__timing__drv__hold_violation_count | 0 | <= 238 |
| finish__timing__wns_percent_delay | 5.707481 | >= -10.61 |

## 3. Runtime and reproducibility

Hardware for all runs: 2 CPUs, 7 GB RAM (a small shared VM). Read every
runtime against that.

- gcd: two independent full passes, 182 s and 240 s wall. Headline
  metrics bit-identical across both passes. This is the reproducibility
  evidence for the baseline.
- ibex: successful make pass 6,383 s (about 1 h 47 min) on 2026-10-23.
  Reaching it took five calendar days (first run 2026-10-18): the host VM
  restarts without warning every 25 to 60 minutes, ORFS's detailed route
  step writes its output only at the very end, so each restart discarded
  the attempt. 34 route attempts were killed this way.
- aes: successful make pass 12,960 s (3 h 36 min) on 2026-10-24, after
  4 route attempts were killed by restarts.
- The supervisor that ground through the restarts made 163 starts or
  resumes in total across ibex and aes. No genuine flow error occurred
  in any attempt; TritonRoute reduced violations monotonically in every
  observed attempt.

## 4. Toolchain, as pinned and as built

ORFS itself pins the flow tools; EOS's component pins (PR 2) are a
separate list and were not substituted for them.

- ORFS v3.0 (commit 181e91337761), scripts unmodified.
- OpenROAD at ORFS's pin ead3236e56d57362a8dc8b25658fb41a191eba17,
  built from source on this machine. Build facts that matter: SWIG
  4.1.0 (ORFS's DependencyInstaller pin; SWIG 4.5 generates broken
  wrappers for this tree), Boost 1.83 (this tree predates the asio API
  removals in 1.87), static Tcl 8.6, OR-Tools 9.14.6206,
  USE_TCL_READLINE=OFF, no Qt (see item L1). Submodules at ORFS's pins:
  OpenSTA 7a184d00dd88, ABC 95b3543e9286.
- Yosys 0.33 at ORFS's pin 2584903a0605, with YosysHQ/abc bb64142b0779
  (the ABCREV in Yosys's Makefile).
- eqy at ORFS's pin 0cc2ff01a9c1, installed and on PATH for every run;
  ORFS enables its equivalence-check steps when eqy is present.
- KLayout 0.30.12 (EOS's component pin). ORFS's installer floor for
  KLayout is 0.28.8; the PV runs were additionally checked under
  KLayout 0.28.16 (see item L3).
- Invocation overrides, identical for every baseline run:
  `OPENROAD_EXE` and `YOSYS_CMD` and `KLAYOUT_CMD` pointing at the
  pinned builds above, `TIME_CMD=` (item L4), `QT_QPA_PLATFORM=offscreen`,
  eqy and Yosys bin directories on PATH.

## 5. PV reference flow (gcd)

Run 2026-10-24 with ORFS's own `generate_abstract`, `drc` and `lvs`
targets on the gcd baseline outputs. Scripts and decks unmodified.

- GDS stream-out: works. `6_final.gds` and `6_1_merged.gds` produced
  (898,868 bytes each, identical content), plus `gcd.lef` and `gcd.lib`
  abstracts. The default flow simply never invokes this target (item L2).
- DRC: KLayout deck `platforms/sky130hd/drc/sky130hd.lydrc` ran to
  completion, report database holds 290 rule categories, violation
  count 0.
- LVS: blocked before comparison by a defect in ORFS v3.0's pinned
  platform CDL (item L3). Verdict: unknown, on the evidence below.

## 6. Known limitations

- L1. Final report images fail on this OpenROAD build. ORFS's
  `final_report.tcl` ends by calling `gui::show`; this build has no Qt
  and does not register that command, so the report step records
  `Error: final_report.tcl, 69 invalid command name "gui::show"` in
  `6_report.log`. Every metric artifact is written before that point and
  the 14-rule gate passes; the only missing outputs are the report's
  layout images. A Qt-enabled rebuild of the pinned OpenROAD would close
  it. Decision (project owner, 2026-10-24): accepted as a documented
  limitation; no Qt-enabled rebuild for Phase 0. Revisit only if the
  Phase 1 regression harness ever diffs full report output.
- L2. No GDS from the default flow. In ORFS v3.0, `6_final.gds` is
  produced only by the `generate_abstract` target. The baseline runs
  therefore left final DEF, ODB, SPEF, SDC and netlist on disk, and no
  GDS, until the PV flow ran that target (Section 5).
- L3. KLayout LVS cannot run at the ORFS v3.0 pin. KLayout's netlist
  reader rejects ORFS's own pinned `sky130hd.cdl` at the
  `sky130_fd_sc_hd__macro_sparecell` definition (platform CDL line
  11169): its internal call `XI1 ... / sky130_fd_sc_hd__conb_1` presents
  7 pins to the reader (the "/" separator token is counted) against
  conb_1's 6-pin definition. Reproduced with the platform CDL alone and
  with a 9-line excerpt, under KLayout 0.30.12 and 0.28.16, standalone
  and through `make lvs`. The design CDL reads cleanly and gcd does not
  instantiate the cell; this is an upstream collateral inconsistency,
  not a design or environment fault. No patch was applied: baseline
  discipline is unmodified upstream collateral. Options, if an LVS
  verdict is wanted later: a documented one-line deviation on a local
  copy of the CDL, or a Netgen path with open_pdks collateral.
- L4. `TIME_CMD=` deviation. `/usr/bin/time` is not reliable on this
  host across VM replacements, so ORFS's timing wrapper was blanked.
  The harness records wall time itself. This affects only runtime
  bookkeeping, never results.
- L5. Cosmetic: the pinned OpenROAD binary reports its version as
  "GITDIR-NOTFOUND" because it was built from a tarball without `.git`.
  Provenance is the pinned commit plus the build recipe, not the banner.

## 7. Exit assessment

Every element of the Phase 0 exit condition is recorded above:
reproducible outputs (gcd, twice, bit-identical), baseline QoR and
runtime for three reference blocks against ORFS's own regression gate,
and this limitations list, including the one genuine gap: no LVS verdict
exists at the ORFS v3.0 pin, because upstream's own LVS path is broken
there (L3). Phase 0 is complete on that basis.
