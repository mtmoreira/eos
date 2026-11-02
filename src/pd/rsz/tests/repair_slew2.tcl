# repair_slew2: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_slew2.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: extra lib/lef carrying DFF_MAX_SLEW (pin max_transition).
# NOTE: repair_slew2.lib/lef carry the Nangate proprietary header
# and are local-only, never committed.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -lef fixtures/repair_slew2.lef -def fixtures/repair_slew2.def -liberty fixtures/Nangate45_typ.lib -liberty fixtures/repair_slew2.lib -clock clk1 -period 1 -wire_layer metal3 -design]
