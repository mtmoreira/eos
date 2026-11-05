// hier_outreg: leaf output register with enable.
module hier_outreg (
  input wire clk,
  input wire rst_n,
  input wire en,
  input wire [15:0] d,
  output reg [15:0] q
);
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      q <= 16'h0000;
    else if (en)
      q <= d;
  end
endmodule
