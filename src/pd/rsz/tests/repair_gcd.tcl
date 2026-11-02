# repair_gcd: gcd command case (R1 step 2, PR4).
# Mirrors the PR3 C++ driver sequence on the command path: sky130hd
# tech + merged lef, routed 6_final.def, degraded SDC (core_clock
# 2.9 ns), sky130 tt liberty, setup then hold (the command default
# with no pass flag, matching the driver order).
# Wire RC: -wire_layer met2, the layer the driver set explicitly
# (8.93e5 ohm/m, 7.789e-11 F/m from the tlef); the payload reports
# the tech-derived values so the match is auditable.
# RE-BASELINE NOTE: the step-1 anchor (END WNS +0.002058 ns, TNS 0,
# 22 inserted, 2 resized, 1068 instances) was optimistic-loop
# output. Under the corrected F1 loop the driver-path numbers are
# END WNS -0.0852105 ns, TNS -0.273604 ns, 44 inserted, 8 resized,
# 1090 instances, RSZ-0062 unable to repair. This script pins the
# COMMAND-path result; if it differs from the driver-path numbers,
# the README records the difference and the case is not claimed
# as a driver confirmation.
# PENDING-LICENCE: tlef/lef/lib/DEF/SDC are local-only inputs.
puts [eos::repair_timing \
  -tlef fixtures/sky130_fd_sc_hd.tlef \
  -lef fixtures/sky130_fd_sc_hd_merged.lef \
  -def fixtures/gcd_6_final.def \
  -liberty fixtures/sky130_fd_sc_hd__tt_025C_1v80.lib \
  -sdc fixtures/gcd_degraded.sdc \
  -wire_layer met2]
