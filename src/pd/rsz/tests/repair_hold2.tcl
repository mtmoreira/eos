# repair_hold2: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_hold2.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: input delay 0.0, output delay -0.3, propagated clock.
# Design-doc map uses -clock/-period only; the dropped delays and
# propagated clock are recorded as a delta in README. Upstream:
# 11 hold buffers, end min slack 0.01.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_hold1.def -liberty fixtures/Nangate45_typ.lib -clock clk -period 2 -wire_layer metal1 -hold]
