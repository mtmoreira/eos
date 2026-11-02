# repair_wire4: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_wire4.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: two loads driven from the middle, set_load 0 on out1,
# -max_wire_length 900.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_wire4.def -liberty fixtures/Nangate45_typ.lib -sdc fixtures/repair_wire4.sdc -wire_layer metal3 -design -max_wire_length 0.0009]
