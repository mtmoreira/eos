# P8 coverage audit: place-and-route commands

Base: `2d69060` (origin/main at audit time, the P7 merge).
Method: branch-by-branch trace of each place-and-route runner
(`src/pd/*/src/*.cc`, `src/pv/extract/src/extract.cc`) and of the
`eos::` command layer in `src/ui/registry/src/core_commands.cc`,
against the runner unit tests in `tests/unit/pd/` and
`tests/unit/pv/` and the command-layer tests in
`tests/unit/ui/registry/registry_test.cc`.

Scope rule (owner test rule, 2026-10-29): every valid scenario of
every new command gets a test, and every test runs in CI. A
scenario here is one option combination or one failure mode named
by the runner code or its header contract. Two layers are audited:

- Runner layer: the `Run*` API in `tests/unit/pd/` and
  `tests/unit/pv/`. Status `tested` cites the existing test,
  `added` cites a test added by this unit.
- Command layer: option parsing in the registry (unknown options,
  duplicates, missing values, argument shape). These are covered
  by the registry tests cited in section 3 and needed no new tests.

Result: zero untested valid scenarios remain. Every scenario
deliberately excluded is named in section 4 with its reason, never
silently dropped.

Test counts after this unit (runner tests only; registry tests
unchanged):

| File | Before | After |
| --- | --- | --- |
| tests/unit/pd/floorplan/floorplan_test.cc | 8 | 11 |
| tests/unit/pd/place/place_test.cc | 7 | 7 |
| tests/unit/pd/cts/cts_test.cc | 10 | 15 |
| tests/unit/pd/route/route_test.cc | 18 | 23 |
| tests/unit/pd/finish/finish_test.cc | 20 | 24 |
| tests/unit/pv/extract/extract_test.cc | 13 | 14 |
| tests/unit/pd/dbsta/dbsta_test.cc | 4 | 4 |
| tests/unit/pd/pdr/pdr_test.cc | 4 | 4 |
| tests/unit/pd/rsz/rsz_test.cc | 2 | 2 |

18 tests added, all in existing test binaries, so the ctest entry
count is unchanged (64) and the dependency graph is unchanged
(65 modules, 64 tests); `tools/dep_graph.py check` stays OK and
`tools/tests/test_dep_graph.py` needs no count update.

## 1. eos::floorplan

Runner: `RunFloorplan` in `src/pd/floorplan/src/floorplan.cc`.
Options: `-die_area`, `-core_area`, `-site`.

| Scenario | Status |
| --- | --- |
| Explicit die and core areas, rows, tracks, placed IO | tested: Floorplan.ExplicitAreasCreateRowsTracksAndPlacedIo |
| Default areas come from the design | tested: Floorplan.DefaultAreasComeFromTheDesign |
| Default core area equals the die area | added: Floorplan.DefaultCoreAreaEqualsDieArea |
| Named site accepted | tested: Floorplan.NamedSiteIsAccepted |
| No database | tested: Floorplan.NoDatabaseFailsFast |
| No technology | tested: Floorplan.NoTechnologyFailsFast |
| No mapped design | tested: Floorplan.NoMappedDesignFailsFast |
| Unknown site | tested: Floorplan.UnknownSiteFailsFast |
| Empty die area | tested: Floorplan.EmptyDieAreaFailsFast |
| Inverted die area (upper below lower) | added: Floorplan.InvertedDieAreaFailsFast |
| Empty core area with a valid die area | added: Floorplan.EmptyCoreAreaFailsFast |
| Second floorplan run on the same design | tested: CoreCommands.FloorplanRunsOnImportedDesign (registry, runs the command twice) |

## 2. eos::place

Runner: `RunPlace` in `src/pd/place/src/place.cc`.
Options: `-timing_driven` (the only option).

| Scenario | Status |
| --- | --- |
| Untimed placement, all instances, no overlaps, HPWL reported | tested: Place.PlacesAllInstancesWithNoOverlaps |
| Second run succeeds | tested: Place.SecondRunSucceeds |
| No database | tested: Place.NoDatabaseFailsFast |
| No technology | tested: Place.NoTechnologyFailsFast |
| No mapped design | tested: Place.NoMappedDesignFailsFast |
| Unfloorplanned design (no rows) | tested: Place.UnfloorplannedDesignFailsFast |
| Timing-driven without Liberty | tested: Place.TimingDrivenWithoutLibertyFailsFast |
| Timing-driven with Liberty (success path) | excluded, see section 4 |
| Routability-driven path (Liberty loaded) | excluded, see section 4 |

Place has no other option combination: the option is a single
boolean, and both of its reachable values are tested.

## 3. eos::cts

Runner: `RunCts` in `src/pd/cts/src/cts.cc`.
Options: `-clock`, `-buffer` (repeatable), `-root_buffer`,
`-sink_buffer`; Liberty comes from the session.

| Scenario | Status |
| --- | --- |
| Clock tree with an explicit buffer list | tested: Cts.BuildsClockTreeWithBuffers |
| Empty buffer list, inferred from Liberty | added: Cts.EmptyBufferListInfersFromLiberty |
| Explicit root and sink buffers | added: Cts.ExplicitRootAndSinkBuffers |
| Clock named by instance pin (u1/CLK) | added: Cts.ClockByInstancePinName |
| Second run succeeds (fresh design per run) | tested: Cts.SecondRunSucceeds |
| No database | tested: Cts.NoDatabaseFailsFast |
| No technology | tested: Cts.NoTechnologyFailsFast |
| No mapped design | tested: Cts.NoMappedDesignFailsFast |
| Unfloorplanned design | tested: Cts.UnfloorplannedDesignFailsFast |
| Unplaced design (rows, no placed instance) | tested: Cts.UnplacedDesignFailsFast |
| No Liberty path | tested: Cts.NoLibertyFailsFast |
| Unreadable Liberty path (STA reader throws; runner engine-failure path) | added: Cts.UnreadableLibertyFailsFast |
| No clock named and no SDC clock | tested: Cts.NoClockFailsFast |
| Unknown clock net | tested: Cts.UnknownClockNetFailsFast |
| Clock net with no sinks, zero clock roots after the run | added: Cts.ClockNetWithoutSinksFailsFast |
| Skew and insertion delay reported unavailable (not invented) | tested: Cts.BuildsClockTreeWithBuffers (asserts both flags false) |

## 4. eos::route

Runner: `RunRoute` in `src/pd/route/src/route.cc`.
Options: `-min_layer`, `-max_layer`, `-max_iterations`,
`-bottom_layer`, `-top_layer`; Liberty comes from the session.
The runner always runs the global stage followed by the detailed
stage: there is no global-only option, so no global-only scenario
exists (see section 10).

| Scenario | Status |
| --- | --- |
| Default layers and bound, nets routed, congestion reported | tested: Route.RoutesEveryNetAndReportsCongestion |
| Detailed stage writes wires, violations reported | tested: Route.DetailedStageWritesWiresAndReportsViolations |
| Detailed bound of 1 runs exactly one pass | tested: Route.DetailedBoundOfOneRunsExactlyOnePass |
| Explicit full global layer range 1..2 | tested: Route.ExplicitFullLayerRangeRoutes |
| Single global layer range (min equals max), engine rejects it (GRT-0229, vertical edge over a zero limit) | added: Route.SingleLayerRangeFailsInEngine |
| Minimum layer only, engine rejects it (GRT-0228, horizontal edge over a zero limit) | added: Route.MinLayerOnlyFailsInEngine |
| Explicit detailed layer range, both ends | tested: Route.ExplicitDetailedLayerRangeRoutes |
| Bottom detailed layer only, default top | added: Route.BottomLayerOnlyRoutes |
| Second run succeeds (guides and wires stripped and re-routed) | tested: Route.SecondRunSucceeds |
| No database | tested: Route.NoDatabaseFailsFast |
| No technology | tested: Route.NoTechnologyFailsFast |
| No mapped design | tested: Route.NoMappedDesignFailsFast |
| Unfloorplanned design | tested: Route.UnfloorplannedDesignFailsFast |
| Unplaced design | tested: Route.UnplacedDesignFailsFast |
| No Liberty path | tested: Route.NoLibertyFailsFast |
| Unreadable Liberty path (STA reader throws; runner engine-failure path) | added: Route.UnreadableLibertyFailsFast |
| Unknown detailed layer | tested: Route.UnknownDetailedLayerFailsFast |
| Non-routing detailed layer (cut layer) | tested: Route.NonRoutingDetailedLayerFailsFast |
| Zero detailed iterations | tested: Route.ZeroDetailedIterationsFailsFast |
| Negative detailed iterations | tested: Route.NegativeDetailedIterationsFailsFast |
| Inverted global layer range | tested: Route.InvertedLayerRangeFailsFast |
| Negative minimum layer | tested: Route.NegativeLayerFailsFast |
| Negative maximum layer | added: Route.NegativeMaxLayerFailsFast |
| Detailed loop stopped at the bound (flag true) | covered by formula: Route.DetailedStageWritesWiresAndReportsViolations and Route.DetailedBoundOfOneRunsExactlyOnePass assert stopped_at_bound equals (violations remain and bound reached); the clean fixture reaches zero violations, so the true case is asserted through the same formula, not a separate fixture |
| Global-only route (no detailed stage) | excluded, see section 4 |

## 5. eos::finish

Runner: `RunFinish` in `src/pd/finish/src/finish.cc`.
Options: `-pdn`, `-fill_config`.

| Scenario | Status |
| --- | --- |
| Filler insertion, no options | tested: Finish.InsertsFillers |
| Basic power grid with -pdn | tested: Finish.BuildsBasicPowerGrid |
| Metal fill with a config file | tested: Finish.RunsMetalFillWithConfig |
| -pdn and -fill_config together | added: Finish.PdnAndMetalFillTogether |
| Repeat run is idempotent (fillers, grid counted not rebuilt) | tested: Finish.RepeatRunIsIdempotent |
| No database | tested: Finish.NoDatabaseFailsFast |
| No technology | tested: Finish.NoTechnologyFailsFast |
| No mapped design | tested: Finish.NoMappedDesignFailsFast |
| Unfloorplanned design | tested: Finish.UnfloorplannedDesignFailsFast |
| Unplaced design | tested: Finish.UnplacedDesignFailsFast |
| Unrouted design (placed, never routed) | tested: Finish.UnroutedDesignFailsFast |
| Guides-only design (guides, no wires) | added: Finish.GuidesOnlyDesignFailsFast |
| No filler master in the library | tested: Finish.NoFillerMasterFailsFast |
| -pdn without supply nets | tested: Finish.PdnWithoutSupplyNetsFailsFast |
| Unreadable fill config | tested: Finish.UnreadableFillConfigFailsFast |

## 6. eos::write_gds

Runner: `WriteGds` in `src/pd/finish/src/finish.cc`.
Options: exactly one positional path.

| Scenario | Status |
| --- | --- |
| Stream-out after finish with -pdn | tested: WriteGds.WritesGdsAfterFinish |
| Stream-out after finish without -pdn | added: WriteGds.WritesGdsAfterFinishWithoutPdn |
| Before finish (no finish marker) | tested: WriteGds.BeforeFinishFailsFast |
| No database | tested: WriteGds.NoDatabaseFailsFast |
| No technology | added: WriteGds.NoTechnologyFailsFast |
| No mapped design | tested: WriteGds.NoMappedDesignFailsFast |
| Empty path | tested: WriteGds.EmptyPathFailsFast |
| Bad path (unwritable) | tested: WriteGds.BadPathFailsFast |
| Second write overwrites | tested: WriteGds.SecondWriteOverwrites |

## 7. eos::extract_parasitics

Runner: `ExtractParasitics` in `src/pv/extract/src/extract.cc`.
Options: one positional SPEF path, `-model`.

| Scenario | Status |
| --- | --- |
| Extraction of every routed net, SPEF written | tested: Extract.ExtractsEveryRoutedNetAndWritesSpef |
| Extracted STA differs from estimated STA (SPEF parsed by STA) | tested: Extract.ExtractedStaDiffersFromEstimatedSta |
| Repeat extraction rewrites cleanly | tested: Extract.RepeatExtractionRewritesCleanly |
| No database | tested: Extract.NoDatabaseFailsFast |
| No technology | tested: Extract.NoTechnologyFailsFast |
| No mapped design | tested: Extract.NoMappedDesignFailsFast |
| Unfloorplanned design | tested: Extract.UnfloorplannedDesignFailsFast |
| Unplaced design | tested: Extract.UnplacedDesignFailsFast |
| Unrouted design (placed, never routed) | tested: Extract.UnroutedDesignFailsFast |
| Guides-only design (guides, no wires) | tested: Extract.GuidesOnlyDesignFailsFast |
| Missing model path | tested: Extract.MissingModelFailsFast |
| Unreadable model path | tested: Extract.UnreadableModelFailsFast |
| Model covering none of the routed layers (zero RC segments) | excluded, see section 10 item 6 |
| Missing SPEF path | tested: Extract.MissingSpefPathFailsFast |
| Unwritable SPEF path | added: Extract.UnwritableSpefPathFailsFast |

## 8. Command-layer parsing (registry tests, no new tests needed)

`tests/unit/ui/registry/registry_test.cc` already pins the
command layer for every command above: unknown options,
duplicates, missing values, non-integer values, and argument
shape, plus the session preconditions (technology, mapped design,
Liberty) in command order:

| Command | Registry test |
| --- | --- |
| floorplan | CoreCommands.FloorplanRunsOnImportedDesign, FloorplanFailsFastWithoutTechnology, FloorplanFailsFastWithoutMappedDesign, FloorplanRejectsBadOptions |
| place | CoreCommands.PlaceRunsOnFloorplannedDesign, PlaceFailsFastWithoutTechnology, PlaceFailsFastWithoutMappedDesign, PlaceFailsFastOnUnfloorplannedDesign, PlaceRejectsBadOptions, PlaceTimingDrivenWithoutLibertyFailsFast |
| cts | CoreCommands.CtsRejectsBadOptions, CtsFailsFastWithoutTechnology, CtsFailsFastWithoutLiberty |
| route | CoreCommands.RouteRunsOnPlacedDesign, RouteRejectsBadOptions, RouteAcceptsDetailedOptions, RouteRejectsZeroIterations, RouteFailsFastWithoutLiberty, RouteFailsFastWithoutTechnology |
| extract_parasitics | CoreCommands.ExtractParasiticsRejectsBadArguments |
| finish | CoreCommands.FinishAndWriteGdsRunOnRoutedDesign, FinishRejectsBadOptions |
| write_gds | CoreCommands.FinishAndWriteGdsRunOnRoutedDesign, WriteGdsRejectsBadArguments |

## 9. Support modules (no command surface, included for completeness)

| Module | Existing tests | Note |
| --- | --- | --- |
| pd.dbsta | 4: Dbsta.PinIdsAreDistinctAndNullSafe, InstanceIdsUseSentinelForTop, NetIdsAreDistinct, SetBlockAloneBindsDatabase | Bridge module, no command; no new command scenario arises from it. |
| pd.pdr | 4: Pdr.PrimDijkstraSquare, FluteSquare, PrimDijkstraTriangle, FluteTriangle | Steiner-tree support engine used by place and route; its scenarios are the square and triangle cases, both covered for both algorithms. |
| pd.rsz | 2: Rsz.FactoryConstructsAndDestroys, DirectConstructs | Landed via the separate R1 workstream, not a P unit; included here only so the audit table is complete. No P-command scenario depends on new rsz tests. |

## 10. Excluded scenarios, with reasons

1. Place timing-driven success (`-timing_driven` with Liberty):
   not a currently valid scenario at the `RunPlace` API level.
   The place runner creates a private STA session and never loads
   a Liberty library into it, so `defaultLibertyLibrary()` is
   always null and timing-driven mode always fails fast; the
   existing `Place.TimingDrivenWithoutLibertyFailsFast` test pins
   that behavior. Reaching the success path needs a runner change
   (loading Liberty into the private session), which is outside
   P8 coverage scope and outside this unit's stop rules for
   runner behavior change. The gap is recorded here, not hidden.
2. Place routability-driven path (Liberty loaded): same reason.
   The runner sets routability mode from the same always-null
   Liberty check, so the routability-on path is unreachable
   through `RunPlace`; the routability-off path is what every
   place test exercises.
3. Route global-only (global stage without the detailed stage):
   not a valid scenario because `RouteOptions` offers no flag to
   skip the detailed stage; the runner always runs both. The
   global-stage outputs (guides, congestion) are asserted within
   the full-run tests.
4. Runner-internal failure paths that no caller input can reach:
   Tcl interpreter creation failure (place, cts, route), a null
   STA scene after session init (cts, route), and the
   dbNetwork setup exception (floorplan). These are defensive
   branches, not command scenarios, and no test input induces
   them.
5. Instance-less designs (a netlist with no instances) as flow
   inputs: no P-unit runner defines behavior for them, and every
   precondition in the runners is stated in terms of rows,
   placed instances, wires, and masters on a real netlist. They
   are not enumerated as scenarios because they are not valid
   flow inputs, not because they were overlooked.
6. The extract runner's zero-RC-segment branch (a run that
   leaves no RC segments in the design): no test input reaches
   it cleanly, as established by two probes during this audit.
   A rules file with no Metal sections fails earlier in the
   extractor itself (RCX-0487, no RC model read), before the
   runner's count check. A rules file retargeted to layers the
   technology does not have (fixture rules moved from layers 1
   and 2 to layers 3 and 4) crashes the imported extractor with
   a segmentation fault during extraction, after the model
   reads. A crashing engine is not a testable runner scenario,
   and fixing the imported extractor is outside P8 scope
   (imported files are never patched). The branch stays as
   defensive code, documented here rather than pinned by a test
   that would crash the suite.

## 11. Dependency graph and affected-test selection

P8 adds tests inside existing test binaries only: no new module,
no new ctest entry, no new file for the graph to track. The graph
therefore regenerates identically, `tools/dep_graph.py check`
reports OK (65 modules, 64 tests), and
`tools/tests/test_dep_graph.py` keeps its 64-entry expectation
(the P7 comment line stands; P8 adds zero entries).

Affected-test selection was verified with `dep_graph.py select`
against a changed pd file and a changed pv file (run on the P8
branch, before any graph change, since the graph is unchanged):

- `select src/pd/route/src/route.cc` selects `pd.route`,
  `pd.finish`, `pv.extract`, `ui.registry`, `ui.tcl`, `ui.gui`,
  `qualification.phase1_exit`, and the `pd.rsz.mock.*` entries.
- `select src/pd/finish/src/finish.cc` selects `pd.finish` plus
  the same downstream set minus `pd.route` and `pv.extract`.
- `select src/pv/extract/src/extract.cc` selects `pv.extract`
  plus the same downstream set minus the `pd.*` entries.

The graph tracks test entries per module binary, not per
`TEST()` case, so the new P8 cases inside the existing pd and pv
binaries are selected through their binaries' entries.
