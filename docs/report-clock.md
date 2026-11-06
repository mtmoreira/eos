# `report_clock` in the EOS registry (Immediate unit 5)

`eos::report_clock` is the clock inventory, review item I2: the
first thing a signoff review asks for, and EOS previously could
not show a single clock period. It reports the clocks the user
defined with `eos::` commands, in the PrimeTime-adjacent shape
from Timing Constraints U-2022.12 ch. 2: the Clock, Period,
Waveform, Attrs, Sources table plus the generated-clock table,
and `-skew` for uncertainty.

```text
report_clock [-skew]
```

It runs over the session constraint set and needs nothing else:
no Liberty, no SDC file, and no STA run. It never mutates
session state.

## The main table

One row per clock definition, sorted by name. Rows that render
identically merge (one clock defined on several sources lists
them all); rows that differ in any displayed value stay
separate, so a name stacked with `-add` under two periods is
never collapsed into one number.

| Column | Meaning |
|---|---|
| `Clock` | The clock name, as given to `create_clock` or `create_generated_clock` (or the target, when no `-name` was given). |
| `Period` | The period in nanoseconds. For an SDC clock, the stored `-period`. For a generated clock, the period derived from its master (see below), or `-` when it cannot be derived. |
| `Waveform` | The `{rise fall}` pair in nanoseconds. For an SDC clock, the stored `-waveform`, or the SDC default `{0 period/2}` when no `-waveform` was given. For a generated clock, the derived waveform (see below), or `-` when it cannot be derived. |
| `Attrs` | Attribute letters, listed in the legend above the table. `G` is the vendor letter for a generated clock. `u` is EOS-only: the clock has a row whose target never bound. The vendor letters `p`, `d`, and `f` never appear: EOS has no `set_propagated_clock`, `dont_touch_network`, or `fix_hold` command. |
| `Sources` | The source objects of the clock, in braces: the targets the clock was defined on. |

Example, a design with one SDC clock and one generated clock:

```text
Attributes:
     G - generated_clock

Clock        Period  Waveform  Attrs  Sources
------------------------------------------------------------
SYS_CLK      10      {0 5}            {clk}
DIV2         20      {0 10}    G      {u_div/Q}
------------------------------------------------------------
```

## The generated-clock table

One row per generated-clock definition, sorted by name, with
the derivation spelled out:

| Column | Meaning |
|---|---|
| `Generated Clock` | The generated clock name. |
| `Master Source` | The source objects of the master clock, or `-` when the master is not defined. |
| `Generated Source` | The objects the generated clock is defined on, in braces. |
| `Master Clock` | The `-master_clock` name, or `-` for a combinational generated clock, which has no master. |
| `Waveform Modification` | The derivation: `divide_by(n)`, `multiply_by(n)`, `invert`, `combinational`, in that order, as they apply. |

## Derived periods and waveforms

A generated clock has no stored period: its period and waveform
are derived from its master, by pure arithmetic on stored data.

- Period: master period times `divide_by` over `multiply_by`
  (each 1 when absent). A combinational generated clock keeps
  the master period.
- Waveform: the master waveform scaled by the same factor. An
  inverted clock moves the high interval `[rise, fall)` to
  `[fall, rise + period)`, which is one `{rise fall}` pair only
  when the master rise is 0 and the master fall is below the
  period. The SDC default waveform `{0 period/2}` satisfies
  this, so the common inverted case derives; a non-default
  master waveform that does not satisfy it renders as `-`.
- The derivation follows master chains: a generated clock whose
  master is itself a generated clock derives from the chain.
  A master cycle yields `-`, never a loop.
- When the master is not defined, or is a construction record
  (`bind_clock`, which carries no period), the period and
  waveform are `-`. The definition is still listed: reporting a
  broken definition is this command's job, and flagging it is
  `check_timing`'s (`generated_clocks` check).

## `-skew`

`-skew` adds a `Skew` column reporting the stored clock
uncertainty of each clock, following the manual's rule that
`-skew` reports the uncertainty of the clocks. EOS has no
`set_clock_uncertainty` command yet, so the value is `none`
for every clock today, and the report says so in a footer
line naming the missing command:

```text
skew: no clock uncertainty is defined for any clock (set_clock_uncertainty is not an EOS command yet)
```

When `set_clock_uncertainty` lands, the column populates
without a change to this command's interface.

## Construction records

A `bind_clock` record carries no period, so it cannot appear
in the main table. It is not hidden either: construction
records are listed in their own section after the tables, with
name and target. A design with only construction records and
no SDC or generated clocks reports just that section.

## Empty state

A design with no clocks at all reports `clocks=0`.

## Boundary

- `report_clock` reports clocks defined with `eos::`
  commands. It does not read the SDC file recorded by
  `read_sdc`: that path stores the file path only, so clocks
  defined in an SDC file do not appear. The session-constraint
  boundary is the same as for `check_timing`; see
  `docs/sdc-commands.md`.
- `create_generated_clock` supports `-divide_by`,
  `-multiply_by`, `-invert`, and `-combinational`. It does not
  support `-edges`: that option fails fast as unknown, and a
  generated clock defined by edges cannot be expressed in EOS
  yet.
- There is no pattern argument: selecting clocks by pattern
  needs `get_clocks`, which is a collections item (C1), not
  part of I2. `report_clock` always reports every clock.
- `report_clock` does not include `report_clock_timing`
  (clock-network summary, latency, transition, verbose clock
  paths). That is a separate item, listed under clock-network
  reporting in the review.
- Unknown options and positional arguments fail fast.
  `-skew` given twice fails fast.
- Registry count pin: 72 to 73.
