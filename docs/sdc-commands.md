# SDC commands in the EOS registry (Immediate unit 3)

This page documents the SDC command family added to the EOS shell
(`eos::` commands, `src/ui/registry/src/core_commands.cc`) and the
migration that came with it. It covers registry-authored constraints:
commands that store real constraint rows in the session, visible to
`list_constraints`, `constraint_coverage`, `rebind_constraints`, and
checkpoints. It does not cover the SDC file parser used by
`read_sdc` and `run_timing`; see `docs/read-sdc-audit.md` for that
side.

## Migration: the construction record is now `bind_clock`

Before Immediate unit 3, `eos::create_clock` was a design
construction record: `create_clock <name> <target>`, with no period
and no waveform. That name collided with SDC `create_clock`, and the
owner decided (2026-11-01, review item B18) on rename with
migration. Overloading one name with two semantics was rejected: it
breaks fail-fast clarity and invites silent misparsing.

The construction record is now:

```
bind_clock <name> <target>
```

Same handler, same payload shape (`constraint=<id> status=<word>
target=<target>`, plus `object=<id>` when bound), same binding rule
(port, then net, then instance, else a pin path
`<instance>/<pin>`). Every in-repo use (registry tests, the phase 1
exit test) moved to `bind_clock`.

The old form does not silently keep working. Calling
`create_clock` with exactly two bare positional arguments and no
option flags fails fast with a message stating that `create_clock`
is now the SDC command and the construction record moved to
`bind_clock`. Any other non-SDC shape fails on the SDC option rules
below (missing `-period` first).

## Shared rules

All commands in this family follow the same discipline:

- Unknown options fail; nothing is silently ignored. Duplicate
  non-repeatable options fail. A missing option value fails.
- A value is the next token unless it is exactly a known flag, so
  negative numbers (`-0.5`) parse as values while a missing value
  before the next flag is an error.
- Targets accept a bare object name, a pin path
  (`<instance>/<pin>`), or a single-object `get_ports` / `get_pins` /
  `get_nets` / `get_cells` wrapper passed as one quoted argument
  (`[get_ports clk]`). A wrapper naming several objects fails and
  directs the user to pass objects as separate arguments. Targets
  are normalized before storing, so the stored target is the object
  name and binds.
- One stored row per target object, so each target's binding status
  is reported independently and coverage counts each row.
- When the user gives `-name`, it is used verbatim on every row of
  that call. When absent, rows auto-name as
  `<kindword>_<target>`, except single-target `create_clock` and
  `create_generated_clock`, which default the name to the target.
- Payloads keep the established per-constraint shape, plus
  `params=k=v;k=v` when the row carries semantic parameters, one
  line per constraint.
- Commands with no targets fail fast in this increment. There is no
  all-objects default: the store's empty status means "addresses an
  empty collection, never treated as covering anything", so an empty
  spec must not stand in for "all". This is a documented gap, not a
  silent default.

## `create_clock`

```
create_clock -period <ns> [-waveform <rise> <fall>]
  [-name <name>] [-add] <target>...
```

Stores a `clock` constraint per target with `period_ns`, and
`waveform_rise_ns` / `waveform_fall_ns` when `-waveform` is given.
Rules: `-period` is required and must be positive; `-waveform`
needs `0 <= rise < fall <= period`; several targets require
`-name`. Redefining a clock name replaces existing SDC clock rows
of that name, unless `-add` is given. Replacement is scoped to SDC
clock rows: a `bind_clock` construction record sharing the name is
untouched. A single target without `-name` defaults the name to
the target; a target list without `-name` fails.

## `create_generated_clock`

```
create_generated_clock -source <pin> [-master_clock <name>]
  [-divide_by <n>] [-multiply_by <n>] [-invert] [-add]
  [-combinational] [-name <name>] <target>...
```

Stores a `generated_clock` constraint per target with `source`
(the normalized master source pin), `master_clock`,
`divide_by` / `multiply_by`, `invert=1` when `-invert` is given,
and `combinational=1` when `-combinational` is given. Rules:
`-source` is required; `-master_clock` is required unless
`-combinational` is given; without `-combinational`, at least one
of `-divide_by` / `-multiply_by` is required, each a positive
integer. `-name` follows the same default and requirement rule as
`create_clock`, and replacement follows the same `-add` rule.
`-edges` is not supported in this increment; passing it fails as
an unknown option.

## `set_clock_gating_check`

```
set_clock_gating_check [-setup <ns>] [-hold <ns>]
  [-high | -low] <target>...
```

Stores a `clock_gating_check` constraint per target with
`setup_ns` / `hold_ns` as given and `edge` (`high`, `low`, or
`both` when neither flag is given). At least one of `-setup` /
`-hold` is required; values may be any finite number; `-high` and
`-low` together fail.

## `set_data_check`

```
set_data_check -from <obj> [-from <obj>...] -to <obj>
  [-to <obj>...] [-setup <ns>] [-hold <ns>]
```

Stores one `data_check` row per (`from`, `to`) pair, named
`data_check_<from>_to_<to>`, bound to the `to` endpoint, with
`from` and `setup_ns` / `hold_ns` as parameters. `-from` and `-to`
are both required and repeatable; at least one of `-setup` /
`-hold` is required; positional arguments fail. Gap: the `from`
endpoint is recorded, not bound, in this increment. A later unit
can bind it; until then, binding reflects the `to` endpoint only.

## `set_min_pulse_width`

```
set_min_pulse_width <ns> [-high | -low] <target>...
```

Stores a `min_pulse_width` constraint per target with `value_ns`
(required, positive) and `edge` (`high`, `low`, or `both`).
`-high` and `-low` together fail.

## `set_sense`

```
set_sense (-positive | -negative | -stop_propagation)
  [-clock <name>...] <pin>...
```

Stores a `sense` constraint per pin with `sense` and, when given,
`sense_clocks` holding the space-joined clock names. Exactly one of the three sense
flags is required. Gap: `-clock` names are recorded, not bound, in
this increment.

## `set_ideal_network`

```
set_ideal_network [-no_propagate] <target>...
```

Stores an `ideal_network` constraint per target, with
`no_propagate=1` when `-no_propagate` is given.

## `set_ideal_latency` and `set_ideal_transition`

```
set_ideal_latency <ns> [-rise | -fall] [-min | -max] <target>...
set_ideal_transition <ns> [-rise | -fall] [-min | -max] <target>...
```

Store an `ideal_latency` / `ideal_transition` constraint per
target with `value_ns` (required, not negative), `edge` (`rise`,
`fall`, or `both` when neither or both flags are given), and
`delay` (`min`, `max`, or `both` on the same rule). Giving both
`-rise` and `-fall` (or both `-min` and `-max`) explicitly means
both, the same as giving neither.

## `group_path`

```
group_path -name <group> [-from <obj>...] [-to <obj>...]
  [-through <obj>...] [-weight <w>] [-critical_range <ns>]
```

Stores one `group_path` row per endpoint, all named with the group
name, with `group`, `from` (space-joined), `through`
(space-joined), `weight`, and `critical_range` as parameters.
`-name` is required; at least one of `-from` / `-to` / `-through`
is required; `-critical_range` must not be negative; positional
arguments fail. Rows bind to the endpoints: `-to` first, else
`-through`, else `-from`. The other lists are recorded, not bound.
Redefining a group name replaces that group's rows; the name is
the identity, consistent with clocks.

## `set_max_time_borrow`

```
set_max_time_borrow <ns> <target>...
```

Stores a `max_time_borrow` constraint per target with `value_ns`
(required, not negative).

## Boundary: these constraints do not feed `run_timing` yet

`run_timing` reads its SDC from the file recorded by `read_sdc`,
and the timing service requires that path; it does not read the
session constraint set. Registry-authored clocks are therefore
session constraints visible to constraint reporting and to future
check commands, but STA via `run_timing` still needs `read_sdc`
until a later unit wires session constraints into the timing
service. Rewiring the timing service in this unit was out of scope:
it is a cross-layer change, it would alter `run_timing`'s
fail-fast "no SDC: `eos::read_sdc` first" contract, and it would
need temp-file SDC materialization or engine text-input support.
`run_timing`'s contract is unchanged by this unit.

## Storage notes

- Constraint rows carry ordered `params` (key/value strings,
  insertion order preserved); parameters never affect binding,
  which still runs on the target spec alone.
- Checkpoints write parameters as trailing
  `param <key> <value>` triples on the constraint line, with the
  existing percent escaping (so values containing spaces survive).
  Lines without parameters keep the old seven-token shape, and
  old checkpoint files still load. The checkpoint container version
  is unchanged.
- New constraint kinds: `clock_gating_check`, `data_check`,
  `min_pulse_width`, `sense`, `ideal_network`, `ideal_latency`,
  `ideal_transition`, `group_path`, `max_time_borrow`,
  `generated_clock`. All are usable through the generic
  `add_constraint` / `list_constraints` kind words.
- Registry count pin: 55 to 66 (bind_clock keeps the renamed slot;
  plus `create_clock`, `create_generated_clock`, and the nine M27
  commands).
