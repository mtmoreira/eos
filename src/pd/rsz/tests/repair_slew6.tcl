# repair_slew6: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_slew6.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: generated hi-fanout DEF (50 loads, sky130 cells), no
# clock, wire layer met3.
puts [eos::repair_timing -tlef fixtures/sky130hd.tlef -lef fixtures/sky130hd_std_cell.lef -def fixtures/repair_slew6.def -liberty fixtures/sky130hd_tt.lib -wire_layer met3 -design]
