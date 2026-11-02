# repair_tie6, mock port: loads the EOS-owned synthetic
# mock library (mock/eos_mock.*) instead of any real
# PDK file. Values are synthetic; the .ok pins mock
# behavior, not upstream parity. See mock/README.md.
puts [eos::repair_timing -tlef mock/eos_mock.tlef -lef mock/eos_mock.lef -liberty mock/eos_mock.lib -def mock/repair_tie6.def -tie -tie_port EM_TIEHI/Z]
