// hier_top: top of the hierarchical GUI demo design.
//
// A small DSP-flavored block: a control FSM sequences a datapath
// that runs a multiply-accumulate plus an ALU stage and captures
// the result in an output register. Three hierarchy levels:
//   hier_top -> hier_ctrl -> hier_counter
//   hier_top -> hier_datapath -> {hier_alu, eos_macro_mac, hier_outreg}
// The MAC leaf (eos_macro_mac) lives in lib/, not in rtl/: it
// resolves only when the library file is linked into the filelist
// (see design.f). All modules are plain synthesizable Verilog;
// no vendor primitives, no simulation-only constructs.
module hier_top (
  input wire clk,
  input wire rst_n,
  input wire start,
  input wire [7:0] data_a,
  input wire [7:0] data_b,
  output wire done,
  output wire [15:0] result
);
  wire run;
  wire load;

  hier_ctrl u_ctrl (
    .clk(clk),
    .rst_n(rst_n),
    .start(start),
    .run(run),
    .load(load),
    .done(done)
  );

  hier_datapath u_data (
    .clk(clk),
    .rst_n(rst_n),
    .run(run),
    .load(load),
    .data_a(data_a),
    .data_b(data_b),
    .result(result)
  );
endmodule
