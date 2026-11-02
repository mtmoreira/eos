create_clock -period 2 clk
set_input_delay -clock clk 0 [get_ports {in1 in2}]
set_propagated_clock clk
