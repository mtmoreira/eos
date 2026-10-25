# sta (L3 analysis)

OpenSTA, adopted as the static timing engine under the timing
service.

Status: imported 2026-10-25 (Phase 2 unit 2.2) from the pinned
upstream commit; see PROVENANCE.md for the exact commit, the file
inventory, the exclusions, the vendored CUDD closure and the patch
list (zero patches).

The stable public API of this module lives in
include/eos/analysis/sta.h: a StaEngine loads a Liberty library, a
gate-level Verilog netlist, links the design, reads SDC
constraints and optionally SPEF parasitics, and reports the worst
slack and total negative slack of the setup (max) analysis in
nanoseconds. No upstream sta type appears in the facade; nothing
above L3 calls upstream types directly. The upstream public
headers are available inside the module under include/sta/ for the
module's own implementation files.
