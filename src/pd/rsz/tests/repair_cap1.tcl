# repair_cap1: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_cap1.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: generated hi-fanout DEF (60 loads), max_cap repair.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_cap1.def -liberty fixtures/Nangate45_typ.lib -clock clk1 -period 1 -wire_layer metal3 -design]
