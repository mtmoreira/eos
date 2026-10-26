# Phase 2 exit report: closure

Date: 2026-10-25
Status: the Phase 2 deliverable is built and its repair properties
are proven in CI-runnable form, but the Phase 2 exit condition, as
measured on the gcd comparison, is NOT met: EOS improved the
degraded design only slightly, the pinned upstream tool improved
it far more, and EOS was far slower. This report is published as
a pull request and is not merged by the build driver: closing
Phase 2, or deciding what happens instead, is the project owner's
decision.

## 1. What Phase 2 required, and the verdict

From the approved plan (Section 17, Roadmap):

> Phase 2 deliverable: repair, sizing, buffering and local
> resynthesis adapters under ECO transactions, all-scene
> acceptance. Exit condition: repairs preserve function and
> improve post-route metrics across all required scenes versus
> the pinned upstream baseline, at equal or better runtime.

| Requirement | Verdict | Evidence |
|---|---|---|
| ECO transaction protocol (checkpoint, edit, full re-analysis of every required scene, commit or roll back) | Met | Unit 2.4 (PR 15) |
| Sizing and buffering adapters | Met | Unit 2.4 (PR 15); fixture slack moves from construction values -0.1 ns to +0.5 ns (sizing) and -0.9 ns to +0.45 ns (buffering) |
| Local resynthesis adapter with a mandatory equivalence proof | Met | Unit 2.6 (PR 17): only a full-design kEquivalent proof lets a splice stand; kNotEquivalent and kUnknown roll back |
| All-scene acceptance (a regression in any scene refuses the candidate) | Met | Units 2.4 and 2.5; qualification test `CrossSceneRegressionIsRefusedByTheAllSceneRule` |
| Timing service over scenes on real collateral | Met | Unit 2.3 (PR 14): EOS reproduces the Phase 0 gcd finish slack +0.100989 ns within 0.018 ps; the pinned upstream evaluator agrees with EOS to float precision on identical inputs |
| Repairs preserve function | Met, in the CI-runnable form | Qualification suite, Section 3 of this report: sizing keeps every connection; resynthesis stands only on an equivalence proof; a corrupted splice is proved not-equivalent and rolled back |
| Repairs improve post-route metrics across all required scenes | Met on the fixture set; marginal on gcd | Qualification suite (improvement asserted per scene, not in aggregate); on degraded gcd EOS improved worst slack by 0.005 ns and TNS by 0.005 ns (Section 2) |
| Versus the pinned upstream baseline | NOT met | The pinned upstream repair improved worst slack by 0.107 ns and TNS by 0.277 ns on the identical degraded gcd: roughly 20x and 50x the EOS improvement (Section 2) |
| At equal or better runtime | NOT met | EOS closure run 415.6 s; upstream repair_timing 0.074 s (whole process 2.10 s) on the same machine (Section 2) |
| Phase 2 exit condition as a whole | NOT met as measured | The two comparison clauses above fail on the measured gcd evidence; the rest of this report states the evidence and the limitations, and the decision belongs to the owner |

## 2. The gcd A/B (unit 2.5, local evidence)

The comparison protocol, from the Phase 2 queue: the Phase 0
routed gcd with its clock period tightened from 3.4 ns to 2.9 ns
in the SDC, so setup slack is negative; EOS and the pinned
upstream tool start from the identical database, SDC and Liberty,
both at estimate fidelity. The pinned upstream binary is a local
build and cannot run in GitHub CI, so this is a local evidence
run (full logs in the unit 2.5 wake record), not a CI result.

Starting point, measured independently by both evaluators:

| Evaluator | Worst slack (ns) | TNS (ns) | Worst hold slack (ns) |
|---|---|---|---|
| EOS TimingService | -0.183051796 | -0.412361034 | +0.456130744 |
| Pinned OpenROAD STA | -0.183051801 | -0.412361046 | +0.456130785 |

The evaluators agree to about 1e-8 ns: the STA integration is
right, and the difference below is the search, not the analysis.
Upstream reported 3 endpoints with setup violations.

After repair:

| Tool | Worst slack (ns) | TNS (ns) | Closed | Wall time |
|---|---|---|---|---|
| EOS `eos::close_timing 10` | -0.177804438 | -0.407090139 | No | 415.6 s for the closure run |
| Pinned upstream `repair_timing -setup -hold -repair_tns 100 -max_passes 100` | -0.075766506 | -0.135271798 | No | 0.074 s of repair; 2.10 s whole process |

EOS accepted 10 iterations (its bound), screening 51 candidates
and rejecting 41 under the all-scene rule, for a worst-slack gain
of 0.005 ns and a TNS gain of 0.005 ns. Upstream gained 0.107 ns
of worst slack and 0.277 ns of TNS. As a cross-check, the EOS
evaluator on upstream's written DEF reports -0.075766504 ns and
-0.135271794 ns, matching upstream's own report to about 2e-9 ns.

The measured reasons are structural, not a single bug: the EOS
loop screens candidates over the whole design in name order with
first-improvement acceptance, and every screening costs two
full-design STA runs (about 8.1 s per candidate on gcd); upstream
repairs only the violating endpoints against its incremental
in-memory timing graph. At pin-capacitance fidelity a single
sizing step also gains little, which is why 10 accepted EOS
iterations moved worst slack by only 0.005 ns.

## 3. Per-unit summary

Every unit merged on green CI, verified at the check-run level
(both jobs, "CMake build" and "Structure and layer checks") on
the pull request and again on main after the merge, except unit
2.7, which is this report and is not merged by the driver.

| Unit | PR | Merge commit | What landed | Tests after merge |
|---|---|---|---|---|
| 2.0 Durable report bindings (limitation Q1 closed) | [PR 11](https://github.com/mtmoreira/eos/pull/11) | `39e5b5cee56ca96dd7f122233846962126785085` | Report bindings moved onto the Project and serialized into the project file; restore drops bindings whose artifact is missing and counts the drops | 115 GoogleTest cases, 15 ctest suites |
| 2.1 OpenDB import | [PR 12](https://github.com/mtmoreira/eos/pull/12) | `19b22d39e78938bac2f0ed1c001105b948416e2f` | 525 files from the pristine OpenROAD v2.0 pin `a5a56584fed1`, verified file by file, patch count 0; the ODB facade | 122 GoogleTest cases, 16 ctest suites |
| 2.2 OpenSTA import | [PR 13](https://github.com/mtmoreira/eos/pull/13) | `8197c237141e81b165d7bbcbc5501df1448b73e7` | 404 files from the pristine pin `0f2529d18218`, verified file by file, plus the CUDD 3.0.0 closure (no external CUDD dependency; disclosed in PROVENANCE); the STA facade; the precision fixture matching the pinned upstream binary | 126 GoogleTest cases, 17 ctest suites |
| 2.3 Timing service over scenes | [PR 14](https://github.com/mtmoreira/eos/pull/14) | `eba591e340be311af2382fb2a31ec61ba5e5707f` | Per-scene setup and hold metrics over an ODB design or Verilog, one report binding per scene; the gcd reproduction above | 143 GoogleTest cases, 18 ctest suites |
| 2.4 ECO transaction engine, sizing and buffering | [PR 15](https://github.com/mtmoreira/eos/pull/15) | `7e12ddff66f357332f84db9074e9665d3a87be8e` | The Section 9 protocol over an ODB checkpoint with full per-scene re-analysis and the Section 8 gate-based verdict; ResizeInstance and InsertBuffer adapters | 158 GoogleTest cases, 20 ctest suites |
| 2.5 Closure loop and the upstream A/B | [PR 16](https://github.com/mtmoreira/eos/pull/16) | `36a66627ba84bde0761c9039c74255268ff7fb09` | The L5 closure loop and `eos::close_timing`; the gcd A/B of Section 2, reported as measured | 169 GoogleTest cases, 21 ctest suites |
| 2.6 Resynthesis with equivalence proof | [PR 17](https://github.com/mtmoreira/eos/pull/17) | `2837464695c607c24db02f5396cdd861335f5720` | ABC imported at pin `416199292fbd` (2346 of 2347 upstream src files, byte-verified; the CUDD symbol collision solved at archive level, patch count 0); the delay-mapping and equivalence facade; the resynthesis adapter and its opt-in closure candidates | 177 GoogleTest cases, 22 ctest suites |
| 2.7 Exit suite and this report | This PR | Not merged (owner decision) | The qualification suite under `tests/qualification/` and this report | 183 GoogleTest cases, 23 ctest suites |

## 4. The qualification suite

`tests/qualification/phase2_exit_test.cc`, wired into CTest as
`qualification.phase2_exit`. The unit suites prove each mechanism
in isolation; this suite proves the exit condition's CI-runnable
form, composed through the closure loop and the ECO protocol.
The gcd comparison of Section 2 is not reproduced here: it needs
the local pinned upstream binary and the Phase 0 collateral,
neither of which runs in CI.

| Test | Exit clause it proves |
|---|---|
| `SizingImprovesEveryRequiredSceneAndCloses` | Improvement across all required scenes: two scenes start at -0.1 ns and -0.3 ns, one sizing step closes both (+0.5 ns, +0.3 ns), improvement asserted per scene; the swap keeps every net's connections exactly, and the accepted step is one ledger transaction under a proof-pass closure binding |
| `CrossSceneRegressionIsRefusedByTheAllSceneRule` | All-scene acceptance: the sizing step that fixes one scene's setup drops the other scene's hold slack to -0.10 ns, so the loop refuses it; design, connectivity and ledger are exactly as if never attempted, and the closure binding records proof unknown |
| `BufferingImprovesTheHighFanoutFixture` | The fixture set is broader than one design: a nine-flop high-fanout design closes by buffering (-0.9 ns to +0.45 ns) |
| `ResynthesisStandsOnlyOnAnEquivalenceProof` | Function preservation for a logic change: the double-inversion cone remaps only with a full-design kEquivalent verdict recorded in the outcome, and the slack moves -0.4 ns to +0.6 ns |
| `CorruptedSpliceIsProvedNotEquivalentAndRolledBack` | The proof gate catches a wrong function: an OR spliced where an AND belongs is proved kNotEquivalent and rolled back completely (instances, masters, connections and timing unchanged) |
| `ResynthesisThroughTheLoopPreservesFunctionAndCloses` | The same proof gate inside the closure loop: the double-inversion chain closes through a resynthesis candidate, one ledger transaction, proof-pass closure binding |

## 5. Final test counts, from real runs

Local gates run on 2026-10-25 on this branch, before publishing.
CI on this pull request re-runs the same gates.

- CMake configure and build: OK. One environment note, stated
  plainly: the driver VM was replaced by a restart in the middle
  of this unit's first clean build, which removed the apt
  packages the local build uses; the package set named in
  `.github/workflows/ci.yml` was reinstalled and the build was
  completed and passed in that restored environment. CI on this
  pull request is the clean-build verdict.
- CTest: 23 of 23 suites passed, no skips. The suites are the 22
  from unit 2.6 plus `qualification.phase2_exit`.
- GoogleTest cases: 183 in total (177 before this unit, plus 6
  qualification cases).
- `python3 tools/check_layers.py`: OK, 56 modules, 7 layers.
- `python3 -m pytest tools/tests -q`: 11 passed, no skips.

## 6. Known limitations

- P2-1. The exit condition's comparison clauses are not met, as
  measured (Section 2). The closure loop's search strategy is
  the gap: whole-design candidate generation in name order,
  first-improvement acceptance, and two full STA runs per
  screened candidate, against upstream's endpoint-targeted
  incremental repair. A facade that reports worst endpoints
  would let a later version aim candidates at the violating
  cones first; the screening protocol would not change.
- P2-2. Acceptance after an edit runs at estimate fidelity only.
  Scenes at extracted parasitics are refused before any edit,
  because their SPEF describes the pre-edit netlist and EOS has
  no extraction engine to re-derive parasitics for an edited
  netlist. Incremental extraction is not qualified.
- P2-3. Placement is simplified in v1: an inserted buffer is
  placed at its driver instance's location, and a spliced cone's
  instances at the first cone instance's location. No placement
  legalization runs.
- P2-4. The timing gate covers the four metrics the timing
  service reports (setup and hold worst slack and TNS). The
  plan's gate also names max transition, capacitance and fanout
  violations; the STA facade exposes no such metrics in this
  version. Power and area are not in the v1 metric set, so the
  lexicographic order below timing has nothing to judge yet.
- P2-5. EOS's ODB pin (OpenROAD v2.0) cannot read the ORFS v3.0
  `6_final.odb` binary: the newer writer's format version is
  rejected by the v2.0 reader. The gcd evidence starts from the
  same database exported to DEF by the pinned OpenROAD. A newer
  ODB pin is a manifest decision for the owner; it has not been
  taken.
- P2-6. Resynthesis v1 scope: scalar Liberty timing only (a
  table-model cell in a cone is refused), one output signal pin
  per cone master, design names must be plain Verilog
  identifiers, and a cone master missing from the Liberty is
  refused. Buffering refuses nets driven by, or loading, a
  top-level port.
- P2-7. The imported OpenSTA module is GPL-3.0 and the imported
  ABC carries its own licence terms; both are recorded in their
  PROVENANCE.md files. The distribution and licence decision
  stays deferred, as set in Phase 0; the module boundaries are
  the containment.
- P2-8. Diagnosis is scene-level: the timing service reports
  per-scene metrics and the STA facade exposes no per-endpoint
  path detail, so the loop cannot name a violating endpoint (see
  P2-1). The closure loop's master-name family derivation in
  `eos::close_timing` (trailing drive number) is a disclosed
  heuristic; the service pin-checks every family it is given.

## 7. Exit assessment

The deliverable exists and behaves as specified: three repair
adapters under one ECO transaction protocol, judged on every
required scene, with function preservation proven where a repair
changes logic and a corrupted change demonstrably rolled back.
Those properties are proven by the qualification suite in CI.

The exit condition, however, is a comparison, and the comparison
was run and lost: on the identical degraded gcd, EOS improved
worst slack by 0.005 ns in 415.6 s where the pinned upstream
tool improved it by 0.107 ns in 0.074 s of repair time, and
neither tool closed the design. Declaring Phase 2 complete on
the fixture evidence alone would contradict the measured gcd
evidence, so this report does not declare it.

What happens next is the owner's decision: accept Phase 2 with
the comparison clauses recorded as not met, direct a further
unit of work on the search strategy (endpoint-targeted candidate
generation is the known gap, P2-1), or set a different exit
basis. Phase 3 gets its own breakdown and its own go; nothing in
this report starts it.
