# Hierarchical demo filelist: design sources plus the library macro.
# Entries resolve relative to this file's directory. The lib entry
# is the library link: without it (see design_no_lib.f), u_mac
# vanishes from the hierarchy tree and block diagram, because
# the Hierarchy walk descends only into known modules.
rtl/hier_top.v
rtl/hier_ctrl.v
rtl/hier_counter.v
rtl/hier_datapath.v
rtl/hier_alu.v
rtl/hier_outreg.v
lib/eos_macro_mac.v
