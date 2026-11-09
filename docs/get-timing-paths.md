# `get_timing_paths` in the EOS registry (Immediate unit 12)

`eos::get_timing_paths` returns timing paths as a collection,
review item I8: the per-endpoint path enumeration behind
`report_timing`, exposed so paths can be filtered, sorted,
traversed and fed to other reports. `report_timing` prints one
worst path per endpoint; `get_timing_paths` returns up to
`-nworst` paths per endpoint as path members of a new
collection, one scene at a time, worst first.

```text
get_timing_paths [-delay_type max|min] [-max_paths N]
  [-nworst N] [-from <object>] [-through <object>]...
  [-to <pin>] [-slack_less_than X] [-group <name>]
  [-scene <name>] [-filter <expr>]
```

`get_timing_paths` runs the timing analysis itself for the
selected scenes: it does not reuse a previous `run_timing`,
because the session keeps no live engine. It needs an active
scenario, a Liberty for the scenario's corner, a design
source (`read_verilog` or `import_odb`) and an SDC file,
exactly as `run_timing` and `report_timing` do. One full STA
run happens per scene; the cost is the same as `report_timing`.

The payload is `collection=eos_coll_N count=M`, plus one
`scene=<name> paths=<count>` line per scene that contributed
paths. There is no text-table rendering of paths here: the
paths live in the collection.

## Options

| Option | Meaning |
|---|---|
| `-delay_type max|min` | Setup (`max`, the default) or hold (`min`) paths. |
| `-max_paths N` | At most N paths in total when no `-to` is given, worst first. Default 1. With `-to` the endpoint branch returns up to `-nworst` paths for that endpoint; `-max_paths` does not cap it. |
| `-nworst N` | Up to N worst paths per endpoint, worst first. Default 1. |
| `-from <object>` | Only paths starting at the named pin, net, instance or clock. A single value, which may name a collection holding many objects. A clock is a startpoint: paths launched by that clock. |
| `-through <object>` | Only paths passing through the named pin, net or instance. Repeatable; several `-through` values combine as AND. A clock is not a through object and fails fast naming it. |
| `-to <pin>` | Only paths ending at the named pin. |
| `-slack_less_than X` | Only paths with slack below X ns. |
| `-group <name>` | Only endpoints in the named path group (the `report_constraint` pattern: the group is resolved against the scene's groups first). An unknown group fails fast naming the available groups. |
| `-scene <name>` | Only the named scenario. An unknown scene fails fast. |
| `-filter <expr>` | A collection filter over the path attributes, applied after the analysis (for example `-filter {slack < 0}`). |

`-from` and `-through` combine with `-to`, `-group` and
`-slack_less_than` as AND. A `-from` or `-through` value
naming an object with no timing path yields an empty
collection, never an error; a value naming no object at all
fails fast naming it. An endpoint with no constrained path
end fails fast, as with `report_timing`. Names resolve
against the loaded network in this order: pin, then net,
then instance, then clock (for `-from` only). A clock whose
name also names a pin, net or instance resolves to that
object, not to the clock; a from-clock must be named
differently from every pin it could collide with.

## The path class

Each member is a path: the scene, the check kind
(`setup`/`hold`), the endpoint, the rank (1-based within the
endpoint for that scene, 1 the worst) and the full stored
path report. The display name is `scene:endpoint#rank`, for
example `func_tt:r2/D#1`. The member key for dedup and
algebra is `path:<scene>:<kind>:<endpoint>#<rank>`, so a
setup path and a hold path over one endpoint share a display
name but are distinct members and both survive a merge.

| Attribute | Meaning |
|---|---|
| `name`, `full_name` | The display name. |
| `object_class` | `path`. |
| `endpoint` | The endpoint pin. |
| `startpoint` | The startpoint pin. |
| `slack`, `arrival`, `required` | Numbers in ns. |
| `delay_type` | `max` or `min`. |
| `scene` | The contributing scenario. |
| `group` | The path group, empty when the endpoint is in no named group. |
| `points` | The number of pins on the path. |
| `rank` | The rank within the endpoint, 1 the worst. |

Intrinsic path attributes are read-only, as with every
class. User attributes set with `set_attribute` round-trip
on the path member, keyed by the member key.

Collection operations work over path collections:
`get_pins -of_objects` returns every point pin of the
paths, `get_nets` returns the nets of those pins,
`get_cells` returns the owning instances, `get_clocks`
returns the clock named by the path's group when the group
name is a clock, and `get_ports -of_objects` fails fast.
`sort_collection`, `filter_collection`, `index_collection`
and `foreach_in_collection` work on the path attributes; a
path is not an SDC target, so passing a path handle where a
design object is needed fails fast naming the reason.
Collections are snapshots: a later `run_timing` does not
refresh a path member. Path collections follow the existing
lifetime rules: cleared by `restore` and `close_design`,
surviving `rollback` and `abort_transaction`.

## `report_bottleneck -paths`

`report_bottleneck` gains one option, `-paths <collection>`,
which reads a path collection instead of the capture.
Without `-paths` the capture precondition stands unchanged:
it still needs a `run_timing` with the capture on. With
`-paths` the ranking is built from the collection's members
of the requested kind, `-scene` resolves against the
collection's scenes, and the ranking text names the
collection. A collection holding non-path members, or an
unknown handle, fails fast.

## Not in this unit

`report_timing` over a path collection is the defined
follow-up and is not built here: `report_timing` still
reports the worst path per endpoint from the analysis, and
`-to` with `-max_paths` above 1 still fails fast there.
`report_delay_calculation` and `get_objects path` are also
out of scope, as is any GUI work.
