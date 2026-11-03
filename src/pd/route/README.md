# route (L4 engines)

Full routing: `eos::route` runs the in-tree OpenROAD global
router (grt module, imported in P2 from OpenROAD at ead3236)
over a placed design, saves the resulting guides into the
database, and then runs the in-tree OpenROAD detailed router
(drt module, imported in P5 from the same commit) over those
guides, with an EOS-owned rip-up and reroute loop bounded by
an iteration bound. The detailed stage writes real wires and
vias back into the database.

Status: global routing implemented in P4, detailed routing in
P5. The runner is EOS-owned code in src/route.cc; its stable
public API is include/eos/pd/route.h.

What the command reports, as measured on the run:

- routed nets vs total nets (the global engine skips nets it
  cannot route, e.g. single-terminal nets; the gap is reported,
  not hidden), and the number of guide objects written;
- a congestion map, one entry per routing layer in range, summed
  from the block GCellGrid the global engine wrote: directional
  capacity, usage, and overflow max(0, usage - capacity) per
  tile;
- the detailed violation count: DRC markers the detailed stage
  leaves in the design (zero means DRC-clean on the rules the
  engine checks);
- the detailed loop: iterations used against the iteration
  bound, and whether the loop stopped at the bound. The loop
  stops early when the violation count reaches zero; it stops
  at the bound when violations remain, and the report states
  which happened. A nonzero violation count is reported, never
  failed on, and never hidden.

How the detailed loop works: the runner drives TritonRoute in
single-step mode (the upstream single-step pattern). The
engine's main() runs pin access, guide reading, region
preparation, and track assignment, then initializes the
detailed router. Each loop step is one rip-up and reroute pass
(stepDR): the first pass rips up everything and follows the
guides; later passes repair DRC markers only, with the engine
strategy's DRC entry parameters. endFR then writes the routed
design back to the database, and the reported violation count
is the marker count of that written design. Distributed routing
stays off: the runner passes a null distributed backend, and
any distributed call fails loudly (see ../drt/PATCHES.md).

Preconditions (all fail fast): technology data loaded, a mapped
design, a floorplanned block (rows exist), at least one placed
instance, and a Liberty library. The library is required because
the global engine's clock-net detection builds the STA graph
and throws STA-2141 when no library is loaded; the runner fails
fast without one instead of letting the engine die inside clock
detection. The critical-nets percentage is still pinned to 0,
because the wire-parasitics builder that path needs is still
deferred (see ../grt/PATCHES.md).

Options: `-min_layer` / `-max_layer` restrict the global
routing layer range (1-based OpenDB routing layer numbers; 0
means the engine default on that end). `-max_iterations`
bounds the detailed rip-up and reroute loop (default 64; values
below 1 fail fast). `-bottom_layer` / `-top_layer` name the
detailed stage's layer range (empty means the engine default
on that end; a name that is not a routing layer in the
technology fails fast). The registry command is `eos::route`.

Congestion is reported, never failed on: the global stage
produces the map, the detailed stage owns violations.
