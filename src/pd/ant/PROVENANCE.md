# ant: provenance

Upstream: OpenROAD at commit ead3236e56d57, imported as EOS's own
code under unit P2 (placement) of the place-and-route phase, as a
link/runtime support import for grt only. There is no eos::antenna
command; PV stays orchestration in this phase. The pin tree itself
is never modified; the file below is a copy into this module.

## Licence audit (run at import time, 2026-11-01)

The file was grepped for licence text (GNU/General Public/Lesser/
Affero/Mozilla/Eclipse/Apache/MIT/Boost phrases). Result: BSD-3-
Clause only, matching the OpenROAD BSD-3 licence. No copyleft text
was found. The Tcl factory (MakeAntennaChecker) and all .tcl/.i
surfaces are not imported.

## Imported files (SHA-256 of the upstream file at ead3236)

| EOS path (relative to src/pd/ant) | Upstream path (relative to OpenROAD) | SHA-256 |
|---|---|---|
| include/ant/AntennaChecker.hh | src/ant/include/ant/AntennaChecker.hh | 3ac4ad973313677e4f2a2401313bf3abc6e10843103cb645d826e2074efeda6c |
| src/AntennaChecker.cc | src/ant/src/AntennaChecker.cc | 5415b2f3037660e691593f7fbda9e988c6ca9a445f9db8c08bb8ee82943c86d3 |
