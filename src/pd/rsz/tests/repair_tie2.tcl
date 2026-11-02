# repair_tie2: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_tie2.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: repair_tie_fanout -separation 2 LOGIC1_X1/Z.
# Upstream -separation is in Liberty units (microns, per the
# OpenROAD resizer docs); EOS -separation is in meters, so 2
# microns is 0.000002 m.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_tie2.def -liberty fixtures/Nangate45_typ.lib -tie -tie_port LOGIC1_X1/Z -separation 0.000002]
