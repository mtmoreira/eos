# Waivers in the EOS registry (waiver-model unit)

`eos::set_waiver`, `eos::list_waivers`, `eos::remove_waiver`,
and `eos::waiver_coverage` are the waiver commands: named
approvals for named violations. A waiver is a reporting
disposition only. It does not change what the engine checks,
what slack is computed, or what TNS is. It is applied at
report time, over violations the engine just computed: the
worst violator is the worst unwaived one, a waived violation
stays in every listing marked with its waiver name, and
totals always show both numbers, unwaived and waived.

```text
set_waiver -kind <timing|check|coverage> -class <class> -target <endpoint|pin>
    [-scene <name>] [-expires <YYYY-MM-DD>] -reason <text>
    -approved_by <name> -name <name>
list_waivers
remove_waiver <name>
waiver_coverage
```

## The record

A waiver is project state, stored in the `db/waivers`
`WaiverSet` on the project and serialized as the fourth
section of the project file (`EOSWAIVERS 1`, after the
report-bindings section). A project file with no waivers
section loads with an empty waiver set. `close_design`
clears the set with the rest of the project, and `restore`
reports `waivers_restored=<n>`.

| Field | Meaning |
|---|---|
| `id` | A `WaiverId` from `next_id`, never reused. |
| `name` | Unique in the set. Duplicates fail fast at creation; a change means `remove_waiver` then `set_waiver`. |
| `kind` | `timing`, `check`, or `coverage`. `path` follows after `get_timing_paths` lands and is not in v1. |
| `class` | A `report_constraint` class for `timing`; a `check_timing` check name for `check`; a coverage check type for `coverage`. An unknown class fails fast. |
| `target` | The string exactly as written, matched by name at report time. A target naming nothing is retained and shows as unmatched, never dropped. |
| `scene` | Empty means all scenes. A named scene must exist in the scenario set at creation or creation fails fast. |
| `reason` | Required; empty fails fast. |
| `approved_by` | Required, free text, recorded not enforced. |
| `expires` | Optional `YYYY-MM-DD`. Past its date the waiver matches nothing and reports `expired`. A malformed date fails fast at creation. Compared against the system date in UTC. |
| `created_revision` | The ledger revision at creation. |

Status is derived, never stored: `active` when the waiver
matches at least one current violation, `unmatched` when it
matches nothing, `expired` past its date.

## Matching

Matching is exact on kind, class, target, and scene. There
are no wildcards in v1; globs are a later version. One waiver
covers one identity; covering N endpoints means N waivers.
A waiver is not a constraint, not a false path, and not
requirement coverage.

What counts as a violation depends on the kind:

- `timing`: a violating endpoint or DRC pin in a timing
  report, identified by its class (`max_delay`, `min_delay`,
  `max_transition`, `max_capacitance`, `max_fanout`), its
  pin, and its scene.
- `check`: a `check_timing` finding, identified by its check
  name and the object the finding names (the first token of
  the finding string: a port, pin, or constraint name).
  Check findings carry no scene, so only a scene-less check
  waiver matches them.
- `coverage`: an untested coverage row, identified by its
  check type, its pin, and its scene. A waived untested row
  is still untested.

## The commands

`set_waiver` needs an open design. It returns
`waiver=<id> name=<name> status=unmatched matches=0`: a new
waiver has matched nothing yet, because matching is computed
at list and report time against current violations.

`list_waivers` lists one line per waiver in creation order:

```text
waiver=<id> name=<name> kind=<kind> class=<class> target=<target>
  scene=<scene or all> status=<active|unmatched|expired> matches=<n>
  reason=<reason> approved_by=<name> expires=<date or ->
  created_revision=<n> evaluated=<run|none>
```

Matching is computed at list time against the most recent
report inputs the session holds: timing waivers against the
most recent `run_timing` results (`evaluated=run` when a run
exists, else `none`), check waivers by re-running the check
runners fresh over the current design and constraints
(`evaluated=run` when a design is open), coverage waivers by
running the coverage analysis fresh, but only when at least
one coverage waiver exists, because that is a full analysis
per scene. An empty set reports `waivers=0`, never an error.

`remove_waiver <name>` drops one waiver by name. An unknown
name fails fast naming the existing names. It returns
`removed=<name> waivers=<remaining>`.

`waiver_coverage` returns `total=<n> active=<n> unmatched=<n>
expired=<n>`. There is no `complete=` term: a waiver set is
never complete, it is only accounted for.

## Report integration

The same three behaviors apply everywhere waivers apply:

1. The worst violator is the worst unwaived one.
2. Waived violations appear in a separate "Waived violations"
   section with their waiver names, never dropped.
3. A footer line names unmatched waivers that could apply to
   that report (`waivers unmatched: <names or none>` in text
   reports, `waivers_unmatched=<names or none>` in
   payload-only reports).

`report_constraint`: per-class rows gain `waived=<n>`; class
cost is the unwaived cost, with `waived_cost=` per class;
critical-range tables list unwaived violators worst first; a
Waived section per class lists waived endpoints with slack and
waiver name.

`report_qor`: per-group WNS, TNS, and violating-path counts
are the unwaived numbers; each group row gains a waived
count; DRC summary rows gain waived counts the same way;
payload lines gain `waived=<n>` per group and per DRC check.
The union TNS is the unwaived TNS.

`timing_summary`: violator rows gain `waived=<n>`; path rows
gain `waived=<name or ->`, only when the path violates.

`report_analysis_coverage`: the summary keeps tested and
untested counts (a waived untested row is still untested);
each summary row gains a waived count of untested rows
covered by a waiver, and detail rows gain
`waived=<name or ->`; `-exclude_untested` keeps its existing
meaning.

`check_timing`: findings gain a waived marker under
`-verbose` (`finding=<check> <object> <reason>
waived=<name or ->`); `check=` lines gain `waived=<n>`;
the verdict is `pass` with the waived count named when
every finding is waived, `fail` when any finding is
unwaived, `pass` when nothing is found. Per-check `status=`
stays raw (fail iff findings non-empty) and `findings=`
stays the total count. There is no fourth verdict.

`write_sdc` is unaffected: its output is byte-identical
with and without waivers in the session.

Out of scope: `check_constraints` (a defined follow-up),
`report_exceptions` (a separate follow-up), the `path`
waiver kind (a follow-up after `get_timing_paths`), and any
GUI work (the Timing panel waived column is a follow-up).
