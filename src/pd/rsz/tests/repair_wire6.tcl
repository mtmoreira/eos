# repair_wire6: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_wire6.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: 1500um wire to an output port, -max_wire_length 600.
# No set_load upstream, so no SDC.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_wire1.def -liberty fixtures/Nangate45_typ.lib -wire_layer metal3 -design -max_wire_length 0.0006]
