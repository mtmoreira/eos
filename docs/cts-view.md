# `cts`, the CTS session record, and `cts_summary` (full-flow GUI unit F5)

`eos::cts` runs the in-tree TritonCTS engine on the session's
placed database, one clock net per invocation:

```text
cts -clock <net> [-buffer <cell>...] [-root_buffer <cell>] [-sink_buffer <cell>]
```

With no `-buffer`, the engine infers the buffer set from the
loaded Liberty (the `tt` corner when present, else the first
corner in map order; the chosen corner is named in the
payload). The command requires the session database with
technology and a top block, a floorplanned and placed design,
and a loaded Liberty; each missing prerequisite fails fast
with the stage-command message for it. An unknown clock net
fails with the driver's "clock net not found in the design
(check the -clock argument)", and a clockless design fails
the same way: there is no no-op success.

The payload reports the engine's counts and states its two
honesty limits in words:

```text
roots=<n> buffers=<n> sinks=<n> subnets=<n> skew_ns=unavailable (TritonCTS reports no ns skew; measure via post-CTS STA) insertion_delay_ns=unavailable (TritonCTS insertion delay is a length, not a time) liberty_corner=<c> scene=<name> worst_slack_ns=<value>
```

TritonCTS computes no skew in nanoseconds and its insertion
delay is a length, not a time, so both read `unavailable`
rather than an invented number. The payload's timing truth is
the per-scene post-CTS worst slack from the session timing
service on the CTS database (estimated parasitics, the same
analysis `place` reports). When the session is not configured
for timing on the database, the payload carries
`sta_slack_ns=unavailable (<reason>)` instead.

On demo/clocked, the unavailable form is what both clock
nets report today: `sta_slack_ns=unavailable (scene func_tt:
could not read SDC file: clk_top.sdc: Error 374: clk_top.sdc
line 2, empty ports/pins/nets argument.)`. The root cause is
in the timing service's SDC read, not in CTS: it re-reads
`clk_top.sdc` against the flat imported database, and the
SDC's generated-clock source, `[get_pins u_div/q]`, does not
resolve there (the flat DEF names the divider `u_div/_17_`,
pin `Q`; OpenSTA warns `pin 'u_div/q' not found`). Every
ODB-path analysis on this demo reports the same posture,
`place` included, and `run_timing` fails with the same
error before and after CTS. A separate SDC pin-resolution
fix addresses it; until then the payload, the record, and
`cts_summary` carry the statement verbatim rather than a
finite slack the analysis never produced.

## The CTS session record

On success only, the run is stored in a session record
(`ctx.cts`), mirroring the place record: one entry per clock
net in first-run order, replaced by the latest successful run
on that net. A failed run leaves the record untouched. The
record is interface session state, not project state: it is
cleared when the design closes, and a restore does not bring
it back. `cts` does not clear the timing session's last
results and does not touch the place record, so
`place_summary` keeps reporting the place run and
`timing_summary` keeps answering from the last `run_timing`
until a new one replaces it.

Each entry also carries `clock_sinks`, the count of flop
clock pins among the entry's `sinks`. The two counts answer
different questions: `sinks` is the engine's count of every
electrical load pin on the net, and `clock_sinks` is the
clock view's count for the same net. They differ when a
clock net also feeds a non-clock load. On demo/clocked,
`clk` reports 23 and 23; `div_q` reports `sinks=9` and
`clock_sinks=8`, because the generated clock's net also
drives the toggle-feedback inverter's input pin
(`u_div/_16_`, pin A), a real load the clock view does not
count as a clock sink.

Re-running `cts` on an already-buffered net does not build
a second tree and does not succeed: the engine skips a net
that already holds a clock buffer, reports zero clock
roots, and the driver fails with "design has no clock (no
clock tree was built; check the -clock argument)". The
record keeps the first run's entry.

## `cts_summary`

```text
cts_summary
```

The structured query the GUI reads: the recorded runs, in
record order, one net line per net, each followed by that
run's per-scene slack lines. It is a query, not a report: it
runs no engine, writes no file, and changes no session
state. It takes no options and does not require the
database; its whole content is the record. With an empty
record it fails fast:

```text
EOS invalid_configuration: no clock tree: eos::cts first
```

Payload shape (the demo/clocked record, verbatim except the
SDC path inside the note, which echoes the path the session
read the file from):

```text
cts nets=2
net name=clk roots=1 buffers=3 sinks=23 clock_sinks=23 subnets=3 liberty_corner=tt
sta_note sta_slack_ns=unavailable (scene func_tt: could not read SDC file: clk_top.sdc: Error 374: clk_top.sdc line 2, empty ports/pins/nets argument.)
net name=div_q roots=1 buffers=3 sinks=9 clock_sinks=8 subnets=3 liberty_corner=tt
sta_note sta_slack_ns=unavailable (scene func_tt: could not read SDC file: clk_top.sdc: Error 374: clk_top.sdc line 2, empty ports/pins/nets argument.)
```

A net whose run measured finite slacks carries one
`slack scene=<name> worst_slack_ns=<value>` line per scene
instead of the `sta_note` line; a net carries one or the
other, never both.

## The GUI surface

The Clocks tab is the CTS surface. A `cts` typed in the
console navigates to it on success. The stage bar's CTS
button is enabled once a CTS record is loaded and shows the
Clocks tab when clicked. Below the per-clock trees, the tab
appends the recorded runs:

```text
CTS (this session):
 net clk: roots 1, buffers 3, sinks 23, subnets 3, Liberty corner tt
 sta_slack_ns=unavailable (scene func_tt: could not read SDC file: clk_top.sdc: Error 374: clk_top.sdc line 2, empty ports/pins/nets argument.)
 net div_q: roots 1, buffers 3, sinks 9 (8 clock pins), subnets 3, Liberty corner tt
 sta_slack_ns=unavailable (scene func_tt: could not read SDC file: clk_top.sdc: Error 374: clk_top.sdc line 2, empty ports/pins/nets argument.)
```

(The numbers are the demo/clocked record. A net with finite
slacks shows one indented `post-CTS worst slack (<scene>):
<x> ns` line per scene instead of the note, and the
`(N clock pins)` parenthetical appears only when the two
sink counts differ.)

The section is keyed by net name and the tree blocks by
clock name; on the clocked demo the two differ for the
generated clock (net `div_q`, clock `clk_div`), and the
section keeps them side by side without pretending they are
the same key. When no run is recorded, nothing is appended:
the tab's stage line and notes already state the pre-CTS
truth.

## What the clock view claims after CTS

The mapped-stage walk reads the live database at query time,
so after CTS the trees show the inserted clock buffers as
buffer nodes at their real depths, with every sink below the
buffer that drives it. Buffer and subnet counts are measured
facts: they appear in the `cts` payload, the record,
`cts_summary`, and the tab's CTS section, and the walk counts
the same buffers independently.

One walk rule changed with this unit. The structural half of
source resolution (which instance generates a generated
clock when the SDC names no source object) used to require
the candidate's output net to carry a flop clock pin
directly. After CTS inserts a root buffer on `div_q`, that
net's loads are the buffer's input pin and the feedback
inverter, so no candidate resolved and `clk_div` rendered
`tree=none, sinks=-` even though its tree was in the
database. The rule now treats a net as clock-carrying when
a load is a clock pin, or a load is an input pin on an
instance with an output pin (a buffer shape) whose output
nets are clock-carrying in turn. With it, the post-CTS walk
resolves `clk_div`'s source through the root buffer to the
divider, flags the divider `generates=clk_div` on `clk`'s
tree, and shows `sinks=8` for `clk_div` (the walk counts
clock pins; the record's 9 for the same net counts load
pins).

Counting buffer nodes on the walked `clk_div` tree needs
one care: the walk classifies any input-pin load on an
instance with an output pin as a buffer node, so the tree
shows 5 buffer nodes, the 3 CTS clock buffers plus the
feedback inverter and the divider's own D pin, which are
design logic on the generated clock's nets. The CTS buffers
among them are the ones whose master is a clock buffer
(EM_CLKBUF_X1/X2): 3, matching the record, as on `clk`.

Three things do not change, and the view does not pretend
otherwise:

- **Latency stays `-`.** Post-CTS STA yields path slacks,
  not per-sink clock arrival times, and no facade exposes
  the latter. Filling the column from buffer depths would be
  structure dressed as time.
- **Skew stays the SDC value.** The Clocks tab's skew field
  is the stored clock uncertainty the designer wrote, before
  CTS and after; nothing in the run measures skew.
- **The stage token stays `mapped`.** CTS enriches the
  database; it does not change which design state the
  session holds. The appended CTS section carries the
  post-CTS fact.
