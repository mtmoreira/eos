# repair_slew1: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_slew1.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: generated hi-fanout DEF (30 loads), set_max_transition
# .05 on the design. Clock lives in the SDC (command rejects -sdc
# with -clock/-period).
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_slew1.def -liberty fixtures/Nangate45_typ.lib -sdc fixtures/repair_slew1.sdc -wire_layer metal3 -design]
