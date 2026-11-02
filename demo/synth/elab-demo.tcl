# Elaboration demo script for eos-gui.
# Run from demo/synth: ../../build/apps/eos-gui/eos-gui elab-demo.tcl
# The same four commands can be typed in the GUI console entry
# line instead, one per Return (the eos:: prefix is optional there).
eos::create_scenario func_tt func tt estimate
eos::read_verilog optloop.v optloop
eos::elaborate
eos::synth_generic
