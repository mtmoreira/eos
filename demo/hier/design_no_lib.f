# NEGATIVE CONTROL. Same design as design.f minus the library
# macro file. Measured behavior in current EOS (not the naive
# expectation): elaboration still SUCCEEDS, because EOS runs
# plain `hierarchy -top` without -check, so the unlinked macro
# becomes an opaque cell instead of an error. What changes is
# the hierarchy: without the library entry, u_mac VANISHES from
# synth_hierarchy (no hier_top/u_data/u_mac node) and from
# synth_blocks hier_datapath (only u_alu and u_out). With the
# entry (design.f), u_mac appears with its full port list.
# That appearance versus disappearance is the library-link test:
// the macroblock resolves into the visible hierarchy only when
# its library file is linked.
rtl/hier_top.v
rtl/hier_ctrl.v
rtl/hier_counter.v
rtl/hier_datapath.v
rtl/hier_alu.v
rtl/hier_outreg.v
