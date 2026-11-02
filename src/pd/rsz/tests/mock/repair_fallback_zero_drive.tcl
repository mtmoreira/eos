# repair_fallback_zero_drive, mock port: same design as repair_setup2
# but with a degenerate liberty (mock/eos_mock_zero_drive.lib) in which
# EM_BUF_X1 output slew tables are all zero, so its drive resistance
# is 0 and no finite positive computed max wire length exists. The
# command must fail fast with the documented message, never run
# uncapped. See mock/README.md.
if {[catch {
  eos::repair_timing -tlef mock/eos_mock.tlef -lef mock/eos_mock.lef -liberty mock/eos_mock_zero_drive.lib -def mock/repair_setup2.def -sdc mock/repair_setup2.sdc -wire_layer em3 -wire_layer_clk em5 -design
} msg]} {
  puts "error=$msg"
} else {
  puts "status=unexpected-ok"
}
