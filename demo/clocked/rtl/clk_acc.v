// clk_acc: eight-bit accumulator. q <= q + data while running,
// cleared on load.
module clk_acc (
  input wire clk,
  input wire rst_n,
  input wire run,
  input wire load,
  input wire [7:0] data,
  output reg [7:0] q
);
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      q <= 8'h00;
    else if (load)
      q <= 8'h00;
    else if (run)
      q <= q + data;
  end
endmodule
