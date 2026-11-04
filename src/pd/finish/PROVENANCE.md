# finish: provenance

Module: src/pd/finish, target eda_pd_finish, layer L4
engines. Adopted by place-and-route unit P7 (owner approval
2026-10-31, place-and-route phase scope).

## Upstream

No new engine is imported by this module itself. The engines the
runner drives are already in-tree from P7: the Finale metal-fill
engine (see src/pd/fin/PROVENANCE.md), the PdnGen power-grid
engine (see src/pd/pdn/PROVENANCE.md), and the detailed placer's
fillerPlacement (see src/pd/place/PROVENANCE.md), all at the same
OpenROAD commit ead3236e56d57362a8dc8b25658fb41a191eba17 that
supplies the in-tree OpenDB. No other commit is substituted
anywhere in this module, and no file under src/pd/fin,
src/pd/pdn, or src/pd/place is modified by this module.

The GDSII stream writer (src/gds_stream.cc) is EOS-owned glue,
not an import: no OpenDB GDSII writer exists at the ead3236 pin,
and the io/gds module is still a skeleton, so the plan's "OpenDB
writer already in tree" premise does not hold at this pin. See
include/eos/pd/finish.h for the writer's documented scope.

## EOS-owned files (not upstream)

CMakeLists.txt
PROVENANCE.md
README.md
include/eos/pd/finish.h
src/finish.cc
src/gds_stream.cc
src/gds_stream.h

The runner is EOS-owned glue only: it runs filler placement
through the in-tree detailed placer, builds the basic power grid
through the pdn module's PdnGen, runs metal fill through the fin
module's Finale when a rules file is given, marks the block
finished, and streams GDSII through the EOS-owned writer.
