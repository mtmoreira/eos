# repair_tie4: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_tie4.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: generated hi-fanout tie DEF (10 loads); repair_design
# must NOT repair the tie net (design pass), then
# repair_tie_fanout. SDC carries set_max_fanout 5.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_tie4.def -liberty fixtures/Nangate45_typ.lib -sdc fixtures/repair_tie4.sdc -design -tie -tie_port LOGIC1_X1/Z]
