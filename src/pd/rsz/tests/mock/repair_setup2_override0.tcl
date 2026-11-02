# repair_setup2_override0, mock port: same design as repair_setup2,
# but with an explicit -max_wire_length 0, which must win over the
# computed default and reproduce the pre-adoption uncapped behavior
# (883 inserted). See mock/README.md.
puts [eos::repair_timing -tlef mock/eos_mock.tlef -lef mock/eos_mock.lef -liberty mock/eos_mock.lib -def mock/repair_setup2.def -sdc mock/repair_setup2.sdc -wire_layer em3 -wire_layer_clk em5 -design -setup -max_wire_length 0]
