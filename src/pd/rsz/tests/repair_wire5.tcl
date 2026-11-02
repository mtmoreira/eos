# repair_wire5: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_wire5.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: 3 libs / 3 lefs (Nangate45, fakeram, pad), long wire
# to a pad, -max_wire_length 600. check_in_core has no EOS
# equivalent and is dropped. fakeram files are local-only.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -lef fixtures/fakeram45_64x32.lef -lef fixtures/pad.lef -def fixtures/repair_wire5.def -liberty fixtures/Nangate45_typ.lib -liberty fixtures/fakeram45_64x32.lib -liberty fixtures/pad.lib -wire_layer metal3 -design -max_wire_length 0.0006]
