# place (L4 engines)

Global and detailed placement of standard cells, driven by the
EOS-owned runner in src/place.cc (include/eos/pd/place.h).

Status: implemented in P2. `eos::place` runs RePlAce global
placement (gpl: doInitialPlace, doNesterovPlace) followed by
OpenROAD detailed placement (dpl: detailedPlacement), then the
engine's own checkPlacement report. The runner reports instances
placed, HPWL in DBU, and a pairwise bounding-box overlap count.
Timing-driven mode (-timing_driven) drives the global placer with
resizer slacks and fails fast when the STA session holds no
Liberty library; placed STA slack is reported by the session
timing service (eos::run_timing) on the placed database, not by
this runner. See PROVENANCE.md (import inventory, licence audit)
and PATCHES.md (port adaptations, including the timingBase Slack
change and the timing-driven Liberty gap).

External dependencies: OR-Tools 9.5.2237 and LEMON (system
package), declared in third_party_manifest.yaml, never vendored.
Configure with -DCMAKE_PREFIX_PATH pointing at the extracted
OR-Tools tarball, or install OR-Tools to a standard prefix; the
CI install step does the latter.
