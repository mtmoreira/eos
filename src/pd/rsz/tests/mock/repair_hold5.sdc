create_clock -period 2 clk
set_propagated_clock clk
set_max_delay -ignore_clock_latency -from [get_pins r1/CK] -to [get_pins r3/D] 0.2
