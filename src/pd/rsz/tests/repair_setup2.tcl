# repair_setup2: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_setup2.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: repair_design then repair_timing -setup; SDC file as
# shipped (clock 0.1, delays 0.02); signal metal3, clock metal5.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_setup2.def -liberty fixtures/Nangate45_typ.lib -sdc fixtures/repair_setup2.sdc -wire_layer metal3 -wire_layer_clk metal5 -design -setup]
