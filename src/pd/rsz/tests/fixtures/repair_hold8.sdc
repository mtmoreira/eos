create_clock -period 2 clk
set_input_delay -clock clk 0 [get_ports {in1 in2}]
set_output_delay -clock clk -0.1 [get_ports out]
set_propagated_clock clk
