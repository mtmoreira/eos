# route (L4 engines)

Global routing only, through the global stage: `eos::route` runs
the in-tree OpenROAD global router (grt module, imported in P2
from OpenROAD at ead3236) over a placed design and saves the
resulting guides into the database.

Status: implemented in P4. The runner is EOS-owned code in
src/route.cc; its stable public API is include/eos/pd/route.h.

What the command reports, as measured on the run:

- routed nets vs total nets (the engine skips nets it cannot
  route, e.g. single-terminal nets; the gap is reported, not
  hidden), and the number of guide objects written;
- a congestion map, one entry per routing layer in range, summed
  from the block GCellGrid the engine wrote: directional capacity,
  usage, and overflow max(0, usage - capacity) per tile.

What it does not do (P5 scope): detailed routing, rip-up and
reroute, and DRC-clean loops. Congestion is reported, never
failed on: the global stage produces the map, the detailed stage
owns violations.

Preconditions (all fail fast): technology data loaded, a mapped
design, a floorplanned block (rows exist), at least one placed
instance, and a Liberty library. The library is required because
the engine's clock-net detection builds the STA graph and throws
STA-2141 when no library is loaded; the runner fails fast without
one instead of letting the engine die inside clock detection.
The critical-nets percentage is still pinned to 0, because the
wire-parasitics builder that path needs is still deferred
(see ../grt/PATCHES.md).

Options: `-min_layer` / `-max_layer` restrict the routing layer
range (1-based OpenDB routing layer numbers; 0 means the engine
default on that end). The registry command is `eos::route`.
