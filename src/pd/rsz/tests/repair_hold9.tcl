# repair_hold9: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_hold9.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: catch around repair_timing -hold -slack_margin .4
# -max_buffer_percent 2; EOS records RSZ-0060 as
# buffer_limited=1 and still returns Ok. Upstream: 20 hold
# buffers before the limit.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/gcd_nangate45_placed.def -liberty fixtures/Nangate45_typ.lib -clock clk -period 2 -wire_layer metal3 -hold -slack_margin 0.4 -max_buffer_percent 2]
