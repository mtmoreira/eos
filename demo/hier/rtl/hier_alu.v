// hier_alu: leaf combinational ALU. op selects add, subtract,
// xor, or and. The datapath wires op to add.
module hier_alu (
  input wire [1:0] op,
  input wire [7:0] x,
  input wire [7:0] y,
  output reg [7:0] z
);
  always @* begin
    case (op)
      2'b00: z = x + y;
      2'b01: z = x - y;
      2'b10: z = x ^ y;
      default: z = x & y;
    endcase
  end
endmodule
