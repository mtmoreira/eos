# Hierarchical demo script for eos-gui.
# Run from demo/hier: ../../build/apps/eos-gui/eos-gui elab-demo.tcl
# The same three commands can be typed in the GUI console entry
# line instead, one per Return (the eos:: prefix is optional there).
# NOTE: this script deliberately stops at elaborate. synth_generic
# does not complete on this design: it is sequential, and the
# generic ABC flow has no model for the flip-flop cells the
# elaborated netlist contains (see README.md, "Known limitation").
# The GUI elaboration walkthrough is read, elaborate, and the
# hierarchy, blocks, and schematic views, all of which work here.
eos::create_scenario func_tt func tt estimate
eos::read_filelist design.f hier_top
eos::elaborate
