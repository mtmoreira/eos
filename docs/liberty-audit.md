# Liberty audit (Immediate unit 2, review S10/SI7/U6)

Base: `c5fa125c` (origin/main at audit time). Method: code trace from
the registry command through the timing service to the parser, plus
the test tree and docs. Statuses below come from named reader and
parser call sites, or from an exhaustive grep proving absence. No live
execution was used; anything not established from code is listed under
Unresolved, not guessed.

Scope note: this is an audit, not a fix. It publishes what
`read_liberty` parses, uses, ignores, and rejects today, names every
gap found, and states what later units (power reporting, noise and
glitch analysis, library round-trip) must decide. It does not change
parser behavior.

Status vocabulary used below: read-and-used means parsed and feeding
an EOS-visible metric; read-but-ignored means parsed and stored but
no EOS-visible metric consumes it; rejected means a hard error;
silently-ignored means never looked up, dropped without any message.

## 1. Headline: `eos::read_liberty` parses nothing

`eos::read_liberty` itself parses nothing. It only records a path.
Real Liberty parsing happens later, inside the imported (unmodified)
OpenSTA reader, when a timing scene is built. So "what EOS parses"
equals "what upstream OpenSTA's `LibertyReader` parses, minus nothing,
plus no EOS filtering", except that EOS never invokes the upstream
power, noise, and Tcl reporting surface, so several parsed constructs
are dead from EOS's point of view.

Code path:

1. Registry command, records only, does not parse.
   `src/ui/registry/src/core_commands.cc:1806-1817`:
   `add("read_liberty", "read_liberty <corner> <path>", ...)` checks
   arg count and readability, then stores
   `ctx.timing.liberty_by_corner[args[0]] = args[1]` (storage
   declared at `src/ui/registry/include/eos/ui/registry.h:112`). No
   parse, no validation of Liberty syntax, no error possible here
   except an unreadable file.
2. Scene building hands the stored path to STA.
   `src/analysis/timing/src/timing_service.cc:82` (`AnalyzeScene`)
   and `:163` (`PrepareEngine`): `engine.ReadLiberty(scene.liberty_path)`.
   The per-corner path comes from `core_commands.cc:755-764` and
   `:3607-3616` (scene build in `run_timing` and `report_timing`).
   The synth-opt loop uses one resident engine the same way
   (`core_commands.cc:2727`, `sta.ReadLiberty(ctx.synth.mapped_liberty)`).
3. EOS facade, thin Tcl wrapper, no filtering.
   `src/analysis/timing/sta/src/sta_facade.cc:503-505`:
   `StaEngine::ReadLiberty` runs `read_liberty <path>` through Tcl.
   There is no EOS-owned allowlist, denylist, pre-filter, or
   post-filter on Liberty content. The only EOS-side Liberty-adjacent
   logic is path bookkeeping in the registry.
4. Upstream Tcl command. `src/analysis/timing/sta/src/liberty/
   Liberty.tcl:29-84`: `read_liberty` accepts `-corner`, `-min`,
   `-max`, `-infer_latches`, and a filename. Note: the EOS facade
   calls bare `read_liberty <path>`, with no `-corner`, no
   `-min`/`-max`, and critically no `-infer_latches`, so latch
   inference is off; `ff` and `latch` groups are required for
   sequential cells.
5. SWIG bridge to C++ `Sta`. `src/analysis/timing/sta/src/liberty/
   Liberty.i:119-127` maps `read_liberty_cmd` to
   `sta->readLiberty(...)`. `src/analysis/timing/sta/src/search/
   Sta.cc:718-748` loads the file, and if it is the first library,
   makes it the default library and copies its units.
6. The actual parser. `src/analysis/timing/sta/src/liberty/
   LibertyReader.cc:76-101` and `LibertyParser.cc:126ff`. The grammar
   (`LibertyParse.yy`) is fully generic: any `KEYWORD (...) { ... }`
   group and any `name : value;` or `name (...) ;` attribute is
   accepted syntactically and stored in a `LibertyGroup` tree.
   Semantics live entirely in `LibertyReader`, which walks the tree
   with explicit lookups. The same entry point also accepts gzip (via
   `gzstream`) and OpenROAD `.blib` binary files by magic number
   (`LibertyParser.cc:99-125`), so EOS `read_liberty` transitively
   accepts `.blib`, though no EOS doc mentions it.
7. Provenance: zero EOS modifications.
   `src/analysis/timing/sta/PROVENANCE.md` pins OpenSTA commit
   `0f2529d18218b87aa0e82de444395a830d5058a1`, "Patch count: 0. No
   upstream source file is modified." The separate `src/io/liberty/`
   module is a skeleton (`src/io/liberty/src/liberty.cc`: "skeleton
   translation unit"). It parses nothing and is not on this path.

Separate, unrelated Liberty consumer (do not confuse with
`read_liberty`): `synth_map -liberty` does not use OpenSTA.
`src/synth/abc/src/abc_facade.cc:462ff`
(`AbcEngine::LibertyToGenlib`) is an EOS-owned brace-matching text
scrape that reads only `cell` blocks, `area`, `ff(`/`latch(`
presence, pin `direction`, pin `capacitance`, pin `function`, and
scalar `cell_rise`/`cell_fall` values. It emits a genlib gate only
for combinational, single-output cells with a function and a scalar
(non-table) delay, erroring `kUnsupported` ("no Liberty cell
qualifies for genlib mapping...") otherwise. Table-based cells are
silently skipped there. This path is out of scope for the
`read_liberty` question, but any audit sentence about "EOS reads
Liberty for mapping" must name which parser it means.

## 2. Asked constructs: per-construct status

| Construct | Status | Evidence |
|---|---|---|
| `pg_pin` group | Read-but-ignored for EOS metrics; rejected only for unknown `pg_type` | `LibertyReader.cc:1066-1084` dispatches `pg_pin` to `makePgPinPort` but does not add it to `port_group_map`, so it gets no capacitance, timing, or power processing. `makePgPinPort` (`:1211-1256`) reads `pg_type` to direction power, ground, or well, and `voltage_name`. Vocabulary at `Liberty.cc:2012-2023`: `primary_power`, `primary_ground`, `backup_power`, `backup_ground`, `internal_power`, `internal_ground`, `nwell`, `pwell`, `deepnwell`, `deeppwell`, plus `node` to `none`. Unknown `pg_type` errors 1291 (`:1240`), which throws (section 4). Missing name warns 1314 only. Downstream: the port object exists; no timing arc is built from it; its only consumer is as the `related_pg_pin` pointer on power objects. |
| `internal_power` group (inside `pin`) | Read-but-ignored for EOS metrics; parsed and stored, consumed only by upstream power code EOS never calls | `LibertyReader.cc:2771-2821` (`readInternalPowerGroups`): reads `related_pin`, `related_pg_pin`, `when`, and `rise_power`/`fall_power`/`power` sub-tables via `power_lut_template` with energy scaling. Stored via `LibertyCell::makeInternalPower` (`Liberty.cc:1211-1215`). Sole consumer found: `src/analysis/timing/sta/src/power/Power.cc:1113-1115, 1137ff, 1303ff`. EOS has no power method (section 5). |
| `leakage_power` group (inside `cell`) | Read-but-ignored for EOS metrics; same caveat | `LibertyReader.cc:2753-2769` (`readLeakageGrouops`, sic): reads `when` plus `value` times `power_scale_` (from `leakage_power_unit`, `:345`), plus `related_pg_pin`; stored via `makeLeakagePower` (`Liberty.cc:1230-1234`). Missing `value` warns 1307 only. Sole consumer: `Power.cc:1497-1552`. |
| `cell_leakage_power` attribute (cell) | Read-but-ignored for EOS metrics | `LibertyReader.cc:1826-1828` (with `power_scale_`). Sole consumer: `Power.cc:1552`. |
| `related_pg_pin` attribute (in `internal_power` and `leakage_power`) | Read, stored, EOS-ignored; dangling reference warns only | `LibertyReader.cc:2762` (leakage) and `:2778` (internal power) via `findLibertyPort` (`:2835-2852`): unknown port name warns 1290, stores `nullptr`, no error. |
| Timing groups: cell timing arcs | Read-and-used | `LibertyReader.cc:1928-1972` (`makeTimingArcs`): `related_pin`, `related_bus_pins`, `related_output_pin`. Missing `related_pin` warns 1243, except for `min_pulse_width`, `minimum_period`, `min_clock_tree_path`, and `max_clock_tree_path` (`:1962-1965`). Arcs are consumed by the delay calculation into slack and TNS metrics (section 5). |
| Timing sense (`timing_sense`) | Read-and-used; unknown value warns, not errors | `LibertyReader.cc:2000-2017`: `non_unate`, `positive_unate`, `negative_unate`; anything else warns 1245, value left unset. |
| Timing type (`timing_type`) | Read-and-used; unknown value warns plus fallback to `combinational` | `LibertyReader.cc:2018-2034`. Full vocabulary in `TimingArc.cc:773-820`: `clear`, `combinational` (plus rise and fall variants), `rising_edge`, `falling_edge`, `setup_rising` and `falling`, `hold_rising` and `falling`, `non_seq_setup` and `hold` variants, `nochange` variants, `recovery` and `removal` variants, `skew` variants, `preset`, `three_state_enable` and `disable` (plus rise and fall variants), `min_pulse_width`, `minimum_period`, `retaining_time`, `min` and `max_clock_tree_path`, `unknown`. Unknown warns 1244, type forced to `combinational`. |
| Delay tables (`cell_rise`/`cell_fall`, `rise`/`fall_transition`) | Read-and-used (NLDM) | `LibertyReader.cc:2148-2228` (`makeTableModels`, only when `delay_model` is table): each looked up as a sub-group of `timing`, each requiring a `lu_table_template` name as first parameter plus `index_1`/`2`/`3` and `values` (`readTableModel`, `:2543-2610`). Missing transition or delay for a gate type warns 1210/1211 but the arc is still created. |
| Constraint tables (`rise`/`fall_constraint`) | Read-and-used (setup, hold, and similar checks) | Same `makeTableModels` loop (`:2200ff`): `rise`/`fall_constraint` become check table models with the timing type's scale-factor type. |
| Retain tables (`retaining_rise`/`fall`, `retain_rise`/`fall_slew`) | Read-and-used (as retain models) | `LibertyReader.cc:2229-2280` (`makeRetainTableModels`). |
| Delay and power table interpolation | Read-and-used: linear (bi- and tri-linear) interpolation; scale factors applied | `TableModel.cc:1079-1185` (linear interpolation between bracketing axis points); `TableModel.cc:833-856` (multiplies by `scaleFactor(cell, pvt)` unless the table is a scaled-cell table). Out-of-range behavior is unresolved (see Unresolved). |
| LVF and OCV tables in timing (`ocv_sigma_*`, `ocv_std_dev_*`, `ocv_mean_shift_*`, `ocv_skewness_*`, including `early` and `late` via `sigma_type`) | Read-and-used (stored on the table models; consumed by the POCV delay path) | `LibertyReader.cc:2314-2387` (`readLvfModels`, `readEarlyLateTableModels`). |
| `receiver_capacitance`, `receiver_capacitance1`/`2` | Read-and-used (CCS receiver model) | `LibertyReader.cc:2388-2431`. |
| CCS `output_current_rise`/`fall` plus `vector` plus `reference_time` | Read-and-used (parsed into output waveforms) | `LibertyReader.cc:2432-2542` (`readOutputWaveforms`, `makeOutputWaveforms`); template type `output_current` (`readTableTemplates`, `:549-557`). Whether EOS's delay calculation actually selects the CCS path for a given library was not traced (see Unresolved). |
| `when`, `sdf_cond`, `sdf_cond_start`/`end`, `mode` (in `timing`) | Read-and-used (conditions and modes on arcs) | `LibertyReader.cc:2035-2065` (`readTimingWhen`), `:2066-2091` (`readTimingMode`, requires exactly 2 values else warns 1246/1248/1249). |
| `ocv_arc_depth` (library, cell, timing) | Read (stored); EOS-metric effect not verified | `LibertyReader.cc:230-232` (library), `:1841-1842` (cell), `:1982-1983` (timing). |
| Linear delay model (`intrinsic_rise`/`fall`, `rise`/`fall_resistance`, library defaults) | Read-and-used (only when `delay_model` is `generic_cmos`) | `LibertyReader.cc:2112-2148` (`makeLinearModels`), dispatched by `makeTimingModels` (`:2092-2111`). |
| Non-table, non-linear delay models (`piecewise_cmos`, `cmos2`, `polynomial`, `dcm`) | Parsed-as-type but rejected-for-delay: warning, no model built | `readDelayModel` (`:445-475`) records the type and warns 1160/1161/1162/1163 ("... not supported."). Then `makeTimingModels` (`:2092-2111`) breaks for those four types and builds no delay model. Not a hard error: arcs exist without models. Unknown `delay_model` string warns 1164, type stays at the `makeLibrary` default `cmos_linear` (`:310`). |
| Noise models: all noise groups | Silently-ignored (no group is ever looked up) | Exhaustive grep over `src/analysis/timing/sta/src/liberty/` and `include/sta/`: zero occurrences of any noise group name (`noise_immunity`, `noise_margin`, `output_noise`, `propagated_noise`, `steady_state_current`, `dc_current`, `iv_curve`, `em_*`, `antenna`). The only noise tokens in the entire STA tree are the template-axis names `input_noise_width` and `input_noise_height` (`TableModel.cc:1724-1725`, enum `LibertyClass.hh:134-135`, unit mapping `:1760-1765`): a template may name such a variable without triggering the "axis type ... not supported" warning, but no reader code ever attaches a noise table to a cell, port, or arc, and gate and check axis validation would reject noise axes for delay tables (warn 1251, "unsupported model axis"). There is no noise analysis reachable from EOS at all. |

## 3. Broader Liberty surface

Library-level, read (complete list from `readLibraryAttributes`,
`LibertyReader.cc:211-273`, plus helpers). Units: `time_unit`,
`pulling_resistance_unit`, `voltage_unit`, `current_unit`,
`leakage_power_unit`, `distance_unit`, `capacitive_load_unit`
(`:336-382`, ff and pf only, else warns 1154-1158). `technology`
(`:322-335`): only `fpga` acts (forces `cmos_linear`); any other
value (for example `cmos`) is silently ignored. `delay_model`
(section 2). `bus_naming_style` (`:476-492`, must match a
`%s[%d]`-shape else warn 1165). Thresholds
`input`/`output_threshold_pct_rise`/`fall`,
`slew_lower`/`upper_threshold_pct_rise`/`fall` (`:518-533`); missing
ones warn at library end (`checkThresholds`, `:534-548`, warns
1145-1148). Table templates `lu_table_template`,
`output_current_template`, `power_lut_template`, `ocv_table_template`
(`:549-583`, each with `variable_1`/`2`/`3` plus optional
`index_1`/`2`/`3`; unknown variable warns 1297). `voltage_map`
(`:633-649`). `operating_conditions` plus
`default_operating_conditions` (`:650-689`: `process`,
`temperature`, `voltage`, `tree_type`). Scale factors: top-level
plus named `scaling_factors` groups,
`k_<pvt>_<type>[_<rise|fall|high|low>]` (`:690-745`). `wire_load`
(`resistance`, `capacitance`, `slope`, `fanout_length`),
`wire_load_selection` plus `wire_load_from_area`,
`default_wire_load[_mode|_selection]` (`:746-872`). OCV derate:
`ocv_derate` groups plus `ocv_derate_factors` (`rf_type`,
`derate_type`, `path_type`, template; `LibertyReader.cc:3513ff`,
`readCellOcvDerateGroup` `:3000-3017`, `default_ocv_derate_group`).
`normalized_driver_waveform` (`:2926-2965`, validates axis
variables, else warns 1225/1265/1256/1227).
`rise`/`fall_transition_degradation` tables (`:905-925`). Scalar
library attrs: `nom_process`, `nom_temperature`, `nom_voltage`,
`default_inout`/`input`/`output_pin_cap`, `default_max_transition`,
`default_max_fanout`, `default_intrinsic_rise`/`fall`,
`default_inout`/`output_pin_rise`/`fall_res`, `default_fanout_load`,
`slew_derate_from_library` (`:235-271`; zero-valued
`default_max_transition`, `default_max_fanout`, and
`default_fanout_load` warn 1171/1172 but are still stored).
`slew_derate_from_library` is genuinely consumed downstream
(`DelayCalcBase.cc:99, 129-130`, `DmpCeff.cc:134`,
`ArnoldiDelayCalc.cc:373, 447`); thresholds likewise
(`DelayCalcBase.cc:96-98, 117-120`, `CcsCeffDelayCalc.cc:116-118`).
Library-level silently-ignored (never looked up): everything else,
including `revision`, `comment`, `date`,
`default_cell_leakage_power`, `default_leakage_power_density`,
`default_arc_mode`, `default_constraint_arc_mode`, all present in
the repo's sky130 fixture (section 6) and all dropped without a
message. `define(...)` statements are parsed into the group tree
(`LibertyParser.cc:156-197`) but no `LibertyReader` code consumes
them (see Unresolved).

Cell-level, read (`readCellAttributes`, `:1813-1860`): `area`,
`cell_footprint`, `dont_use`, `is_macro_cell`, `is_pad`/`pad_cell`,
`is_level_shifter` (plus `level_shifter_type` HL/LH/HL_LH,
`:2966-2984`, else warn 1228), `is_clock_cell`,
`is_isolation_cell`, `always_on`, `interface_timing`,
`cell_leakage_power` (section 2), `is_memory` (or presence of a
`memory` subgroup), `user_function_class`,
`clock_gating_integrated_cell` (`latch_posedge`/`latch_negedge`/
`other`), `scaling_factors` (reference), `ocv_derate_group`, plus
`leakage_power` groups, `statetable`, `mode_definition`/`mode_value`
groups (`readModeDefs`, `:873-905`), `test_cell` (recursive
`readCell`, `:3097-3117`). Cell-level silently-ignored: for example
`dont_touch` (zero hits in `LibertyReader.cc`); `area` is stored and
only surfaces via upstream property and area aggregation
(`search/MakeTimingModel.cc:192`, `search/Property.cc:740-742`); no
EOS metric uses it; `dont_use`, `is_macro_cell`, and similar are
likewise stored, not enforced by any EOS command (no EOS command
refuses a `dont_use` cell).

Pin-level, read (`readPortAttributes` `:1260-1300` plus
`readPortDir` `:1480-1505` plus `readCapacitance` `:1507-1579` plus
`readMinPulseWidth` `:1594-1627`): `direction` (`input`/`output`/
`inout`/`internal`; unknown warns 1240); `capacitance`,
`rise`/`fall_capacitance`, `rise`/`fall_capacitance_range`,
`min`/`max_capacitance`, `min`/`max_transition` (note: plain
`max_transition` is read as the max member of this pair; zero
`max_transition` warns 1241); `fanout_load`, `max`/`min_fanout`,
`min_period`, `clock` (bool), `clock_gate_clock`/`enable`/`out_pin`,
`is_pll_feedback_pin`, `isolation_cell_data`/`enable_pin`,
`level_shifter_data_pin`, `switch_pin`, `related_ground_pin`,
`related_power_pin`, `driver_waveform_rise`/`fall` (by name lookup,
silently dropped if the named waveform is absent), `pulse_clock`
(four `*_triggered_*_pulse` values, else warn 1242),
`min_pulse_width_high`/`low` (note: there is no lookup of a bare
`min_pulse_width` pin attribute, only the high/low pair, and each
creates a `min_pulse_width` check arc via `makeScalarCheckModel`),
`function`, `three_state`. `signal_type` is read only inside
`test_cell` (`readSignalType`, `:1443-1479`; `test_scan_*` values,
unknown warns 1299) and ignored on ordinary pins. Pin-level
silently-ignored: everything not listed, for example `prefer`,
`x_function`-style extras, `em_*`, `antenna*` (zero hits in
`LibertyReader.cc`).

Function, three-state, bus, and sequential constructs, read-and-used
(for STA structure, not as EOS metrics). `function` parsed by the
expression reader (`makePortFuncs`, `:1628-1669`, `parseFunc`
`:3478ff`); `three_state` parsed, inverted to `tristate_enable`,
and output pins re-typed to tristate (`:1647-1669`). Bus:
library and cell `type` groups with `bit_from`/`bit_to`
(`:493-518`, missing warns 1179/1180), `bus`/`bundle` groups with
`members`, pin groups inside bus and bundle groups, bus-bit name
expansion (`makeCellPorts` `:1066-1084`, `makeBusPinPorts`
`:1130-1156`, `makeBundlePinPorts` `:1190-1209`). Sequential: `ff`,
`ff_bank` (`clocked_on`, `next_state`), `latch`, `latch_bank`
(`enable`, `data_in`), each with `clear`, `preset`,
`clear_preset_var1`/`2` (`makeSequentials`, `:1670-1743`); `lut`
groups only get their output ports created (`:1670-1687`); no LUT
function parsing was found. `statetable` (`table` rows,
`:3017-3096`, malformed rows warn 1300-1303). `scaled_cell`
(`endScaledCell` `:200-209`, `readScaledCell` `:1006-1039`;
unknown operating condition warns 1202-1205, port, function, and
timing mismatch warns 1206-1208).

## 4. Unrecognized group or attribute: hard error, warning, or silent ignore?

Default: silent ignore. The generic parser stores every group and
attribute (`LibertyParser.cc:213-290`); `LibertyReader::visitAttr`
is an empty no-op for both simple and complex attributes
(`LibertyReader.cc:127-135`); group begin and end dispatch exists
only for `library`, `cell`, and `scaled_cell` (`defineVisitors`,
`:118-124`); `makeCellPorts` handles only `pin`, `bus`, `bundle`,
and `pg_pin` (`:1072-1080`), and every other cell subgroup is
reached only if some `findSubgroup(s)` call names it. Unrecognized
content is therefore parsed, never complained about, and freed when
its parent group is cleared or deleted (`endCell` `:175-197` does
`library_group->clear()`; `endLibrary` `:162-172` deletes the
group). There is no "unknown attribute" warning anywhere in
`LibertyReader`.

Warnings (non-fatal, message only, load continues). Upstream `warn()`
prints `Warning <id>: <file> line <n>, <msg>` and does not throw.
Examples: unknown `timing_sense` (1245), unknown `timing_type` (1244,
plus fallback), unknown `delay_model` (1164), unsupported
`delay_model` (1160-1163), unknown `bus_naming_style` (1165),
missing thresholds (1145-1148), missing table template (1253),
missing table `values` (1257), unsupported table axes (1251), no
table models in a timing group (1311), missing `related_pin` (1243),
missing cell name (1193), dangling `related_pg_pin` and `related_pin`
references (1290/1306), zero `max_transition` and `default_*`
(1241/1171/1172), `mode` arity (1246/1248/1249), non-increasing table
index (1173/1178).

Hard errors (fatal to the load). Upstream `error()` throws, which
the EOS facade catches and returns as `kIoError`. Cases found:
syntax and grammar errors (`LibertyParse.yy` error handler, id 164);
`library missing name` (1141); unknown `pg_type` (1291, section 2);
unknown `rf_type` in `ocv_derate_factors` (1286). Missing or
unreadable file throws `FileNotReadable` (`LibertyParser.cc:126ff`),
also surfaced as `kIoError`. Two soft spots: an `include_file` that
cannot be opened is only warning 25 (`LibertyParser.cc:360-367`),
and a nested `include_file` is a scanner error (`:340`); note
include paths are opened as given, not resolved relative to the
parent Liberty file (see Unresolved).

Net effect for the audit: a Liberty file can load "successfully" in
EOS while silently discarding whole feature areas (noise, power from
EOS's view, `define`s, `revision`-class metadata). Success of
`read_liberty` or `run_timing` is not evidence those constructs were
honored.

## 5. What EOS does with the parsed Liberty downstream today

Timing, the only EOS-visible consumer.
`TimingService::Analyze` (`timing_service.cc:79-147`) per scene:
`ReadLiberty`, `ReadVerilog`, `LinkDesign`, `ReadSdc`, optional
`ReadSpef`, then queries for worst slack, total negative slack,
worst hold slack, total negative hold slack, plus the three
electrical checks (`MaxTransitionCheck`, `MaxCapacitanceCheck`,
`MaxFanoutCheck`, backed by `Sta::maxSlewCheck`
`search/Sta.cc:5804`, `maxCapacitanceCheck` `:6012`, and the fanout
check via `search/CheckFanouts.cc:125, 152`, including library
`default_max_fanout` and `default_max_slew` fallbacks in
`CheckSlews.cc:309, 354`), plus violating endpoints and worst paths.
These are the metrics `run_timing`, `report_timing`, and
`synth_map` judging and the synth-opt loop consume. Delay values
come from the NLDM and linear tables (section 2) through the delay
calculation; pin capacitances feed load and slew;
`min_pulse_width_high`/`low` and `min_period` feed check arcs;
thresholds and `slew_derate_from_library` feed the delay
calculation (section 3).

Power: parsed, stored, never computed by EOS. There is no
`report_power` command and no power method on `StaEngine` (`sta.h`
exposes only slack, TNS, endpoint, path, and electrical-check
methods; grep of `sta_facade.cc` for power returns nothing).
Upstream power code exists and is even compiled in
(`src/power/Power.cc`, `ReportPower.cc`, `Power.i`, `Power.tcl`),
and it is the sole consumer of `internal_power`,
`leakage_power`, and `cell_leakage_power`, but the EOS facade owns a
private Tcl interpreter and exposes no way to evaluate
`report_power` or any other upstream Tcl command, and
`TimingService` never calls `Sta::power`. The registry even states
this: `core_commands.cc:3155-3159` ("No power ... measures power on
a mapped netlist, so the report does not invent a power number").
So internal and leakage power are dead data in EOS today, despite
being fully parsed.

Cell characterization: none. No EOS command characterizes cells,
writes Liberty (upstream `write_liberty` and `write_liberty_binary`
exist in `Liberty.tcl` and `Liberty.i` but are unreachable from EOS),
or reports Liberty contents (upstream `report_lib_cell` likewise
unreachable).

Other parsed-but-EOS-unused values: `area`, `dont_use`,
`is_macro_cell`, `cell_footprint`, `user_function_class`,
`related_power` and `ground_pin`, `pg_pin` ports,
`mode_definition`s, `statetable`s, `test_cell`s, and wireload,
operating-condition, and scale-factor objects (scale factors are
applied to table values, section 2; wireload and operating-condition
end-to-end effect on estimate-scene metrics was not traced, see
Unresolved). The `src/io/liberty` skeleton plays no role.

## 6. Tests covering `read_liberty`, and fixtures

Facade unit tests, the real parse tests.
`tests/unit/analysis/sta/sta_test.cc`: embedded `kLiberty`
(`:42-137`): library `eos_fixture`, `table_lookup`, units including
`leakage_power_unit : "1pW"`, thresholds,
`slew_derate_from_library`, `nom_*`; cells DFF (`ff`,
`setup_rising` and `hold_rising` with `rise`/`fall_constraint
(scalar)`, `rising_edge` with `cell_rise`/`fall` plus
`rise`/`fall_transition (scalar)`) and BUF (`function`,
`positive_unate`). `kLibertyElectrical` (`:138-277`) adds
`fanout_load`, `max_transition`, `max_capacitance`, `max_fanout`
(the electrical checks). `ReadLiberty` exercised at `:278`, `:423`;
`MissingLibertyFails` at `:619-622`. No `pg_pin`,
`internal_power`, `leakage_power` group, `bus`, `three_state`,
`noise`, or non-scalar table appears in either fixture (verified by
grep: only `leakage_power_unit` matches).

Timing-service tests. `tests/unit/analysis/timing/
timing_service_test.cc`: `kLiberty` (`:32ff`) and
`kLibertyElectrical` (`:116ff`), same shape as above (scalar tables,
`slew_derate_from_library`, thresholds); `scene.liberty_path`
wiring at `:359-363` and throughout. Same coverage gaps as above.

Registry tests, registration only, not parsing.
`tests/unit/ui/registry/registry_test.cc`: `kSynthMapLiberty`
(`:1472-1605`, AND2, OR2, INV, BUF, DFF, scalar tables) plus other
embedded libs (`:362`, `:648`, `:1995`, `:3317`); `read_liberty`
success and failure assertions at `:782`, `:833`, `:870`, `:891`,
`:908`, `:1606`, `:1673`, `:1713`, `:2656-2680`, and others. These
prove the registry stores the path; they do not prove the file
parses (parsing happens later, in the `synth_map` and `run_timing`
paths that reuse the same fixtures). `synth_map` tests additionally
exercise the ABC scrape path (section 1), which is a different
parser with its own, narrower subset.

Real-world fixture that does contain the ignored and parsed-but-unused
constructs. `src/pd/rsz/tests/fixtures/sky130hd_tt.trimmed.lib`
(line 1 header and throughout; grep matches for
`pg_pin|internal_power|leakage_power|noise` are present, including
`pg_pin ("VGND"/"VNB"/"VPB"/"VPWR")`, `leakage_power ()`,
`cell_leakage_power`, and `internal_power ()` with `fall_power`
tables). Its header also contains constructs the parser silently
drops: `revision`, `default_cell_leakage_power`,
`default_leakage_power_density`, `default_arc_mode`,
`default_constraint_arc_mode`, and ten `define(...)` statements.
Caveat: this is upstream resizer test data, not an EOS `tests/`
fixture, and no EOS unit test asserts anything about its power or
`pg_pin` content. The EOS-owned mock
(`src/pd/rsz/tests/mock/eos_mock.lib`, `lu_table_template` NLDM) and
`demo/synth/cells.lib` contain neither `pg_pin` nor power groups
(only `leakage_power_unit`).

Coverage gap summary: no EOS test asserts parsing or use of
`pg_pin`, `internal_power`, `leakage_power` groups,
`cell_leakage_power`, `bus`/`bundle`, `three_state`, non-scalar
(real NLDM) tables, CCS `output_current`, OCV and LVF tables,
`scaled_cell`, `statetable`, `test_cell`, or any noise construct. A
regression that silently dropped any of those would pass the current
suite.

## 7. Claimed Liberty subset vs. actual: mismatches

There is no EOS document or README that claims a Liberty subset for
`read_liberty`, so there is no direct claim to falsify. Searched
`docs/`, `README.md`, `src/analysis/timing/README.md`,
`src/analysis/timing/sta/README.md`, and `structure.yaml`. What
exists, and how it compares:

1. No subset claim (not a mismatch, but a gap the audit should
   record). `src/analysis/timing/sta/README.md` says only: "a
   StaEngine loads a Liberty library, a gate-level Verilog netlist,
   links the design, reads SDC ... and reports the worst slack and
   total negative slack". True, but it does not say which Liberty
   constructs drive those numbers. `src/analysis/timing/README.md`
   likewise says only "the Liberty for its corner". An audit
   document must not upgrade either sentence into "EOS supports
   Liberty X".
2. Apparent mismatch 1: the command name overpromises.
   `eos::read_liberty <corner> <path>` reads nothing at call time
   (section 1). A user (or a doc) treating a successful
   `read_liberty` as "the library parsed OK" is wrong: syntax
   errors, unknown `pg_type`, and missing-table warnings surface
   only later, inside `run_timing`, `synth_map`, or opt evaluation,
   attributed to that scene.
3. Apparent mismatch 2: "adopted OpenSTA" does not equal "all of
   OpenSTA's Liberty handling is available."
   `sta/README.md` plus `PROVENANCE.md` present OpenSTA as "adopted
   as the static timing engine". True for timing, but the same
   imported tree's power engine (`src/power/`) and Liberty writer
   and reporter (`write_liberty`, `report_lib_cell`) are compiled in
   yet unreachable from any EOS command (section 5). Any claim of
   the form "EOS reads Liberty, therefore power or noise are
   handled" would be false: power is parsed-but-dead, noise is not
   even parsed as a model.
4. Adjacent scope statement, correctly narrow, must not be
   generalized. `docs/phase2-exit-report.md` ("scalar Liberty timing
   only") describes the resynthesis v1 (ABC genlib) scope, not
   `read_liberty`. Conflating it with the STA path would understate
   STA (which handles full NLDM tables, CCS receiver and
   output-current data, OCV and LVF) and overstate the mapping path
   (which skips table-delay cells entirely, section 1).
5. Package and corner statements are not parser claims.
   `docs/sky130-package-*.md` statements about "exactly one Liberty
   corner" and corner sets describe package metadata, not which
   Liberty constructs the parser honors. No mismatch, but they
   should not be cited as parser coverage.

## 8. Unresolved (stated, not guessed)

1. Out-of-range table behavior. Interpolation is linear between
   bracketing points (`TableModel.cc:1079-1185`); whether the axis
   lookup clamps or linearly extrapolates outside the axis range was
   not traced, so no extrapolation rule is stated here.
2. `define()` statements. Parsed into the group tree
   (`LibertyParser.cc:156-197`); no reader consumer was found, but
   whether defines are stored on `LibertyGroup` or simply discarded
   with the group was not verified.
3. `include_file` resolution. The scanner opens the named file as
   given (`LibertyParser.cc:336-372`); whether a relative include is
   resolved against the parent file's directory or the process
   working directory was not verified, and an unopenable include is
   only warning 25.
4. Wireload, operating-condition, and scaled-cell end-to-end effect.
   Parsed and stored (section 3); scale-factor application to table
   values is verified (`TableModel.cc:833-856`), but no call site
   was traced proving wireload or operating-condition objects change
   an EOS estimate-scene metric.
5. CCS delay-calc selection. `output_current` and
   `receiver_capacitance` are parsed (section 2), but which delay
   calculator EOS's scenes select (NLDM vs. CCS vs. DMP) was not
   traced, so "CCS is used" is not established, only "CCS data is
   parsed and available to the delay calculators".
6. Checklist mapping. The review items S10/SI7/U6 reference a Logic
   Library Reference checklist whose text is not in this repo;
   statuses above are derived from code, not cross-checked against
   that external checklist document.
7. `related_power_pin`, `related_ground_pin`, `dont_use`, `area`,
   and similar stored attributes. No EOS-metric consumer was found
   (only upstream property exposure, for example
   `search/Property.cc:740-742`); whether any upstream Tcl query
   path could surface them is moot because EOS exposes no such query
   path (section 5), but not every upstream consumer of each stored
   attribute was exhaustively grepped.

## 9. Implications for later units

- Power leg: no power claim may rest on `read_liberty` success. The
  power constructs are parsed, but EOS computes no power. A
  `report_power` unit must wire the upstream power engine (or its
  own) to the parsed data and add activity annotation before any
  power optimization or power signoff statement.
- Noise and glitch analysis leg: blocked at the parser. Noise groups
  are not looked up at all, so no reporting unit can surface noise
  data without a parser change first. Any SI plan must budget that
  parser work, not just a report command.
- Library round-trip leg: no EOS command writes Liberty today. A
  round-trip unit must expose the upstream writer (or its own) and
  audit its loss against section 2, including the silently dropped
  constructs, which a writer can never recover.
- Test leg: the coverage gaps in section 6 are the starting list for
  parser regression tests. A fixture containing `pg_pin`, power
  groups, a bus, a three-state pin, a real NLDM table, and a noise
  group, with assertions on what is honored and what is dropped,
  would close the largest hole.
