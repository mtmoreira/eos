# sdc (L1 io)

Parses SDC constraint files into constraint model objects.

Status: the scoped clock parser is implemented
(eos::io::ParseSdcClocks: create_clock, create_generated_clock,
and set_clock_uncertainty, with an unwired list for statements
outside the subset; see include/eos/io/sdc.h). read_sdc wires
its results into the session constraint set. The full SDC
surface remains with the STA engine, which reads the same
files directly.
The stable public API of this module lives in include/ and is versioned per the EOS Code Structure document.
