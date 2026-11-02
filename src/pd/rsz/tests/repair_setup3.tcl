# repair_setup3: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_setup3.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: create_clock -period 1 clk, set_wire_rc -layer met1,
# set_load 1.0 l2q. The clock lives in the SDC here because the
# command rejects -sdc together with -clock/-period.
puts [eos::repair_timing -tlef fixtures/sky130hd.tlef -lef fixtures/sky130hd_std_cell.lef -def fixtures/repair_setup3.def -liberty fixtures/sky130hd_tt.lib -sdc fixtures/repair_setup3.sdc -wire_layer met1 -setup]
