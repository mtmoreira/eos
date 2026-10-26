# Closure A/B results: endpoint targeting and screening economics (unit 2.10)

Date: 2026-10-26
Status: the closure-competitiveness track re-ran the frozen
unit 2.5 A/B with units 2.8 and 2.9 in place. Neither success
bar is met, as measured below. Per the track queue, the track
stops here: no further loop tuning is started, and the
fallback decision (importing upstream's resizer under EOS's
transaction protocol) belongs to the project owner, as a
separate scope needing a separate go. Nothing in this report
starts it.

## 1. The comparison protocol (frozen from unit 2.5, unchanged)

The Phase 0 routed gcd with its clock period tightened from
3.4 ns to 2.9 ns in the SDC, so setup slack is negative; EOS
and the pinned upstream tool start from the identical
database, SDC and Liberty, both at estimate fidelity. The
common-evaluator cross-check runs in both directions: the EOS
TimingService evaluates upstream's output DEF, and the pinned
upstream STA evaluates EOS's output DEF. The pinned upstream
binary is a local build and cannot run in GitHub CI, so this
is a local evidence run (full logs kept with the unit 2.10
wake record), not a CI result. Acceptance semantics are
unchanged throughout: the Section 8 all-scene gate, the
Section 9 ECO protocol, equivalence proof for logic changes.

Starting point, measured independently by both evaluators in
this run:

| Evaluator | Worst slack (ns) | TNS (ns) | Worst hold slack (ns) |
|---|---|---|---|
| EOS TimingService | -0.183051796 | -0.412361034 | +0.456130744 |
| Pinned OpenROAD STA | -0.183051801 | -0.412361046 | +0.456130785 |

The evaluators agree to about 1e-8 ns, as they did in unit
2.5. Upstream reported 3 endpoints with setup violations.

## 2. Results

| Tool | Worst slack (ns) | TNS (ns) | Worst hold slack (ns) | Closed | Wall time | Candidates tried / rejected |
|---|---|---|---|---|---|---|
| Start (both evaluators) | -0.183052 | -0.412361 | +0.456131 | No | n/a | n/a |
| EOS, unit 2.5 (whole-design name order) | -0.177804 | -0.407090 | +0.456131 | No | 415.6 s | 51 / 41 |
| EOS, units 2.8 + 2.9 (this run, `eos::close_timing 10` through the shipped shell) | -0.183010 | -0.330864 | +0.456131 | No | 568.1 s | 142 / 132 |
| Pinned upstream `repair_timing -setup -hold -repair_tns 100 -max_passes 100` (fresh run) | -0.075767 | -0.135272 | +0.456124 | No | 0.176 s of repair; 2.85 s whole process | Resized 8 instances, swapped pins on 1 |

Improvements from the identical start:

| Tool | Worst slack gain (ns) | TNS gain (ns) |
|---|---|---|
| EOS, unit 2.5 | +0.005247 | +0.005271 |
| EOS, units 2.8 + 2.9 | +0.000042 | +0.081497 |
| Pinned upstream | +0.107285 | +0.277089 |

Cross-checks, both directions, from this run:

- EOS TimingService on upstream's written DEF: worst slack
  -0.075766504 ns, TNS -0.135271794 ns, hold +0.456124361 ns,
  matching upstream's own report to about 2e-9 ns on setup
  slack and TNS and 1.5e-8 ns on hold.
- Pinned upstream STA on EOS's written DEF (the DEF of a
  scratch rerun of the identical closure whose counts and
  final metrics match the shell run exactly; the shell itself
  exports no DEF): worst slack -0.183009835 ns, TNS
  -0.330864234 ns, hold +0.456130785 ns, matching EOS's own
  final report to about 5e-9 ns on setup slack, 9e-9 ns on
  TNS and 4e-8 ns on hold.

The evaluators agree in both directions. The difference below
is the search, not the analysis.

Wall time note, stated plainly: the identical EOS closure was
run three times in this track (the unit 2.9 progress run and
two runs in this unit, through the shell and through a scratch
driver calling the same closure entry point) and measured
769.9 s, 568.1 s and 404.4 s. The counts and final metrics
were identical every time; the wall varies with this VM's
state. The shell run's 568.1 s is the headline number in the
table. Every measurement is far over the 60 s bar (Section 3).

## 3. Verdict against the track bars

The bars, from the track queue, judged on this re-run:

- Primary bar, parity: EOS final worst slack and TNS at
  least upstream's. NOT met. EOS finished at worst slack
  -0.183010 ns against upstream's -0.075767 ns, and TNS
  -0.330864 ns against -0.135272 ns.
- Minimum useful outcome, below which the track stops and
  the resizer decision is escalated instead of more loop
  tuning: worst slack improvement of at least 0.05 ns AND
  total closure wall of at most 60 s. NOT met on either
  clause. The measured worst slack improvement is 0.000042
  ns, about one twelve-hundredth of the bar. The measured
  wall is 568.1 s, about 9.5 times the bar; the fastest of
  the three identical runs (404.4 s) is still 6.7 times over.

What units 2.8 and 2.9 did achieve, also as measured:
targeting worked as designed on the lesser endpoints (the TNS
gain is about 15 times the unit 2.5 gain), and the
known-before screening cut the per-screening cost from about
8.1 s to 4.00 s (142 screenings in 568.1 s). Neither moves
the bars, for the reasons in Section 4.

## 4. Profiling: where the remaining time and the missing gain go

Screening decomposition, measured in unit 2.9 on the
identical inputs and unchanged code paths: an ODB checkpoint
round trip costs about 0.10 s, a WriteVerilog about 0.018 s,
and one full TimingService analysis 5.0 to 5.5 s standalone.
One real sizing screening end to end cost 9.87 s before unit
2.9 and 3.95 s with the known-before supply, with an
identical verdict and identical numbers. In this run the
4.00 s per screening is essentially all the one remaining
full after-analysis: every screening still pays a fresh STA
session, including the sky130 Liberty parse, because EOS
times an edited design through a full re-analysis. Reducing
that further needs incremental STA, which the track scope
explicitly puts out of scope. Upstream pays none of it: it
searches in memory against its incremental timing graph, and
its whole repair took 0.176 s.

Two further measured facts explain the shape of the result:

- The stale-target skip and the rejection memory never fired
  on this design. A scratch rerun of the identical closure
  that prints the full outcome counters records
  candidates_skipped_stale = 0 and candidates_skipped_memory
  = 0. Both mechanisms are proven on the CI fixtures (unit
  2.9); on gcd they contributed nothing, and the whole
  per-screening saving above came from the known-before
  supply.
- Targeting raised the screening count from 51 to 142,
  because the fanin cones are large (the cone walk includes
  clock fanin), and it spent the run's 10 accepted iterations
  on the lesser endpoints. All 10 accepted steps were sizing
  steps; six of them upsized the same inverter through its
  drive family one step at a time. The lesser endpoints
  improved (the TNS gain), but at pin-capacitance fidelity a
  single sizing step on the worst endpoint's path gains
  almost nothing, so the worst slack moved 0.000042 ns in 10
  accepted iterations. The worst path is not starved of
  attention; the available edits at this fidelity barely
  move it, and each trial of an edit costs about 4 s.

## 5. Track conclusion

The track's question was whether endpoint-targeted candidate
generation plus screening economics would materially close
the gap to the pinned upstream repair on the frozen
protocol. The measured answer is no: the primary bar and
both clauses of the minimum useful outcome fail, and the
failure is structural rather than a defect the next loop
iteration would plausibly fix. The two remaining levers are
the ones the track scope excluded or reserved: incremental
timing inside the search (excluded as out of scope), and
importing upstream's resizer, which searches inside a
transaction against the dbSta bridge EOS deliberately did
not import, while EOS's own TimingService remains the
all-scene judge at commit (the fallback decision, reserved
to the owner as a separate scope). Per the queue, this
report is where the track stops: the result is published and
merged as an increment, no further loop work is started, and
the resizer decision is escalated to the owner with this
evidence attached.

## 6. Test counts and gates

Local gates run on 2026-10-26 on main at 4ce81cc (this unit
adds no code; the document is the only change):

- CMake configure and build: exit 0. One environment note,
  stated plainly: the driver VM was restarted mid-wake and
  lost the system spdlog and fmt libraries after the main
  verdict runs had completed; the scratch closure rerun and
  the evaluator cross-checks after the restart ran against
  the same libraries from the workspace apt prefix. CI on
  this pull request is the clean-build verdict.
- CTest: 23 of 23 suites passed, no skips.
- GoogleTest cases: 196 in total (unchanged from unit 2.9).
- `python3 tools/check_layers.py`: OK, 56 modules, 7 layers.
- `python3 -m pytest tools/tests -q`: 11 passed, no skips.
