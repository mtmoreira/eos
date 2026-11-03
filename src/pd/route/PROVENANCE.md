# route: provenance

Module: src/pd/route, target eda_pd_route, layer L4
engines. Adopted by place-and-route unit P4 (owner approval
2026-10-31, place-and-route phase scope).

## Upstream

No new engine is imported by this unit. The global router the
runner drives (GlobalRouter + FastRoute, with the AntennaChecker,
SteinerTreeBuilder, and detailed placer support objects it takes)
is already in-tree from P2: see src/pd/grt/PROVENANCE.md,
src/pd/ant/PROVENANCE.md, src/pd/stt/PROVENANCE.md, and
src/pd/place/PROVENANCE.md, all at the same OpenROAD commit
ead3236e56d57362a8dc8b25658fb41a191eba17 that supplies the
in-tree OpenDB. No other commit is substituted anywhere in this
module, and no file under src/pd/grt, src/pd/ant, src/pd/stt, or
src/pd/place is modified by this unit.

## EOS-owned files (not upstream)

CMakeLists.txt
PROVENANCE.md
README.md
include/eos/pd/route.h
src/route.cc

The runner is EOS-owned glue only: it constructs the in-tree
engine through the grt module's MakeGlobalRouter factory, runs
global routing with guides saved to the database, and reads the
congestion map back from the block GCellGrid the engine wrote.
Detailed routing is P5 scope and is not in this module.
