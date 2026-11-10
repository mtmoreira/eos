// clk_cnt: eight-bit counter on the divided clock. Free-running;
// the terminal count (255) pulses tc, which the top exposes as
// div_done.
module clk_cnt (
  input wire clk,
  input wire rst_n,
  output wire tc
);
  reg [7:0] count;

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      count <= 8'h00;
    else
      count <= count + 8'h01;
  end

  assign tc = (count == 8'hFF);
endmodule
