# repair_wire3, mock port: loads the EOS-owned synthetic
# mock library (mock/eos_mock.*) instead of any real
# PDK file. Values are synthetic; the .ok pins mock
# behavior, not upstream parity. See mock/README.md.
if {[catch {
  eos::repair_timing -tlef mock/eos_mock.tlef -lef mock/eos_mock.lef -liberty mock/eos_mock.lib -def mock/repair_wire1.def -wire_layer em99 -design -max_wire_length 0.0006
} msg]} {
  puts "error=$msg"
} else {
  puts "status=unexpected-ok"
}
