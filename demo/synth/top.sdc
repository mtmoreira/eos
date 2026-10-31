create_clock -name vclk -period 1.25
set_input_delay -clock vclk 0 [all_inputs]
set_output_delay -clock vclk 0 [all_outputs]
