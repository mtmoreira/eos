# Full-flow demo script for the clocked design (F5 CTS unit).
# Run from demo/clocked: ../../build/apps/eos/eos flow-demo.tcl
# The same commands can be typed in the GUI console entry line
# instead, one per Return (the eos:: prefix is optional there).
# The chain runs the whole front-to-CTS flow in one session:
# scenario, library, filelist, SDC, elaborate, generic and
# mapped synthesis, the DEF bridge out and back in, floorplan,
# place, then one cts run per clock net (master first), the
# CTS record query, the clock view, and a post-CTS timing run.
# Measured outputs for this sequence are recorded in README.md.
eos::create_scenario func_tt func tt estimate
eos::read_liberty tt ../../src/pd/rsz/tests/mock/eos_mock.lib
eos::read_filelist design.f clk_top
eos::read_sdc clk_top.sdc
eos::elaborate
eos::synth_generic
eos::synth_map -target area
eos::write_def clk_top.def -lef ../../src/pd/rsz/tests/mock/eos_mock.lef -tlef ../../src/pd/rsz/tests/mock/eos_mock.tlef
eos::read_lef ../../src/pd/rsz/tests/mock/eos_mock.tlef
eos::read_lef ../../src/pd/rsz/tests/mock/eos_mock.lef
eos::read_def clk_top.def
eos::floorplan
eos::place
eos::cts -clock clk
eos::cts -clock div_q
eos::cts_summary
eos::clock_network_summary
eos::run_timing
