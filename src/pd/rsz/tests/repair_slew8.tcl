# repair_slew8: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_slew8.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: sky130hd vars (signal met2, clock met5) plus
# set_dont_use probe cells. EOS expands the globs through STA
# PatternMatch (same matcher upstream uses). The globs match
# probe cells in the full liberty; verified by grep (see README).
puts [eos::repair_timing -tlef fixtures/sky130hd.tlef -lef fixtures/sky130hd_std_cell.lef -def fixtures/repair_slew8.def -liberty fixtures/sky130hd_tt.lib -clock clk1 -period 1 -wire_layer met2 -wire_layer_clk met5 -dont_use sky130_fd_sc_hd__probe_p_* -dont_use sky130_fd_sc_hd__probec_p_* -design]
