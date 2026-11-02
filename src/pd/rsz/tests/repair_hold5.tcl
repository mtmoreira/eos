# repair_hold5: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_hold5.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: set_max_delay 0.2 r1/CLK to r3/D, no allow flag, so
# repair is blocked. Upstream: no buffers inserted.
puts [eos::repair_timing -tlef fixtures/sky130hs.tlef -lef fixtures/sky130hs_std_cell.lef -def fixtures/repair_hold4.def -liberty fixtures/sky130hs_tt.lib -sdc fixtures/repair_hold5.sdc -wire_layer met1 -hold]
