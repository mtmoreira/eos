// eos_macro_mac: library macroblock for the hierarchical demo.
//
// This file lives in lib/, not in rtl/, on purpose: it stands in
// for a hard macro (a DSP multiply-accumulate block) that a real
// flow links from a library instead of compiling with the design
// sources. EOS resolves it because design.f links this file; the
// no-library filelist design_no_lib.f proves it, since
// elaboration of the same top fails there with an unresolved
// reference to eos_macro_mac.
//
// The block itself is plain synthesizable RTL (EOS has no
// black-box library flow, so the macro ships as RTL, not as an
// empty black box): a registered multiply-accumulate,
// y = a * b + acc_in, held when en is low.
module eos_macro_mac (
  input wire clk,
  input wire rst_n,
  input wire en,
  input wire [7:0] a,
  input wire [7:0] b,
  input wire [15:0] acc_in,
  output reg [15:0] y
);
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      y <= 16'h0000;
    else if (en)
      y <= a * b + acc_in;
  end
endmodule
