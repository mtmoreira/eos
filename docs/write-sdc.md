# `write_sdc` in the EOS registry (Immediate unit 8)

`eos::write_sdc` is the SDC writer, review item I5: the
single-scenario form. A flow that can read SDC but never emit it
cannot be checked by any other tool, and this command closes that
gap: it writes the session constraint set as one SDC file that
`read_sdc` (and any SDC reader) accepts, so the round-trip is
checkable.

```text
write_sdc [-compress] <path>
```

It runs over the session constraint set and needs nothing else:
no Liberty, no SDC file, and no STA run. It never mutates
session state. With no design open every row skips as "no design
is open" and the file is the header plus the skip list.

## Source: the session set, not the engine writer

The writer is written from scratch over `ctx.project.constraints`.
It does not expose the engine's internal `WriteSdc.cc`: that
writer runs on file-origin constraints and drops positive
`set_clock_gating_check` rows and the ideal commands
(`docs/read-sdc-audit.md` section 8), while this one emits every
row the session can express. File-origin constraints that were
never authored in the session stay in their source file; that is
the boundary, stated here, not hidden.

## The file

One SDC command per stored row, in constraint-id order, except
clocks and `group_path`, which merge by name (see below). Every
target renders as an explicit `get_*` collection for its bound
kind, so no wildcard or `-of_objects` form survives into the
file. No timestamp is written, so repeated writes are
byte-identical (the repeatable-reports discipline of unit 1).

The header names the active scenario(s) and states the counts:

```text
# EOS write_sdc: single-scenario form (Immediate unit 8, review item I5)
# scenario: func_tt
# The constraint set is shared by all scenarios in this increment, so
# this one file carries every scenario's constraints. No per-scenario
# state exists to lose in a merge; per-scenario files follow the
# MMMC view model (not built yet). No timestamp is written, so
# repeated writes are byte-identical.
# 11 constraints emitted, 0 rows skipped.
```

A footer lists every skipped row with its reason, so the file
alone documents what it omits. Nothing is silently dropped. The
payload carries the same information: `sdc=<path>
constraints=<n> skipped=<m>` plus one `skipped: <kind> <name>
<target> (<reason>)` line per skipped row.

## Per-kind emission

| Kind | Emitted form |
|---|---|
| `clock` | `create_clock -period <p> [-waveform {<r> <f>}] [-name <n>] [-add] <targets>` |
| `generated_clock` | `create_generated_clock -source [get_pins <s>] [-master_clock <m>] [-divide_by <n> \| -multiply_by <n>] [-invert] [-combinational] [-name <n>] [-add] <targets>` (no `-edges`: EOS does not accept it) |
| `clock_gating_check` | `set_clock_gating_check [-setup <v>] [-hold <v>] [-high \| -low] <target>` (edge `both`: no flag) |
| `data_check` | `set_data_check -from <f> -to <t> [-setup <m> \| -hold <m>]`; upstream takes one margin with `-setup`/`-hold` as flags, so a row with both margins at different values emits two lines, equal values emit one line with both flags |
| `min_pulse_width` | `set_min_pulse_width [-high \| -low] <value> <target>` (value first, positional) |
| `sense` | `set_sense -type clock (-positive \| -negative \| -stop_propagation) [-clock <name>...] <target>` (EOS is clock-only; `-type` is required upstream or the command is silently dropped) |
| `ideal_network` | `set_ideal_network [-no_propagate] <target>` (upstream ignores it on read; emitted as text) |
| `ideal_latency` / `ideal_transition` | `set_ideal_latency <value> [-rise \| -fall] [-min \| -max] <target>` (upstream ignores on read; emitted as text) |
| `group_path` | `group_path -name <g> [-from...] [-through...] [-to...] [-weight <w>] [-critical_range <r>]` (`-weight` and `-critical_range` are parsed but ignored upstream; emitted, documented) |
| `max_time_borrow` | `set_max_time_borrow <value> <target>` |

Target rendering, by the bound object kind: port
`[get_ports <name>]`, pin `[get_pins <name>]` (the name is
`<instance>/<pin>`), net `[get_nets <name>]`, instance
`[get_cells <name>]`. One object renders as its collection.
Several of one kind render as one `get_*` call over a braced
name list, `[get_ports {a b}]`. Several of mixed kinds render as
a bare braced name list, `{a b}`, which the upstream resolvers
expand per name (a repeated option key overwrites upstream, so
a flag per kind is not expressible).

## Merge rules (replace semantics)

Upstream `create_clock` without `-add` replaces a clock of the
same name, and `group_path` redefinition replaces the group, so
the writer must merge rows that would replace each other:

- `create_clock` and `create_generated_clock` rows group by
  (kind, name, parameters), then by target kind. `-add` is
  emitted on all but the first line of a repeated name (upstream
  `create_clock` without `-add` replaces the named clock;
  `-add` requires `-name`, error 368). `-name` is emitted when
  several targets share the name, when the name differs from the
  single target, or when `-add` is set.
- `group_path`: one line per group (the name is the identity).
  The endpoint role comes from the stored `endpoint` parameter
  (`to`, `through`, or `from`); rows without it (old
  checkpoints) derive it by set equality, defaulting to `-to`.
- All other kinds: one line per row, in id order. Lines are
  ordered by first row id.

## Cell-level constraints are not rewritten onto pins

No rewrite for any of the eleven kinds. Upstream accepts the
stored object kinds for each (`set_min_pulse_width` takes
per-pin/instance/clock, `set_max_time_borrow` takes
clocks/instances/pins, `set_clock_gating_check` takes objects,
the ideal commands are ignored); expanding a cell onto its pins
would change what the constraint applies to, and the store has
no pin inventory for a cell.

## Alternate hierarchy separator: vacuous for emitted rows

EOS level names come from Verilog identifiers (no `/`); pin
paths use `/` as the separator by construction. A stored name
containing a separator outside a pin path cannot bind and is
skipped as unresolved.

## Skip rules (reported, never silent)

| Row | Reason |
|---|---|
| Status `kUnresolved` | `unresolved target` |
| Status `kEmpty` | `empty target` |
| `clock` without `period_ns` | `construction record, not an SDC clock` (covers `bind_clock` and bare `add_constraint` clock rows) |
| Any other kind with no stored parameters | `no stored parameters` (except `ideal_network`, which is valid with empty parameters) |
| Clock / generated-clock target bound to a net or instance | `clock target is not a port or pin` |
| Generated-clock row whose source does not resolve to a port or pin | `unresolvable source` / `source is not a pin or port` |
| Name with a wildcard or Tcl-special character | `name contains a wildcard character` / `name cannot be addressed unambiguously` |
| Bound object with no readable kind (defensive) | `bound object has no readable kind` |
| No open design | `no design is open` |

## `-compress`

`write_sdc -compress <path>` writes gzip bytes (zlib) to the
given path; no `.gz` suffix is added automatically. The test
verifies `-compress` by the gzip magic bytes and by feeding the
`.gz` file to `read_sdc` plus `run_timing`: STA auto-decompresses
gzip, so the round-trip holds for compressed output too.

## Round-trip

Verified by the registry tests: a session clock written by
`write_sdc` and read back by `read_sdc` plus `run_timing` gives
the same slack as the hand-written fixture SDC (worst slack
9.4 ns, worst hold slack 0.45 ns on the timing fixture).

## What a merge would lose

The single-scenario form emits one file carrying the session
set. Today the store is shared by all scenarios (audit section
5: no SDC-to-scenario binding on the EOS surface), so the single
file loses nothing that exists. Once per-scenario constraint
state exists, a merge would lose the per-scenario corner/mode
binding; per-scenario files then follow the MMMC view model
(Genus 18.1 ch. 14, Tempus 22.10 ch. 5: per-mode SDC is the
MMMC carrier). That form is not built yet, and there is no
`-scene` option in this increment: it would imply per-scenario
content that does not exist.

## Non-goals

No `-scene` option, no per-scenario files, no re-emission of
file-origin constraints, no SDC authoring commands (that was
I6, unit 3).
