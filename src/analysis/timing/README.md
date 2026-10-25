# timing (L3 analysis)

The timing service: one timing graph and STA evaluation for all consumers.

Status: implemented in Phase 2 unit 2.3. The service
(include/eos/analysis/timing.h) analyzes a design (a gate-level
Verilog netlist, or an imported ODB database whose top block is
written out as a netlist through the ODB facade) once per scene:
a scenario, the Liberty for its corner, and the parasitics its
level calls for (extracted scenes require their SPEF, estimate
scenes carry none). It returns one metric map per scene (setup
worst slack and TNS, hold worst slack and TNS, all in ns).
Report registration is the caller's job, through session/reports:
an L3 service does not reach up to L5.
The stable public API of this module will live in include/ and is versioned per the EOS Code Structure document.
