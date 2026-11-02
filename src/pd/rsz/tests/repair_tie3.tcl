# repair_tie3: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_tie3.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: pad.lef read but pad.lib intentionally skipped
# (non-liberty pin on the net).
puts [eos::repair_timing -lef fixtures/Nangate45.lef -lef fixtures/pad.lef -def fixtures/repair_tie3.def -liberty fixtures/Nangate45_typ.lib -tie -tie_port LOGIC1_X1/Z]
