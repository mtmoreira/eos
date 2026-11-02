# repair_setup1: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_setup1.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: create_clock -period 0.3 clk, set_wire_rc -layer metal3,
# repair_timing -setup. Upstream end: 4 inserted, 17 resized,
# RSZ-0062 (see cross-check in README).
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_setup1.def -liberty fixtures/Nangate45_typ.lib -clock clk -period 0.3 -wire_layer metal3 -setup]
