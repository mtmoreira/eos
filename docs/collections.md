# Collections (Immediate unit 11, review items C1-C3)

EOS collections are the cross-command contract for naming design
objects and clocks (review ruling O6): handles as arguments, a
-filter language, algebra, lifetime and invalidation rules, and a
documented per-class attribute set. The Synopsys model is adopted
(PrimeTime-adjacent names and semantics) over the Cadence
find-plus-handle model. C4, path collections as report inputs, is
deliberately not built here: it waits on get_timing_paths (I8).

## Vocabulary (C1)

A collection is a named handle, `eos_coll_N`, over design objects
and clocks. Handles are never reused within a session. Membership
is a snapshot taken at creation: objects created after a collection
was minted are not added to it. Every collection is stored in
deterministic order (display name, clocks after design objects on
a name tie, then raw id), so index, compare, and iteration mean the
same thing every run. The one exception is an attribute sort:
`sort_collection` returns a collection in attribute order (ties
keep stored order), and `filter_collection` keeps its input order,
so a sort survives a filter.

| Command | Members |
|---|---|
| `get_cells [pattern]` | Instances. PrimeTime semantics: in EOS, as in the SDC layer, an instance is addressed as `[get_cells <name>]`. Library-cell masters are queried with `get_objects cell`. |
| `get_nets [pattern]` | Nets. |
| `get_pins [pattern]` | Instance pins, named `<instance>/<pin>`. |
| `get_ports [pattern]` | Ports. |
| `get_clocks [pattern]` | Clocks by name: SDC clocks and generated clocks. Construction records (`bind_clock`, no period) are not clocks and are excluded. |
| `all_inputs` | Ports with direction `in`. |
| `all_outputs` | Ports with direction `out`. Inout ports are in neither. |
| `all_registers` | Instances whose master cell is a sequential cell (see below). |
| `all_clocks` | Every clock. |
| `get_objects <kind> <pattern>` | The generic alias, kinds `cell`, `instance`, `net`, `port`, `pin`, `clock`. |

All `get_*` commands take an optional glob pattern (default `*`),
`-filter <expr>`, and `-of_objects <collection>` (see traversal).
An empty result is an empty collection, never an error. A pattern
may not start with `-` (it would read as an option). The `-filter`
value is one argument: braced in Tcl, double-quoted in the GUI
console.

`all_registers` recognizes Yosys sequential cell types: the generic
`$dff`, `$adff`, `$aldff`, `$sdff`, `$dffe`, `$adffe`, `$aldffe`,
`$sdffe`, `$dffsr`, `$adffsr`, `$dlatch`, `$dlatchsr`, `$sr`, and
the tech-mapped `$_DFF_*`, `$_DLATCH_*`, `$_SR_*` families.
Liberty DFF cells named by hand are not classified: the rule is a
closed name set, never a substring heuristic, so it cannot misfire.

Collections enumerate the project design (`db::Design`). The
synthesis engine session holds no persistent object IDs, so it
cannot back live handles; that boundary is documented, not bridged.

Listing a collection resolves names at call time. A member whose
object no longer exists reads as `<deleted>`.

## Handles as arguments

A token that is exactly a collection handle expands, wherever an
SDC command takes targets, to its members' addressable names:
design object names, and for a clock member every target of the
clock. Example: `create_clock -period 8 -name bus_clk eos_coll_1`
stores one clock row per member. An unbound clock, or a deleted
member, fails the whole command naming the handle. A token shaped
like a handle (`eos_coll_` followed by digits) that was never
minted fails fast naming it: the handle namespace is reserved and
never reads as a plain design-object name. A single-object
option such as `create_generated_clock -source` requires the
handle to hold exactly one member.

## Filter language (C2)

`-filter <expr>` on the `get_*` commands, and
`filter_collection <collection> <expr>` (the expression is the
rest of the line, so a console caller may write it unquoted and a
Tcl caller braced).

Grammar: `or` of `and`s of terms; a term is a parenthesised
expression or a comparison `attribute op value`. Operators: `==`,
`!=`, `<`, `<=`, `>`, `>=`, `=~`, `!~`. Values are bare tokens or
double-quoted strings. Numeric compare applies when both sides
parse as numbers, else string compare for `==`, `!=`, `=~`, `!~`;
ordering operators on non-numbers fail fast. `=~` and `!~` use
glob matching (see escaping below).

In a filter, an attribute that is neither intrinsic for the
member's class nor ever set as a user attribute on that member
reads as the empty string, so `dont_touch == ""` matches untagged
objects. An attribute name known nowhere (not intrinsic to any
class, never set) fails fast, so a typo fails instead of matching
nothing. A filter syntax error fails fast naming the expression.

Abbreviation stays out: an abbreviated attribute name is an
unknown attribute (fail-fast conflict).

## Traversal (`-of_objects`)

`get_cells`, `get_nets`, `get_pins`, and `get_clocks` accept
`-of_objects <collection>`:

- `get_pins`: from instances (their pins), nets (pins on them),
  pins (themselves).
- `get_nets`: from pins (their nets), instances (nets of their
  pins), nets (themselves).
- `get_cells`: from pins (owning instances), nets (instances with
  pins on them), instances (themselves).
- `get_clocks`: from ports, nets, pins (clocks bound to those
  objects). A generated clock is bound to its target, not to its
  source: traversing from the source port returns the master clock
  only, traversing from the target port returns the generated
  clock.

Any other source kind fails the whole call naming the member.
`get_ports` takes no `-of_objects`: ports have no fanin or fanout
in the flat model, and the flag fails fast saying so.

## Bus and bracket escaping

`[` and `]` are literal in patterns: the glob language has no
character-class syntax, so `get_nets data[0]` matches only the net
`data[0]`, while `get_nets data[*]` spans the brackets. Backslash
escapes the next character literally: `get_nets star\*` matches a
net literally named `star*`.

## Attributes (C2, A9)

`get_attribute <object> <attribute>` and
`set_attribute <object> <attribute> <value>`. The object is a
design object name (resolved ports, then nets, then instances,
then pin paths, the constraint-store order), a clock name, or a
singleton collection handle (a handle holding anything but one
member fails fast with its count). A name that denotes both a
design object and a clock addresses the design object; read the
clock through its singleton handle (`get_clocks <name>` first).
Payload: `object=<name> attribute=<attr> value=<value>`.

Intrinsic attributes are read from the design or constraints at
call time. User attributes, set by `set_attribute`, live in the
interface session and are cleared on restore and close like
collections. Setting an intrinsic attribute fails fast
(read-only). Getting an attribute that is neither intrinsic for
the member's class nor ever set fails fast, listing the class's
attributes.

| Class | Attributes |
|---|---|
| all | `name`, `full_name` (same as `name` in the flat v1 model), `object_class` (`cell`, `instance`, `net`, `port`, `pin`, `clock`) |
| cell | `is_macro` (`true`/`false`) |
| instance | `cell` (master cell name), `is_register` (`true`/`false`, the `all_registers` rule) |
| net | `fanout` (pin count on the net) |
| port | `direction` (`in`, `out`, `inout`) |
| pin | `instance` (owning instance name), `pin` (leaf pin name), `net` (connected net name) |
| clock | `period` (effective ns; empty when underivable), `waveform` (`{rise fall}`; empty when underivable), `is_generated` (`true`/`false`), `source` (space-joined target specs), `master_clock` (name; empty for SDC clocks) |

Clock `period` and `waveform` reuse the report_clock derivation:
SDC rows read stored values, generated rows derive from the
master (divide over multiply, inversion where representable),
cycles yield unknown, never a loop. There is exactly one
derivation in the codebase.

## Algebra (C3)

Collections are heterogeneous by construction: algebra can mix
kinds.

| Command | Result |
|---|---|
| `add_to_collection <c1> <c2>` | New handle, union, deduped. Inputs untouched. |
| `append_to_collection <c1> <c2>` | Union into `c1` in place; the same handle is returned. |
| `remove_from_collection [-intersect] <c1> <c2>` | New handle. Without `-intersect`: `c1` minus `c2`. With `-intersect`: `c1` cap `c2`. |
| `compare_collections <c1> <c2>` | `equal=0\|1 count1=N count2=M common=K only1=A only2=B`. Equality is set equality. |
| `index_collection <coll> <index>` | New singleton handle, 0-based. Payload adds `object=<name>`. Out of range fails fast. |
| `query_objects <coll> [-limit <n>]` | Member names, one per line. With `-limit`, at most `n` lines plus a final `truncated=1 shown=<n> total=<m>` line. |
| `foreach_in_collection <coll> <var> <body>` | Runs the body once per member, in stored order, with the loop variable set to the member's display name. Payload `iterations=N`. |

`foreach_in_collection` needs a script host: the Tcl shell
evaluates the body with the loop variable set (Tcl semantics: the
variable stays set to the last member afterwards). The GUI
console runs one registry command per line and cannot evaluate a
script body, so the command fails fast there naming the reason. A
body failure fails fast naming the member and the error.

## Lifetime (C3, A9)

- `restore`: collections and user attributes are cleared. They
  named objects of the previous project state; the restored
  project carries its own design and constraints. This matches the
  vendor direction and is intended, not a gap.
- `close_design`: collections and user attributes are cleared.
- `rollback`, `abort_transaction`: collections and user
  attributes survive. The ledger revision does not delete design
  objects or constraints, so handles stay valid.

## Failure behavior

Every command fails fast with a named reason: no design open,
unknown collection handle, unknown kind or attribute, bad filter
syntax, an empty result where a single object is required, an
index out of range, `foreach_in_collection` without a script
host. Empty `get_*` results are an empty collection, never an
error. No option is silently ignored: `-hierarchical` is not
offered (the model is flat), and abbreviation stays out.

## Not in this unit

C4, path collections as report inputs (`report_timing` over a
path collection, creation from `get_timing_paths`), waits on
`get_timing_paths` (I8) and is not built here.
