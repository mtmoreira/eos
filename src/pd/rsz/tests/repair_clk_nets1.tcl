# repair_clk_nets1: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_clk_nets1.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# Upstream: create_clock in1 -period 10, clock nets in1 and n2
# (driver lists n2 twice; EOS passes it once),
# repair_clock_nets -max_wire_length 600.
puts [eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_wire1.def -liberty fixtures/Nangate45_typ.lib -clock in1 -period 10 -wire_layer metal3 -clk_nets -max_wire_length 0.0006 -clock_net in1 -clock_net n2]
