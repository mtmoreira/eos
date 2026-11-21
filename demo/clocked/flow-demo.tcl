# Full-flow demo script for the clocked design (F6 route unit,
# F7 extraction tail, F8 finish-and-export tail, F9 ECO step).
# Run from demo/clocked: ../../build/apps/eos/eos flow-demo.tcl
# The same commands can be typed in the GUI console entry line
# instead, one per Return (the eos:: prefix is optional there).
# The chain runs the whole front-to-route flow in one session:
# scenario, library, filelist, SDC, elaborate, generic and
# mapped synthesis, the DEF bridge out and back in, floorplan,
# place, then one cts run per clock net (master first), the
# CTS record query, the clock view, a post-CTS timing run,
# then route, the route record query, the clock view again,
# a post-route timing run, then the ECO step: a timing
# closure run over the routed design and the ECO record
# query (the closure accepts estimate-fidelity runs only,
# so it sits before the extracted scenario exists; on this
# design its gate is already met and it accepts no edit),
# then extraction with the mock
# rules file, the extract record query, an extracted
# scenario over the written SPEF, a two-scene timing run,
# and the extract record query again (now carrying the
# estimated-versus-extracted comparison), then the
# finish-and-export step: a bare finish (the mock Liberty
# has no pg pins and the design has no power or ground
# nets, so no PDN is built), the finish record query, the
# GDS export beside the demo's other products, and the GDS
# record query, which reads the export back.
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
eos::route
eos::route_summary
eos::clock_network_summary
eos::run_timing
eos::close_timing 5
eos::eco_summary
eos::extract_parasitics clk_top.spef -model ../../src/pd/rsz/tests/mock/eos_mock.rules
eos::extract_summary
eos::create_scenario func_tt_ext func tt extracted
eos::set_spef clk_top.spef
eos::run_timing
eos::extract_summary
eos::finish
eos::finish_summary
eos::write_gds clk_top.gds
eos::gds_summary
