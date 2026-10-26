# Resizer import inventory, compile probes and decision memo (unit R0)

Date: 2026-10-26
Status: unit R0 of the resizer import track. Probes and this
memo only; no import code was written. The strategy decision
is the project owner's, taken on this memo. Units R1 to R3
are not approved and nothing in this document starts them.

## 1. Inventory (measured during scoping, rechecked here)

From OpenROAD v2.0 at a5a5658 (EOS's OpenDB pin), excluding
upstream test directories: rsz is 7 .cc and 4 .hh, dbSta is
4 .cc and 5 .hh, stt is FLUTE only, and pdr is 5 .cc and
5 .hh of which rsz calls exactly one function,
pdr::primDijkstra, from SteinerTree.cc. The modern tree at
ead3236 is larger: rsz plus dbSta plus stt is 49 source and
header files against 20 at v2.0, and modern rsz also depends
on dpl and on a larger stt. The declared CMake links overstate
the true closure (grt is included but not called in the repair
sources; gui is shallow; ord is app wiring EOS replaces), as
recorded in the track queue.

## 2. The version skew is worse than scoped

Scoping established that v2.0's bundled OpenSTA is be0d38e
(2021-07-08) while EOS's OpenSTA pin 0f2529d is dated
2026-10-16, and that 32 of the 39 OpenSTA headers rsz and
dbSta include differ between those two versions. The probes
below add a finding scoping did not have: EOS's 2026 OpenSTA
has removed or renamed headers and concepts that BOTH
candidate resizer pins still use.

- sta/PathRef.hh: removed. Modern OpenSTA has sta/Path.hh
  with class Path; both pins include PathRef.hh in rsz and
  dbSta sources and headers.
- sta/UnorderedMap.hh and sta/UnorderedSet.hh: removed.
  Both pins include them.
- sta/Vector.hh: removed. Both pins' dbReadVerilog.cc
  includes it.
- sta/Corner.hh and sta/DcalcAnalysisPt.hh: removed. EOS's
  STA has AnalysisCorner.hh and Scene.hh instead; the Corner
  concept was replaced by scenes. The modern pin's rsz
  sources include Corner.hh in EstimateWireParasitics.cc,
  PreChecks.cc, Rebuffer.cc, RecoverPower.cc, RepairDesign,
  RepairHold and RepairSetup, and the v2.0 sources use Corner
  through the same repair paths. This is not a bridge-only
  surface: Corner is a parameter of the repair code itself.

Consequence: there is no pin, v2.0 or ead3236, whose resizer
drops in against EOS's current STA with adaptation confined
to dbSta and facade glue. Every strategy pays a port of the
resizer's own STA-facing code, and the Corner to Scene change
reaches the signatures the repair passes are written against.

## 3. Probe method and results

Method: g++ -std=c++17 -fsyntax-only on every rsz and dbSta
.cc, with EOS's in-tree STA and odb headers first on the
include path and each candidate tree supplying its own utl,
ord, gui, grt, stt and pdr headers. Unused.cc is excluded: it
is dead code marked "on injured reserve" and is not in the
upstream CMakeLists. A scratch StaConfig.hh was generated
from EOS's own template, and a second pass added scratch
compatibility shims for the four removed headers above
(PathRef aliased to Path; the generic UnorderedMap,
UnorderedSet and Vector wrappers taken from the 2021 STA) so
deeper errors could be seen. Raw logs and per-file summaries
are kept with the R0 wake record. Two honesty notes: the raw
error totals include harness artifacts (the 2021 Vector shim
interacts with EOS's Iterator.hh, and EOS odb headers assume
a cstdint include order the syntax-only harness does not
reproduce), so totals are an upper bound on bridge drift, not
a patch count; and files stopped by a fatal missing header
report one error, so totals are simultaneously a lower bound
on the resizer-internal port, which the harness could not
reach past the header layer.

Strategy A, v2.0 rsz and dbSta against EOS STA and odb@2021:
246 errors in the shimmed pass. The dbSta bridge files that
clear the header layer carry the bulk: dbSta.cc 99,
dbNetwork.cc 71, dbReadVerilog.cc 37, dbSdcNetwork.cc 33,
classified mostly as undeclared or renamed identifiers and
signature or conversion mismatches. All six compiled rsz
sources were stopped at the header layer (PathRef,
UnorderedMap or the FLUTE include), so the resizer-internal
STA port is unmeasured, not zero.

Strategy B, ead3236 rsz and dbSta against EOS STA and
odb@2021: 588 errors in the shimmed pass. dbSta.cc 108,
dbNetwork.cc 86, SteinerTree.cc 84, SteinerRenderer.cc 69,
MakeResizer.cc 68, dbReadVerilog.cc 50, PathRenderer.cc 41,
dbSdcNetwork.cc 37, MakeDbSta.cc 36. Eight rsz sources were
stopped at the header layer, most by the removed Corner.hh.
Strategy B pays both drifts at once: the STA port above and
the odb drift (modern sources include odb/db.h and use the
modern odb API against EOS's 2021 odb), in a closure about
2.5 times the v2.0 file count.

Strategy D, re-pin odb to ead3236, then Strategy B: EOS's odb
module is 535 files in tree; the modern odb is 879 files, 710
of them under src and include. Re-importing it reopens the
odb facade (src/db/odb/src/odb_facade.cc is the one EOS-side
odb consumer outside the module) and every odb-touching test,
and it is a manifest change. It would retire the disclosed
odb format skew (EOS's v2.0 reader cannot read ORFS v3.0 odb
files). It does nothing for the STA port in Section 2, which
is the larger and less bounded half of both other strategies.

## 4. Reference numbers

- Modern resizer: the existing ead3236 binary result on the
  frozen degraded gcd, from docs/closure-ab-results.md: WNS
  gain 0.107285 ns, TNS gain 0.277089 ns, repair 0.176 s.
  That binary's STA is ead3236-era, not EOS's 2026 STA, so it
  is a reference for the algorithm, not for a ported build.
- Resizer at v2.0: no reference number was produced in R0.
  A locally built v2.0 OpenROAD binary exists on this machine
  (openroad-build/build-apt/bin/openroad), so producing the
  number is priced, not blocked: one repair run on the
  identical degraded gcd inputs plus the two-direction
  cross-check, estimated at half a day including setup and
  verification. R0 did not run it because Section 5 makes the
  number moot for the recommendation.

## 5. Verdict against the abort threshold

The track's binding abort threshold stops the track at R0 if
every strategy requires adaptation reaching into repair
algorithm semantics, or a patch volume the memo cannot bound
tightly. The second clause is met as measured: for both
importable pins, the resizer's own sources could not be
compiled past the removed-header layer against EOS's STA, so
the port volume inside Resizer, Rebuffer and the repair
passes is unmeasured; the Corner to Scene replacement changes
types that appear in repair-pass signatures, which is the
boundary between renamed-API adaptation and repair semantics,
and the probes cannot show which side the full port lands on.
A memo that named a patch count here would be inventing it.

Recommendation: do not approve R1 on any of the three
strategies as scoped. The track stops at R0 and the standing
fallback applies: accept the closure gap and move to the
Phase 3 breakdown. If the owner wants the resizer regardless,
the honest next scope is a time-boxed port spike whose only
deliverable is a compiling v2.0 dbSta plus Resizer against
EOS's STA in a scratch tree, with the patch list it actually
took; that spike, not this memo, would bound R1.

## 6. Structure proposal (for the record, not applied)

Had a strategy been approved, the proposal would have been:
rsz as a new L4 module under src/pd, dbSta as a bridge module
beside it, both recorded under the manifest's existing
openroad-modules entry at the chosen pin, with two new rows
in structure.yaml (module rsz, layer L4, path src/pd/rsz;
module dbsta, layer L4, path src/pd/dbsta) and a manifest
note naming the pin and the patch list from the memo. No
structure or manifest change is proposed for approval now,
because no strategy is recommended.
