# repair_clk_inverters1: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_clk_inverters1.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream runs with NO wire RC; EOS applies the tech-derived
# default (metal1). Upstream .ok carries no repair count; the
# default-change delta is recorded in README.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_clk_inverters1.def -liberty fixtures/Nangate45_typ.lib -clock clk1 -period 1 -clk_inverters -clock_net clk1]
