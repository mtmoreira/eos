# ant: port adaptations

None. The imported AntennaChecker files are byte-identical to
upstream ead3236 (see PROVENANCE.md for SHA-256). The Tcl factory
(MakeAntennaChecker) and all .tcl/.i surfaces are not imported;
EOS registers no eos::antenna command in this phase. The checker
exists in this module only as a link/runtime support import for
grt, whose antenna repair path constructs it.
