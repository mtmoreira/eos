// clk_div: divide-by-2 toggle flop. Its output q is the source
// of the generated clock clk_div (see clk_top.sdc).
module clk_div (
  input wire clk,
  input wire rst_n,
  output reg q
);
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      q <= 1'b0;
    else
      q <= ~q;
  end
endmodule
