// INTENTIONALLY UNRESOLVED. DO NOT USE AS A DEMO DESIGN.
//
// This variant exists only so the elaboration-issues panel can be
// tested against a real unresolved reference: it instantiates
// module does_not_exist_anywhere, which is defined nowhere in
// this repository. Measured behavior in current EOS (not the
// naive expectation): elaboration SUCCEEDS, because EOS runs
// plain `hierarchy -top` without -check, so Yosys leaves the
// unknown module as an opaque cell instead of erroring. The
// unresolved reference surfaces in two places instead:
//   - synth_hierarchy shows only the top node, and synth_blocks
//     shows no block instances: the Hierarchy walk descends
//     only into known modules, so the opaque cell vanishes
//     from the tree.
//   - synth_generic fails with: "Cannot find the model for
//     subcircuit does_not_exist_anywhere" (ABC has no model
//     for the opaque cell).
// The default demo path (design.f) never touches this file.
// A future issues panel must therefore surface unresolved
// references from a check of its own, not from elaborate's
// exit code: elaborate exits 0 on this design today.
module unresolved_top (
  input wire clk,
  input wire [7:0] d,
  output wire [7:0] y
);
  does_not_exist_anywhere u_missing (
    .clk(clk),
    .d(d),
    .y(y)
  );
endmodule
