// clk_cap: eight-bit capture register on the divided clock.
// q <= data while enabled.
module clk_cap (
  input wire clk,
  input wire rst_n,
  input wire en,
  input wire [7:0] data,
  output reg [7:0] q
);
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      q <= 8'h00;
    else if (en)
      q <= data;
  end
endmodule
