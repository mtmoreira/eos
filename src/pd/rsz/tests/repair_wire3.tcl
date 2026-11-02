# repair_wire3: adapted upstream repair driver (OpenROAD v2.0 rsz/test repair_wire3.tcl).
# EOS: a single eos::repair_timing call as the last command,
# printed with puts so the harness can read the verdict line
# in script-file mode. Paths are repo-relative to
# src/pd/rsz/tests (the harness cwd).
# PENDING-LICENCE: the PDK lib/lef files this script
# references are not committed; see tests/README.md and
# pending-licence-needs.md. The script runs only where
# those files are present locally.
# NEGATIVE case (Q6 behavior change, not upstream parity).
# Upstream sets layer RC to 0.0 and repairs anyway; EOS forbids
# proceeding on 0.0. Every real Nangate45 metal layer carries
# complete RC data, so no real layer can exercise the fail-fast
# path; metal99 names a layer with no tech data, which takes
# the same fail-fast branch. The script catches the error and
# puts the error line; the harness diffs that line.
if {[catch {
  eos::repair_timing -lef fixtures/Nangate45.lef -def fixtures/repair_wire1.def -liberty fixtures/Nangate45_typ.lib -wire_layer metal99 -design -max_wire_length 0.0006
} msg]} {
  puts "error=$msg"
} else {
  puts "status=unexpected-ok"
}
