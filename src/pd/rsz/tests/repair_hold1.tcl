# repair_hold1: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_hold1.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: repair_hold_pin on r3/D with BUF_X1 (pin hold, not
# whole-design hold). SDC carries clock, input delays, propagated
# clock. Upstream: 2 hold buffers.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_hold1.def -liberty fixtures/Nangate45_typ.lib -sdc fixtures/repair_hold1.sdc -wire_layer metal1 -hold -end_pin r3/D -buffer_cell BUF_X1]
