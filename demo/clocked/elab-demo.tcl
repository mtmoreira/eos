# Clocked demo script for eos-gui.
# Run from demo/clocked: ../../build/apps/eos-gui/eos-gui elab-demo.tcl
# The same four commands can be typed in the GUI console entry
# line instead, one per Return (the eos:: prefix is optional there).
# NOTE: this script deliberately stops at elaborate. The full chain
# (synth_generic, synth_map, write_def, read_lef, read_def,
# floorplan, place, cts) is documented in README.md as the F5
# acceptance sequence, but it does not run until the engine gaps
# listed there close. A script that fails partway is bad demo
# material.
eos::create_scenario func_tt func tt estimate
eos::read_filelist design.f clk_top
eos::read_sdc clk_top.sdc
eos::elaborate
