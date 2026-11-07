# `report_analysis_coverage` in the EOS registry (Immediate unit 7)

`eos::report_analysis_coverage` is the coverage report, review
item I4: the first thing a signoff review asks for after the
constraints are known, and EOS previously could only show that
a constraint record existed (`constraint_coverage` emits
binding counts only), never whether the timing engine actually
tested each constrained pin. It reports, per check type, how
many constrained pins the engine tested and how many it left
untested, with percents, plus the per-pin detail form: the
constrained pin, the related pin, the check type, and the
untested reason. The shape follows PrimeTime T-2022.03
ch. 18/19: tested/untested counts and percents per check type,
and the untested reason per pin.

```text
report_analysis_coverage [-status_details tested|untested] [-check_type <type>] [-exclude_untested] [-scene <name>] [-file <path>]
```

It runs the timing analysis itself for the selected scenes:
it does not reuse a previous `run_timing`, because the session
keeps no live engine. It needs an active scenario, a Liberty
for the scenario's corner, a design source
(`read_verilog` or `import_odb`) and an SDC file, exactly as
`run_timing` and `report_constraint` do. One report binding
per scene is registered through `session/reports`, named
`coverage_report_<scene>`. It builds on `check_timing` (I1):
the coverage universe is the set of checks the engine
defines, and the tested/untested verdict comes from the
engine's own endpoint enumeration.

## The check types

The check types EOS can express and the engine evaluates:

| Check type | Meaning |
|---|---|
| `setup` | Setup check on a register or latch data pin (latch and non-sequential setup fold in). |
| `hold` | Hold check on a register or latch data pin (latch and non-sequential hold fold in). |
| `out_setup` | Output setup check: a `set_output_delay -max` on a port. |
| `out_hold` | Output hold check: a `set_output_delay -min` on a port. |
| `recovery` | Recovery check on an asynchronous reset or set pin. |
| `removal` | Removal check on an asynchronous reset or set pin. |
| `clock_gating_setup` | Clock-gating setup check on a gated-clock enable pin, when `set_clock_gating_check` is in force. |
| `clock_gating_hold` | Clock-gating hold check on a gated-clock enable pin. |
| `no-change` | Data check: a `set_data_check` between two pins. |
| `min_pulse_width` | Minimum pulse width check on a clock pin, from `set_min_pulse_width`, SDF annotation, or the Liberty library. |

The check types EOS cannot express are never reported as
untested: reporting a check as untested when EOS cannot
express it would be a false verdict. They are named on the
"no checks defined" line instead, and the reasons are:

| Check type | Why it is not counted |
|---|---|
| `min_period` | No `set_min_period` command exists in EOS. |
| `max_skew` | No `set_max_skew` command exists in EOS. |
| `clock_separation` | `set_clock_groups` exists, but the engine has no clock-separation evaluation. |

A check type with no defined checks in a scene is listed on
that scene's "No checks defined:" line, in the fixed type
order above. A type with at least one defined check gets a
summary row.

## Tested and untested

The universe per type is the set of pins where the check is
defined: a check arc into the pin exists for the path-end
types, a data check targets the pin for `no-change`, and a
minimum pulse width constraint exists for the pin for
`min_pulse_width`. Tested means a constrained path end (or
a minimum pulse width check) exists for the pin. The related
pin is the from-pin of the check arc: the clock pin for
setup, hold, recovery, removal, clock-gating and no-change
checks; for `out_setup` and `out_hold` the output-delay
reference or clock pin; `-` for `min_pulse_width` and for a
path-delay endpoint, which has no check edge.

Untested reasons, in precedence order:

| Reason | Meaning |
|---|---|
| `constant_disabled` | The pin is constant, or its check is disabled by constraint. |
| `no_clock` | The check's clock is absent: the related clock pin carries no clock for setup, hold, recovery, removal and clock-gating; the output delay has no clock and no reference pin for `out_setup`/`out_hold`; the design has no clock at all for `no-change`; no clocked path reaches the pin for `min_pulse_width`. |
| `no_startpoint_clock` | The clock is present, and timing paths reach the pin, but none of them is clocked: no clocked startpoint reaches the endpoint. |
| `no_paths` | The check and its clock are present, but no constrained path end was visited for the pin. |

A tested row shows `-` as its untested reason, in the text
and in the payload.

## The summary table

One row per check type with at least one defined check, in
the fixed type order:

| Column | Meaning |
|---|---|
| `Check Type` | The check type. |
| `Tested` | The count of tested pins. |
| `Untested` | The count of untested pins. |
| `Tested %` | Tested as a percent of defined checks, one decimal. |

Example, a two-flop design where the first flop is fed from
an unclocked input and the second from the first flop:

```text
Check Type  Tested  Untested  Tested %
----------  ------  --------  --------
setup       1       1         50.0
hold        1       1         50.0
----------
No checks defined: out_setup, out_hold, recovery, removal, clock_gating_setup, clock_gating_hold, no-change, min_pulse_width.
```

## The detail table

One row per defined check, in pin then check-type order:

| Column | Meaning |
|---|---|
| `Constrained Pin` | The pin the check constrains. |
| `Related Pin` | The from-pin of the check arc, or `-`. |
| `Check Type` | The check type. |
| `Untested Reason` | The reason, or `-` when the check is tested. |

Example, the same design:

```text
Constrained Pin  Related Pin  Check Type  Untested Reason
---------------  -----------  ----------  -----------------
r1/D             r1/CK        hold        no_startpoint_clock
r1/D             r1/CK        setup       no_startpoint_clock
r2/D             r2/CK        hold        -
r2/D             r2/CK        setup       -
---------------
```

## The filters

`-status_details tested` shows only the tested detail rows;
`-status_details untested` shows only the untested detail
rows. `-check_type <type>` shows only the detail rows of
that type. `-exclude_untested` drops the untested rows from
the detail table. All three filters act on the detail table
only: the summary table still counts every defined check,
and the payload summary lines do too. An unknown
`-status_details` value or an unknown `-check_type` fails
fast, naming the valid values.

## The payload

The payload is machine-readable key/value lines, then the
rendered report text. Per scene: `scene=<name>
checks=<total defined> tested=<n> untested=<n>
report=coverage_report_<name> artifact=<id>`, plus one line
per summary row: `scene=<name> type=<type> tested=<n>
untested=<n> percent=<pct>`, plus one line per shown detail
row: `scene=<name> pin=<pin> related=<related>
type=<type> status=tested|untested reason=<reason or ->`.
`-file` writes the rendered text to the file and adds a
`file=<path>` line.

## Boundary

- `report_analysis_coverage` reports the ten check types
  above. It does not report `min_period`, `max_skew` or
  `clock_separation` as untested: EOS cannot express them,
  and the reasons are listed above.
- It does not include multi-scenario coverage: that needs
  the MMMC decision, which is not made yet.
- A `set_max_delay`/`set_min_delay` endpoint with no timing
  path at all is not counted in the universe: the engine
  itself has no endpoint there. The common case, an endpoint
  with paths, is covered: the endpoint is discovered through
  the engine's path-end visit and reported as defined and
  tested.
- It does not fix untested checks: adding the missing clock
  or constraint is the designer's step.
- Unknown options and positional arguments fail fast. An
  unknown `-scene` fails fast.
- Registry count pin: 75 to 76.
