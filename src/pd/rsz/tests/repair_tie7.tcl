# repair_tie7: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_tie7.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: two repair_tie_fanout calls (Z0, Z1). EOS -tie_port
# is repeatable and runs repairTieFanout once per port in order.
# NOTE: repair_tie7.lib carries the Nangate proprietary header
# and is local-only, never committed.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -lef fixtures/repair_tie7.lef -def fixtures/repair_tie7.def -liberty fixtures/Nangate45_typ.lib -liberty fixtures/repair_tie7.lib -tie -tie_port TIE_X1/Z0 -tie_port TIE_X1/Z1]
