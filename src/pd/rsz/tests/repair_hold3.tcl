# repair_hold3: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_hold3.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: propagated clock (EOS uses the ideal clock from
# -clock/-period; recorded as a delta). Upstream: 2 hold buffers,
# end min slack 0.01.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_hold3.def -liberty fixtures/Nangate45_typ.lib -clock clk -period 2 -wire_layer metal1 -hold]
