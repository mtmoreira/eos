// hier_datapath: MAC plus ALU stage plus output register.
// On load the accumulator clears; on run the MAC folds
// data_a * data_b into the accumulator, the ALU adds the MAC
// low byte to data_b, and the output register captures
// {mac_high_byte, alu_out}.
module hier_datapath (
  input wire clk,
  input wire rst_n,
  input wire run,
  input wire load,
  input wire [7:0] data_a,
  input wire [7:0] data_b,
  output wire [15:0] result
);
  reg [15:0] acc;
  wire [15:0] mac_out;
  wire [7:0] alu_out;

  eos_macro_mac u_mac (
    .clk(clk),
    .rst_n(rst_n),
    .en(run),
    .a(data_a),
    .b(data_b),
    .acc_in(acc),
    .y(mac_out)
  );

  hier_alu u_alu (
    .op(2'b00),
    .x(mac_out[7:0]),
    .y(data_b),
    .z(alu_out)
  );

  hier_outreg u_out (
    .clk(clk),
    .rst_n(rst_n),
    .en(run),
    .d({mac_out[15:8], alu_out}),
    .q(result)
  );

  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      acc <= 16'h0000;
    else if (load)
      acc <= 16'h0000;
    else if (run)
      acc <= mac_out;
  end
endmodule
