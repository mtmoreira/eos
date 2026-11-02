# registry (L6 interface)

The command registry every interface binds to.

Status: implemented (Phase 1). CommandRegistry with the Plan Section 12 diagnostic taxonomy, a CommandContext holding the L5 Project (design, scenarios, constraints and ledger), the session report registry and collections, and the core command set (29 commands: design lifecycle and construction, scenarios, constraints with coverage and rebind, glob-based collections, ledger transactions and report artifacts, the export freshness gate (eos::export_check, verified_pass on success and proof_unknown with every blocker named on refusal, added in increment 5.2), project-level checkpoint save and restore (the full project file, composed by the session layer in increment 5)). Collections are live ID handles: names resolve at list time, missing objects read as <deleted>.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.

## PR4 commands (unmerged, pending the owner's loop decision)

- eos::repair_timing: self-contained per-call timing
  repair (design, setup, hold, tie, clk_nets, clk_inverters
  passes; default setup+hold), built from explicit file
  options (-tlef, repeatable -lef, -def, repeatable
  -liberty, -sdc or -clock/-period). Wire RC follows
  the Q6 rule documented in src/pd/rsz/README.md
  (explicit override, then -wire_layer or set_wire_rc
  override, then tech default layer, then fail-fast).
  It builds its own dbSta session per call and leaves
  no session state behind.
- eos::set_wire_rc <layer> <r_ohm_per_m> <c_f_per_m>:
  per-layer wire-RC override on the command context,
  cleared by ResetProject on close_design.
  Negative, zero, and NaN values are rejected.

Registry count contract: the unit test pins 50 commands
(the count after GUI U4). PR4 adds these two, making 52;
that test update rides with the PR4 merge, after the
owner's loop decision, and is NOT made here. Until then
the count test fails by exactly that delta, which is
the known pending failure, not a regression.
