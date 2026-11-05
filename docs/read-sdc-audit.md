# read_sdc audit (Immediate unit 2, review I11a/U4)

Base: `c5fa125c` (origin/main at audit time). Method: code trace from the
registry command through the timing service to the parser, plus the test
tree and docs. No live execution was used except where noted; anything
not established from code is listed under Unresolved, not guessed.

Scope note: this is an audit, not a fix. It publishes the accepted set
as it exists today, names every gap found, and states what the next
unit (the SDC subset implementation) must decide. It does not change
parser behavior.

## 1. Headline: there are two `read_sdc`s, and only one parses

`eos::read_sdc` does not parse SDC at all. `src/ui/registry/src/
core_commands.cc:3271-3280`: it takes exactly one argument, checks the
file is readable, stores `ctx.timing.sdc_path`, and returns
`sdc=<path>`. A syntactically garbage file passes `eos::read_sdc` and
fails only later, at `run_timing`, `report_timing`, or `synth_opt`.

The real parser is imported OpenSTA, not EOS-authored code.
`src/analysis/timing/sta/PROVENANCE.md` pins OpenSTA at commit
`0f2529d18218b87aa0e82de444395a830d5058a1`, imported 2026-10-25 with
zero patches. The SDC command set lives in `src/analysis/timing/sta/
src/sdc/Sdc.tcl` (87 `define_cmd_args`), plus an OpenROAD-fork corner
overlay in `src/analysis/timing/sta/src/tcl/AnalysisCorner.tcl` that EOS
never invokes (section 6).

Full trace:

1. `eos::read_sdc` stores the path. `run_timing` and `report_timing`
   require it non-empty (`core_commands.cc:750, 2695, 3594`:
   "no SDC: eos::read_sdc first").
2. `BuildTimingScenes` (`core_commands.cc:729-783`) builds one scene per
   active scenario but passes the same single `sdc_path` to every
   scene.
3. `src/analysis/timing/src/timing_service.cc:88, 169` calls
   `engine.ReadSdc(design.sdc_path)` per scene, into a fresh STA engine
   per scene.
4. `src/analysis/timing/sta/src/sta_facade.cc:519-523`
   (`StaEngine::ReadSdc`) runs bare `read_sdc <path>`: no `-mode`, no
   `-echo`, no `-analysis_corner`. A Tcl error becomes `kIoError`
   "could not read SDC file: <path>: <Tcl error>".
5. STA `read_sdc` (`Sdc.tcl:51-73`) evaluates the file as Tcl, command
   by command, at global scope (`include_file`, `src/analysis/timing/
   sta/src/tcl/Util.tcl:610`, `uplevel #0`), with all `::sta::` procs
   imported (`Init.tcl:35-39`). Consequence: an SDC file is not a
   closed SDC grammar. Any imported `sta::*` Tcl command, and plain
   Tcl, inside the file executes. No EOS doc states this, and no test
   exercises it (see Unresolved).

Other `ReadSdc` call sites use the same bare form: synth-opt candidate
judging (`core_commands.cc:609, 2731, 2843`) and the resizer path
(`src/ui/registry/src/repair_commands.cc:622-623`, a separate `dbSta`
interp, which falls back to `create_clock -name ... -period ...` at
`:626-629` if no SDC was given).

## 2. Accepted-command table

"Applied" means the proc calls an STA backend that stores state used by
timing queries. "Parsed, ignored" means the proc exists but its body is
`# ignored` or only warns. Abbreviated command and option names are
also accepted when the prefix is unique (section 4).

### Reader and writer

| Command | Options | Disposition |
|---|---|---|
| `read_sdc` (`Sdc.tcl:37`) | `-echo`, `-mode mode_name`, `filename` (plus `-analysis_corner` via the overlay, unused by EOS) | Reads the file into the current mode (or a named mode); gzip auto-decompressed. EOS never passes `-mode`. |
| `write_sdc` (`Sdc.tcl:76`) | `-mode`, `-map_hpins`, `-digits`, `-gzip`, `-no_timestamp` (proc also accepts undocumented `-compatible`) | Writes one mode's constraints to one file. Exists in the engine but is not reachable from `eos::` (section 7). |

### General purpose

| Command | Options | Disposition |
|---|---|---|
| `current_instance` (`:117`) | `[instance]` | Applied. |
| `set_hierarchy_separator` (`:138`) | `separator` | Applied; must be one of `/@^#.\|`, else error 342. |
| `set_units` (`:163`) | `-time -capacitance -resistance -voltage -current -power -distance` | Check-only, does not set units: warns 345 on scale mismatch, errors 343/344/501 on bad prefix/suffix. |

### Object access (usable inside SDC files; return objects, set nothing)

`all_clocks` (`:259`), `all_inputs` (`:268`, `-no_clocks`),
`all_outputs` (`:282`), `all_registers` (`:292`, `-clock
-rise_clock -fall_clock -cells -data_pins -clock_pins -async_pins
-output_pins -level_sensitive -edge_triggered`), `current_design`
(`:371`, warns 347 for a non-top cell), `get_cells` (`:397`,
`-hierarchical -hsc -filter -regexp -nocase -quiet -of_objects`,
alias `get_cell`), `get_clocks` (`:492`, `-regexp -nocase -quiet
-filter`, alias `get_clock`), `get_lib_cells` (`:542`),
`get_lib_pins` (`:632`), `get_libs` (`:752`), `get_nets` (`:829`),
`get_pins` (`:913`), `get_ports` (`:1020`). `-nocase` without
`-regexp` warns 358 and is ignored. Non-matching patterns warn unless
`-quiet`.

### Clocks (all applied unless noted)

| Command | Options | Disposition |
|---|---|---|
| `create_clock` (`:1088`) | `-name -period -waveform -add -comment`, `[pins]` | Applied via `make_clock`. `-period` required (error 370); `-waveform` must have an even edge count, non-decreasing, at most 2 periods (errors 371-373); default waveform `{0 period/2}`. Virtual clock if no pins. |
| `delete_clock` (`:1186`) | `-all`, `clocks` | Applied. |
| `create_generated_clock` (`:1209`) | `-name -source -master_clock -divide_by \| -multiply_by -duty_cycle -invert -edges -edge_shift -combinational -add -comment`, `port_pin_list` | Applied. `-divide_by`/`-multiply_by`/`-edges` are mutually exclusive (errors 380-390); `-edges` must be exactly 3 edges (385). Trap: `-edge_shift` is only read inside the `-edges` branch; with `-divide_by`/`-multiply_by` it is parsed and dropped without warning. |
| `delete_generated_clock` (`:1398`) | `-all`, `clocks` | Applied (generated clocks only). |
| `group_path` (`:1427`) | `-name -weight -critical_range -default -comment -from/-rise_from/-fall_from -through/-rise_through/-fall_through -to/-rise_to/-fall_to` | Applied via `make_group_path`, except `-weight` and `-critical_range` are parsed but ignored (explicit comment at `:1451`). `-name` or `-default` required (errors 393-395); positional args rejected (392). |
| `set_clock_gating_check` (`:1507`) | `-setup -hold` (keys), `-rise -fall -low -high` (flags), `[objects]` | Applied. `-setup` or `-hold` required (397); `-high` plus `-low` together errors (396). Global if no objects. |
| `set_clock_groups` (`:1591`) | `-name -logically_exclusive -physically_exclusive -asynchronous -allow_paths -comment`, `-group clocks` | Applied. Exception to section 4: unknown keywords only warn (402, 403) instead of erroring. |
| `unset_clock_groups` (`:1654`) | `-logically_exclusive -physically_exclusive -asynchronous -name -all` | Applied (removal). |
| `set_clock_latency` (`:1722`) | `-source -clock -rise -fall -min -max -early -late`, `delay objects` | Applied. `-early`/`-late` without `-source` is a hard error (410); `-clock` is ignored for clock objects (warn 408). |
| `unset_clock_latency` (`:1790`) | `-source -clock`, `objects` | Applied (removal). |
| `set_sense` (`:1840`) | `-type clock\|data -positive -negative -pulse -stop_propagation -clocks`, `pins` | Partial, with a trap: only acts if `-type` is given. `-type clock` is applied; `-type data` warns 413 only, not applied; `-pulse` warns 416 ("not supported") but other sense is still applied. Without `-type`, the proc silently does nothing, so the common SDC form `set_sense -positive <pins>` is silently dropped. |
| `set_clock_sense` (`:1876`, deprecated SDC 2.1) | `-positive -negative -pulse -stop_propagation -clock`, `pins` | Applied, but always warns 415 (deprecated, use `set_sense -type clock`); `-pulse` warns 416, not applied; mutually exclusive flags warn 417, not error. |
| `set_clock_transition` (`:1932`) | `-rise -fall -min -max`, `transition clocks` | Applied; virtual clocks warn 419 and are skipped. |
| `unset_clock_transition` (`:1962`) | `clocks` | Applied (removal). |
| `set_clock_uncertainty` (`:1979`) | `-from/-rise_from/-fall_from -to/-rise_to/-fall_to -rise -fall -setup -hold`, `uncertainty [objects]` | Applied. |
| `unset_clock_uncertainty` (`:2098`) | Same `-from/-to/-rise/-fall/-setup/-hold` set, `[objects]` | Applied (removal). |
| `set_propagated_clock` (`:3253`) | `objects` | Applied; virtual clocks warn 450 and are skipped. |
| `unset_propagated_clock` (`:3275`) | `objects` | Applied (removal). |

### Exceptions and path constraints (all applied)

`set_data_check` (`:2191`, `-from/-rise_from/-fall_from
-to/-rise_to/-fall_to -setup -hold -clock`, `margin`; missing
`-from`/`-to` errors 425/426); `unset_data_check` (`:2255`);
`set_disable_timing` (`:2321`, `-from -to`, `objects`; `-from`/`-to`
ignored with warn 429 for lib_pin/port/pin args);
`unset_disable_timing` (`:2508`); `set_false_path` (`:2613`, `-setup
-hold -rise -fall -reset_path -comment -from... -through... -to...`);
`set_max_delay` (`:2826`, `-rise -fall -ignore_clock_latency
-reset_path -probe -comment -from... -through... -to...`, `delay`);
`set_min_delay` (`:2915`, same option set); `set_path_margin`
(`:2941`, `-setup -hold -rise -fall -comment -from... -through...
-to...`); `set_multicycle_path` (`:3046`, `-setup -hold -rise -fall
-start -end -reset_path -comment -from... -through... -to...`,
`path_multiplier`); `unset_path_exceptions` (`:3126`, `-setup -hold
-rise -fall -from... -through... -to...`); `set_max_time_borrow`
(`:2891`, `limit objects`, no options; limit must be a positive
float); `set_min_pulse_width` (`:3003`, `-low -high`, `value
[objects]`, per-pin/inst/clock or global; neither flag means both).

### Ideal commands: accepted syntactically, no effect

| Command | Disposition |
|---|---|
| `set_ideal_latency` (`:2676`) | Help says "parsed but ignored." Body is `# ignored`. All options swallowed silently. |
| `set_ideal_network` (`:2691`) | Help says "parsed but ignored." Body `# ignored`; `-no_propagation` "Ignored." |
| `set_ideal_transition` (`:2703`) | Help says "parsed but ignored." Body `# ignored`. |
| `set_ideal_net` (`:2687`) | Anomaly, see Unresolved: `define_cmd_args` exists but no `proc set_ideal_net` exists anywhere in `Sdc.tcl`, and no binding was found in `Sdc.cc`/`Sdc.i`. Predicted outcome is a Tcl invalid-command error at `run_timing`, not executed to confirm. |
| `create_voltage_area` (`:4335`) | Body `# ignored`; options swallowed. |
| `set_level_shifter_strategy` (`:4352`), `set_level_shifter_threshold` (`:4364`) | Bodies `# ignored`; `-rule`/`-voltage` swallowed. |

### IO and environment (all applied unless noted)

`set_input_delay` (`:2713`, `-rise -fall -max -min -clock
-clock_fall -reference_pin -source_latency_included
-network_latency_included -add_delay`, `delay port_pin_list`);
`unset_input_delay` (`:2809`); `set_output_delay` (`:3182`, same
option set; note: with `-reference_pin`, if no clock arrives at the
pin the command is ignored); `unset_output_delay` (`:3209`);
`set_case_analysis` (`:3297`, `0|1|zero|one|rise|rising|fall|falling
pins`); `unset_case_analysis` (`:3325`); `set_logic_dc` (`:3624`),
`set_logic_one` (`:3645`), `set_logic_zero` (`:3657`) (set constant
values; no paths propagate from constant pins); `set_drive` (`:3340`,
`-rise -fall -min -max`, `resistance ports`);
`set_driving_cell` (`:3366`, `-lib_cell -cell -library -rise -fall
-min -max -pin -from_pin -input_transition_rise
-input_transition_fall ...`; warns 458 `-multiply_by ignored`, 459
`-dont_scale ignored`); `set_fanout_load` (`:3502`) warns 461
"set_fanout_load not supported" and does nothing;
`set_input_transition` (`:3511`, `-rise -fall -min -max -clock
-clock_fall`, `transition ports`; `-clock`/`-clock_fall` warn 462/463
"not supported" and are dropped); `set_load` (`:3552`, `-rise -fall
-max -min -subtract_pin_load -pin_load -wire_load`, `capacitance
objects`; `-subtract_pin_load` illegal for ports, warn 486;
`-pin_load` illegal for nets, warn 464);
`set_port_fanout_number` (`:3798`, `-max -min`, `fanout ports`);
`set_resistance` (`:3822`, `-min -max`, `resistance nets`);
`set_timing_derate` (`:3846`, `-early|-late -rise -fall -clock -data
-net_delay -cell_delay -cell_check`, `derate [objects]`; warns 469 if
factor exceeds 2.0, 470 for `-cell_delay`/`-cell_check` on nets);
`unset_timing_derate` (`:3952`, no args, removes all derates);
`set_operating_conditions` (`:4108`, `-analysis_type
single|bc_wc|on_chip_variation -library [condition] -min -max
-min_library -max_library`; unknown condition is a hard error
474/475, bad analysis type 476); `set_wire_load_model` (`:4209`,
`-name -library -min -max`, `[objects]`; unknown model is a hard
error 480); `set_wire_load_mode` (`:4194`,
`top|enclosed|segmented`; help says "ignored during timing but
included in SDC files that are written");
`set_wire_load_min_block_size` (`:4185`) warns 477 "not supported",
does nothing; `set_wire_load_selection_group` (`:4254`, `-library
-min -max`, `group_name [objects]`; help says "parsed but not
supported" while a backend call exists, so treat as stored but with
unverified effect); `set_voltage` (`:4302`, `-min -object_list`,
`max_case_voltage`, stored globally and on listed power nets);
`set_pvt` (`:4402`, `insts -min -max -process -voltage
-temperature`).

### Design-rule and power limits (stored; checked only by check-type queries, which EOS does not expose)

`set_max_area` (`:3669`, `area`; help says "ignored during timing
but included in SDC files that are written");
`set_max_capacitance` (`:3679`, `cap objects`);
`set_min_capacitance` (`:4095`, `cap objects`);
`set_max_fanout` (`:3706`, `fanout objects`);
`set_max_transition` (`:3733`, `-clock_path -data_path -rise -fall`,
`slew objects`; the path flags apply only to clock objects, and for
ports/designs they warn 468 and are ignored);
`set_max_dynamic_power` (`:4376`, `power [unit]`);
`set_max_leakage_power` (`:4387`, same form).

## 3. Rejected and ignored summary (the audit-critical list)

Hard error, whole file stops and `run_timing` fails: unknown command
(section 4); unknown option on most commands (error 562); missing
required option or value (560/561, plus per-command errors above);
bad values (for example `create_clock` without `-period`, 370).

Silently ignored, accepted with no effect and no warning:
`set_ideal_latency`, `set_ideal_network`, `set_ideal_transition`,
`create_voltage_area`, `set_level_shifter_strategy`,
`set_level_shifter_threshold`; `group_path -weight`/`-critical_range`;
`create_generated_clock -edge_shift` outside `-edges`; `set_sense`
with no `-type` (entire command dropped).

Warned but otherwise applied or not applied: `set_sense -type data`
(warn 413, dropped), `set_clock_sense` (warn 415 deprecated; `-pulse`
warn 416, dropped), `set_fanout_load` (warn 461, dropped),
`set_wire_load_min_block_size` (warn 477, dropped),
`set_input_transition -clock`/`-clock_fall` (warns 462/463, dropped),
`set_driving_cell -multiply_by`/`-dont_scale` (warns 458/459,
dropped), `set_max_transition` path flags on ports/designs (warn
468), `set_clock_latency -clock` on clock objects (warn 408),
`set_units` scale mismatch (warn 345).

## 4. Error behavior: unrecognized command and option (exact strings)

Unknown option: `src/analysis/timing/sta/src/tcl/Util.tcl:39-90`
(`parse_key_args`, default `unknown_key_is_error 1`). It first tries
an exact match, then a unique-prefix glob match, so abbreviated
options like `-per` for `-period` are accepted if unambiguous;
otherwise `sta_error 562 "$cmd $arg is not a known keyword or flag."`

Unknown command: `Util.tcl:761-798` (`sta_unknown`, installed as
`namespace unknown`). A lone bus subscript like `foo[2]` is returned
as a value; otherwise a unique-prefix abbreviation of a known command
resolves (for example `create_clo` resolves to `create_clock`);
an ambiguous prefix errors with `Ambiguous command name "<name>":
<sorted list>.`; otherwise it falls through to Tcl's `::unknown`
and the standard `invalid command name` error.

File-level consequence: `Util.tcl:660-715` (`include_file`). On a
command error, if `sta_continue_on_error` is 1 it prints `Error:
<file>, <line> <error>` and continues; otherwise it breaks and
re-raises with file and line. Default is 0 (`Variables.tcl:198-200`).
No EOS-authored code outside `sta/src` sets `sta_continue_on_error`,
so under EOS the default applies: the first error aborts the whole
SDC file and fails `run_timing` with `could not read SDC file: <path>:
Error <id>: <file> line <n>, <msg>`.

Unknown-option caveat: commands whose proc passes 0 for
`unknown_key_is_error` (`set_sense`, `set_clock_groups`,
`group_path`, `set_multicycle_path`, among others) do not hard-error
on unknown keys; they warn or drop them per command. There is no
single uniform rule; it is per proc.

## 5. `create_clock` collision: no misinterpretation at the STA level, two disjoint systems

Note, 2026-11-02 (Immediate unit 3): the collision described below
is resolved in the tree from this date. The registry command
`create_clock` is now the SDC command documented in
`docs/sdc-commands.md`, and the design-construction record moved to
`bind_clock <name> <target>`. The old two-argument
`create_clock <name> <target>` form fails fast and names
`bind_clock`. The paragraphs below remain a true record of the tree
this audit audited; they describe the pre-unit-3 state.

SDC-file `create_clock` is fully accepted with SDC semantics
(`Sdc.tcl:1088-1184`: `-name -period -waveform -add -comment`,
`make_clock`; `-period` mandatory, waveform default `{0 period/2}`).
A file containing `create_clock -period 10 -waveform {1 8} clk1` or
`create_clock -name clk -period 10 [get_ports clk]` (the form used by
every EOS test fixture) creates a real STA clock that drives slack.
The resizer path uses it the same way
(`repair_commands.cc:626-629`).

EOS registry `create_clock` is a different command in a different
interpreter. `core_commands.cc:1224-1245`: `create_clock <name>
<target>`, exactly 2 args, stores a `db::ConstraintKind::kClock`
record in `ctx.project.constraints` (bound or unbound status only).
It is exposed as `::eos::create_clock` via the EOS Tcl shell, and it
carries no period, no waveform, and no STA effect: nothing in
`src/analysis/timing/src/timing_service.cc` reads
`project.constraints`; timing reads only the SDC file path.
Conversely, an SDC `create_clock` never creates a registry constraint.

So the collision is real but not a misparse: the same bare name
means two things in two interpreters that never meet (EOS shell vs.
STA interp). The hazard is user confusion, and the fact that
`eos::create_clock` silently does nothing for timing. A script that
"creates a clock" via `eos::create_clock` and then runs `run_timing`
gets an unconstrained design, not an error, unless the SDC file also
defines the clock. This matches the review's framing (review
section 12/B18) but corrects one implication: the SDC side is not
broken or limited. It is the EOS-construction side that lacks
period and waveform semantics. The rename-with-migration decision
(owner, 2026-11-01) is a prerequisite for the SDC subset unit, and
this audit does not prejudge its shape.

## 6. Review signoff-critical subset checked against the parser: none missing

The review says the plan's I6 list omits the commands below. It does
not say the parser rejects them, and the parser in fact accepts all
of them:

| Review-named command | Parser status |
|---|---|
| `set_clock_gating_check` | Accepted, applied. |
| `set_data_check` | Accepted, applied. |
| `set_min_pulse_width` | Accepted, applied. |
| `set_sense` | Accepted with the section 2 traps (`-type` required for any effect; `-type data` and `-pulse` unsupported). |
| `set_ideal_network` | Accepted syntactically, ignored. |
| `set_ideal_latency` | Accepted syntactically, ignored. |
| `set_ideal_transition` | Accepted syntactically, ignored. |
| `group_path` | Accepted, applied except `-weight`/`-critical_range` ignored. |
| `set_max_time_borrow` | Accepted, applied. |

Audit consequence: the gap for the subset unit is not parser coverage
for 6 of 9. It is (a) the three ideal commands having no timing effect
by upstream design, (b) the `set_sense -type` trap, and (c) EOS
exposing no way to author these from `eos::` (no `eos::` SDC-authoring
commands exist, only the file path) and no check-type reporting to
observe their violations (section 9). The review's sequencing point
(audit before the subset unit and before `write_sdc`) still stands;
its "omitted" wording must not be read as "rejected by `read_sdc`".

## 7. `set_spef` and scenario and corner binding

`eos::set_spef` (`core_commands.cc:3282-3293`): `set_spef <path>`,
readability-checked, stored as the single `ctx.timing.spef_path`. It
is not an SDC command and is not read from the SDC file by EOS.

Binding: `BuildTimingScenes` (`core_commands.cc:765-776`). An
`extracted`-parasitics scenario gets the shared SPEF ("no SPEF ...
eos::set_spef first" if unset); an `estimate` scenario gets none, and
`timing_service.cc:73-78` hard-fails if an estimate scene names a
SPEF ("parasitics levels are never mixed silently"). `TimingService`
then calls `ReadSpef` per extracted scene (`timing_service.cc:91-94,
172-176`).

SDC-to-scenario binding: none on the EOS surface. The scenario's
`mode` string is never passed to STA: no EOS-authored code calls
`set_cmd_mode`, `read_sdc -mode`, `define_scene`, or
`set_cmd_analysis_corner`. Every scene re-reads the identical SDC
file into a fresh engine in the default mode. Mode-specific or
corner-specific SDC files are therefore impossible via `eos::` today;
a user wanting per-mode constraints would have to put them in one
file, and in-file mode switching is unverified (see Unresolved).

Corner scope exists only inside the engine, unused by EOS:
`AnalysisCorner.tcl` (OpenROAD-fork overlay, sourced after
`Sta.tcl`) adds `define_analysis_corner -sdc`, wraps `read_sdc` with
`-analysis_corner`, and whitelists corner-scoped commands (derates,
IO delays, uncertainty, latency and insertion) while rejecting
structural commands in corner scope (disallowed list includes
`create_clock`, `create_generated_clock`, `set_clock_groups`,
`set_propagated_clock`, `set_false_path`, `set_multicycle_path`,
`set_max_delay`, `set_min_delay`, `group_path`, `set_case_analysis`,
`set_disable_timing`, `create_voltage_area`,
`set_clock_gating_check`, `set_clock_sense`, `set_sense`). EOS never
invokes any of this; it is dead capability from the EOS surface.

## 8. `write_sdc`: current state

No `eos::write_sdc` registry command exists at this base (only
`write_verilog` at `core_commands.cc:2984`). This confirms the
review's claim at the `eos::` level.

STA-level `write_sdc` exists but is unreachable from EOS.
`Sdc.tcl:76-115` plus `src/analysis/timing/sta/src/sdc/WriteSdc.cc`
(`write()`): header (current design, timestamp unless
`-no_timestamp`), then `writeTiming()` (clocks, generated clocks,
propagated clocks, uncertainty, latencies, insertions, senses, clock
groups, input and output delays, disables, exceptions, data checks),
`writeEnvironment()` (operating conditions, wireload mode, net and
port loads, drives, driving cells, input transitions, resistances,
case analysis, derates, voltages), `writeDesignRules()` (min pulse
widths, latch borrow limits, slew, cap, and fanout limits, max area
and power), `writeVariables()`. It is single-mode: the `-mode`
argument selects which mode's `Sdc` object is dumped to the one file.
There is no per-scenario or per-view multi-file emission.

Emitted vs. accepted mismatch, from the `WriteSdc.cc` writer-function
list, not from a live round-trip: the emitted set covers most applied
constraints, but (a) the three ideal commands emit nothing (nothing
stored); (b) positive `set_clock_gating_check` has no writer, only
`writeDisabledClockGatingChecks` for the
`set_disable_clock_gating_check` family, and `writeTiming()` lists no
gating-check writer; (c) `set_units`/`set_hierarchy_separator`/
`set_clock_groups -allow_paths` handling and `set_wire_load_model`'s
stored model were not observed in writer output (the wireload writer
emits only the mode); (d) anything stored per-corner-overlay is
outside the single-mode dump. So even at engine level, emitted does
not equal accepted; at `eos::` level the gap is total (no command at
all). Separately, the Yosys-side `opensta` pass
(`src/synth/yosys/src/techlibs/common/opensta.cc:102-103`) shells out
to an external `opensta` binary's `read_sdc`/`write_sdc`. That is not
EOS's engine and not an EOS surface.

The review's single-file vs. per-mode and per-scenario MMMC emission
point is therefore still open: STA `write_sdc` is single-file per
mode, and EOS exposes none of it.

## 9. Docs and help-text claims vs. reality

Searched `docs/`, the repo `README.md`,
`src/analysis/timing/README.md`,
`src/analysis/timing/sta/README.md`, and registry usage strings for
any claimed SDC subset:

- No EOS document claims any SDC subset.
  `src/analysis/timing/sta/README.md` says only that the engine
  "reads SDC constraints" (generic). The registry help for `read_sdc`
  is just the usage string `"read_sdc <path>"`
  (`core_commands.cc:3271`): no subset, no option list.
  `demo/synth/README.md` shows an example using only `create_clock`,
  `set_input_delay`, and `set_output_delay`, but presents it as a
  demo, not a subset claim.
- Mismatch list: (1) Absence, not overclaim: users have no
  EOS-published list of what `read_sdc` accepts; the de facto list
  is the upstream OpenSTA `Sdc.tcl`, which no EOS doc points at.
  (2) The review's "write_sdc absent" claim is true at `eos::` but
  false at engine level (STA `write_sdc` exists, unreachable). Every
  such claim must be scoped to a layer. (3) The review's "omitted"
  wording is about the plan's subset list, not parser rejection
  (section 6). (4) The STA help texts themselves overstate in
  places: `set_wire_load_mode` and `set_max_area` say "ignored during
  timing but included in SDC files that are written" (true for the
  writer, but irrelevant to EOS since EOS cannot invoke the writer);
  `set_wire_load_selection_group` says "parsed but not supported"
  while a backend call exists. (5) `eos::create_clock`'s usage string
  (`"create_clock <name> <target>"`) does not warn that it has no
  timing effect.

## 10. Tests covering `read_sdc`: fixtures exercise almost nothing

No standalone `read_sdc` test exists; coverage is incidental via
timing and synth tests. All fixtures are inline C++ strings or
resizer fixture files (no `.sdc` fixture under `tests/`):

- `tests/unit/ui/registry/registry_test.cc`: `kTimingSdc` = `create_clock
  -name clk -period 10 [get_ports clk]` only, used by
  `TimingRunRegistersPerSceneReports` and about ten other run and
  report tests. `kClosureSdc` = same form, period 1.
  `kSynthMapSdc` = virtual clock plus `set_input_delay -clock vclk 0
  [all_inputs]` plus `set_output_delay -clock vclk 0 [all_outputs]`.
  Negative test: a nonexistent path fails `eos::read_sdc` with
  `kIoError` and leaves `sdc_path` empty. This tests only the
  readability check, not SDC parsing. No test feeds a malformed SDC
  file, an unknown command, or an unknown option through to
  `run_timing`.
- `tests/unit/analysis/sta/sta_test.cc` (`kSdc`, `kSdcTight`):
  `create_clock` only, period 10 and 0.5.
  `tests/unit/analysis/timing/timing_service_test.cc`: same.
- `src/pd/rsz/tests/fixtures/*.sdc` plus `src/pd/rsz/tests/mock/*.sdc`
  (resizer path, separate `dbSta` interp): distinct commands across
  all fixtures are `create_clock` (15), `set_load` (18),
  `set_input_delay` (10), `set_output_delay` (6), `set_max_delay` (3),
  `set_max_fanout` (7), `set_max_transition` (6),
  `set_propagated_clock` (8), `set_clock_uncertainty` (2). No fixture
  exercises false paths, multicycle, exceptions, gating checks, data
  checks, `group_path`, min pulse width, sense, derates, or any
  ignored or warning command.
- `demo/synth/top.sdc`: virtual clock period 1.25 plus zero input and
  output delays (same 3 commands as `kSynthMapSdc`).

Net test coverage gap: the accepted set in section 2 is about 87
commands; tests exercise 9. Nothing tests unknown-command or
unknown-option failure at `run_timing`, ideal-command silent ignore,
`set_sense` without `-type`, `set_ideal_net`, multi-scenario SDC
sharing, or a `write_sdc` round-trip (no test at all, since no
`eos::` command exists).

## 11. Unresolved (stated, not guessed)

1. `set_ideal_net` runtime behavior: define without proc and no found
   C++ binding predicts a Tcl `invalid command name` error aborting
   `run_timing`, but this was not executed. Needs one live STA-interp
   run to confirm.
2. In-file mode and corner commands: whether `set_cmd_mode`,
   `read_sdc -mode` nested inside a file, or `define_scene` /
   `set_cmd_analysis_corner` invoked from within an SDC file affect
   the slack EOS reports. EOS never sets mode and queries via the C++
   API; upstream mode semantics for the query path were not traced.
3. Arbitrary Tcl in SDC scope: `include_file`'s `uplevel #0`
   evaluation plus `namespace import sta::*` implies non-SDC STA
   commands (`report_checks`, `read_spef`, `define_scene`, plain Tcl)
   in a file would execute, but no EOS doc states this and no test
   exercises it; side effects on later EOS steps are untested.
4. Visibility of applied-but-unreported constraints: EOS
   `run_timing` and `report_timing` expose only worst and TNS slack,
   hold slack, electrical checks, and violating endpoints
   (`timing_service.cc:96-146`). Whether `set_data_check`,
   `set_min_pulse_width`, `set_clock_gating_check`, or design-rule
   violations surface anywhere in EOS output is untraced. STA applies
   them, but EOS has no `report_check_types` equivalent command, so
   acceptance does not imply observability. This is the live question
   behind review items I4/I9.
5. Write-side round-trip gaps beyond the writer-function list
   (section 8) were determined by reading `WriteSdc.cc`'s writer list,
   not by executing a read, write, and read round-trip. A live
   round-trip could reveal additional loss or confirm the listed
   gaps.

## 12. Implications for the SDC subset unit (unit 3)

- The de facto accepted set is the upstream OpenSTA `Sdc.tcl` set
  above, not any EOS-published subset. Unit 3 must decide whether the
  signoff-critical subset it authors is a restriction of this set, an
  extension, or a restatement, and must say so explicitly.
- The subset unit must resolve the `set_sense -type` trap and the
  ideal-command no-effect behavior in its own spec: either document
  them as accepted-but-inert, or reject them loudly. Silent ignore
  must not survive into a signoff claim.
- The subset unit must cover the `eos::create_clock` rename with
  migration first (owner decision, 2026-11-01); until then, any SDC
  authoring surface must not reuse the colliding name.
- The subset unit must state the SDC-to-scenario binding rule
  (today: none, one file shared by all scenes in default mode) before
  it can claim MMMC or per-view behavior.
- A `write_sdc` form (unit 5) must emit a set that matches the
  accepted set, per the review's sequencing rule. The engine-level
  writer gaps in section 8 (gating checks, ideal commands, wireload
  model) are the starting list for that unit's writer audit.
