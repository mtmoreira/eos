// hier_counter: leaf 2-bit counter. Counts while en is high and
// pulses tc on the terminal count (3), which ends the RUN state.
module hier_counter (
  input wire clk,
  input wire rst_n,
  input wire en,
  output wire tc
);
  reg [1:0] count;

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      count <= 2'b00;
    else if (en)
      count <= count + 2'b01;
  end

  assign tc = en && (count == 2'b11);
endmodule
