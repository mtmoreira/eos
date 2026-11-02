# repair_fanout3: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_fanout3.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: repair_design -buffer_cell BUF_X1. EOS -design takes
# no buffer-cell option (the engine picks buffers internally);
# the option is not carried. No upstream .ok exists; the EOS .ok
# comes from the verified run. NOTE: repair_fanout3.lef carries
# the Nangate proprietary header and is local-only.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -lef fixtures/repair_fanout3.lef -def fixtures/repair_fanout3.def -liberty fixtures/Nangate45_typ.lib -sdc fixtures/repair_fanout3.sdc -design]
