# repair_hold7: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_hold7.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: catch around repair_timing -hold -slack_margin .4
# -max_utilization 13; EOS records RSZ-0050 as
# utilization_limited=1 and still returns Ok. Upstream clock is
# named core_clock on port clk; EOS names it clk. Upstream: 114
# hold buffers before the limit.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/gcd_nangate45_placed.def -liberty fixtures/Nangate45_typ.lib -clock clk -period 2 -wire_layer metal3 -hold -slack_margin 0.4 -max_utilization 13]
