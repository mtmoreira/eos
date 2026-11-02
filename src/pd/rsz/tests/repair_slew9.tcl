# repair_slew9: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_slew9.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: shared max_slew_cap setup (104-load fanout, sky130
# cells), repair_design -max_slew_margin 5. Margin passes through
# verbatim (upstream uses 5, not a 0-1 fraction).
puts [eos::repair_timing -tlef fixtures/sky130hd.tlef -lef fixtures/sky130hd_std_cell.lef -def fixtures/repair_max_slew_cap.def -liberty fixtures/sky130hd_tt.lib -clock clk1 -period 1 -wire_layer met3 -design -max_slew_margin 5]
