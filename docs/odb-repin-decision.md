# ODB re-pin decision memo (unit F6)

Date: 2026-10-28
Status: unit F6 deliverable, fix track. Decision record only:
this memo measures and prices re-pinning EOS's OpenDB module
from OpenROAD v2.0 to the OpenROAD commit ORFS v3.0 pins,
and recommends a course. It does not change any pin. The
re-pin itself, if approved, is a separate unit gated on the
project owner's decision recorded in the fix track queue.

## 1. The question and the two pins

P2-5 (docs/phase2-exit-report.md): EOS's ODB pin (OpenROAD
v2.0) cannot read the ORFS v3.0 `6_final.odb` binary; the
newer writer's format version is rejected by the v2.0
reader, so every gcd evidence flow in Phases 2 and 3
started from the same database exported to DEF instead.
A newer ODB pin is a manifest decision for the owner; this
memo is the measurement that decision was waiting for.

- Current pin: OpenROAD v2.0, commit
  a5a56584fed1523f384e2944ee8439256ab5e12d (2021-07-16),
  recorded in third_party_manifest.yaml (component opendb,
  module src/db/odb, licence BSD-3-Clause, patches 0).
- Candidate pin: OpenROAD commit
  ead3236e56d57362a8dc8b25658fb41a191eba17, the OpenROAD
  pin of ORFS v3.0 (the flow that wrote the subject file;
  provenance in docs/phase0-exit-report.md Section 4 and
  the local build recipe for that tree). This memo prices
  exactly this candidate. A pin at a later upstream master
  is a different, unmeasured decision: none of the counts
  below carry over to it.

Both trees are on disk locally and every count below was
measured on them in this unit (method stated per count);
no number is taken from upstream documentation.

## 2. The decisive probe

Subject: the ORFS v3.0 sky130hd gcd final database,
`6_final.odb` (SHA-256
80fa96acabc1c072698385b75964d00c3a0691d65583166af1fc4f2037d0becd,
2.5 MB). Its stream header carries schema major 0, minor
74. The two readers' own constants frame the result before
any run: the v2.0 reader is schema minor 51
(src/db/odb/src/db/dbDatabase.h, db_schema_minor = 51,
initial 51); the candidate reader is minor 74 with initial
57, and rejects files above its own minor.

- Candidate reader (the built ead3236 OpenROAD binary,
  Tcl `read_db` on the subject file): PASS, exit 0. Top
  block gcd, 1046 instances, 394 nets, 54 block terms,
  2 libraries. `write_def` and `write_db` round-trips both
  succeed; the written DEF recounts 1046 COMPONENTS and
  392 signal NETS plus 2 special nets, matching the 394.
- Current reader (EOS's own facade, OdbDatabase::ReadDb
  on the same file, probe program in the unit evidence):
  REJECTED, as P2-5 recorded. ReadDb returns kIoError
  ("could not read database file"), the facade's
  documented result for a file written by an
  incompatible database version; the probe exits 1 with
  no top block. The same facade's WriteDb / ReadDb round
  trip on its own files passes in the odb unit tests, so
  the rejection is the version gate, not a broken reader.

The format gap is therefore exactly one-directional and
exactly as P2-5 recorded: the candidate reads what the
current pin cannot, and nothing in the probe suggests the
reverse problem (EOS-written databases are read back by
EOS's own checkpoint restore path, which is a same-pin
round trip either way; see Section 5 for the checkpoint
consequence).

## 3. Size of the candidate module against the current one

Counting method: every regular file under the module tree,
tests and docs included, on both sides.

| Measure | Current (v2.0, in EOS) | Candidate (ead3236, upstream src/odb) |
|---|---|---|
| Total files in module tree | 535 | 887 |
| Files under src + include | 529 (525 imported, plus the facade, fmt_compat.h and 2 .gitkeep files) | 711 |
| Public headers | 63 (60 opendb + 3 utl) | 42 (odb) |
| Built .cpp/.cc sources | 222 upstream + facade + 2 bison parsers | 300 under src, 297 excluding swig |

Notes on the candidate count: upstream test/ carries 169
of the 887 files and, by the same rule the v2.0 import
applied, would not be imported; the import-relevant delta
is the src + include line, 711 against the v2.0 import's
525 imported files (the module PROVENANCE inventory), a net
growth of about 186 files. The layout also changes shape:
the public headers move from include/opendb to include/odb
(the Z* and ISdb/IZdcr headers leave the public set), the
tm, zlib and utl subdirectories leave the odb tree (the
object factory is inside db in the candidate; utl becomes
the separate OpenROAD src/utl module, 19 library files
against the 5 utl files EOS carries inside odb today, and
the candidate odb links it as utl_lib), and codeGenerator
grows from 27 to 50 files (kept for provenance, not built,
as today).

## 4. The adapter surface the re-pin touches

EOS was built so this decision would be cheap to price:
upstream odb types appear in exactly one EOS-authored
translation unit, src/db/odb/src/odb_facade.cc (825 lines),
behind include/eos/db/odb.h (194 lines), which exposes no
upstream type. Everything else consumes the facade:

- Direct consumers (link eda_db_odb): L3 timing service,
  L4 ECO transactions, L4 timing ECO, L5 session flow,
  L6 command registry. Five modules.
- Tests: the odb unit tests (14 cases) plus the timing,
  transactions, timing ECO, flow and registry unit tests
  and the Phase 2 qualification suite, all through the
  facade. No test includes an upstream odb header.

The facade uses 16 upstream types (dbDatabase, dbChip,
dbBlock, dbLib, dbMaster, dbMTerm, dbInst, dbITerm,
dbBTerm, dbNet and the defin, defout, lefin readers among
them). Checking each facade-called entry point against
both pins' headers, four signature breaks are certain:

1. dbDatabase::read / write: FILE* becomes std::istream&
   and std::ostream&. The facade's ReadDb and WriteDb
   (the binary path itself) change from fopen/fclose to
   file streams.
2. lefin::createTechAndLib gains a technology name:
   (lib_name, lef_file) becomes (tech_name, lib_name,
   lef_file). The facade's first-LEF path changes.
3. lefin::createLib gains the technology:
   (name, lef_file) becomes (dbTech*, name, lef_file).
   The facade's later-LEF path must obtain and hold the
   dbTech the first read created.
4. defin::createChip gains a mandatory dbTech* third
   argument. The facade's ReadDef path changes with it.

Unchanged at signature level: the lefin, defin and defout
constructors, defout::writeBlock, dbDatabase::create and
destroy, and dbInst::swapMaster. The remaining facade
calls (about forty getters, finders and mutators across
the 16 types) were not compile-probed against the
candidate headers in this unit; the honest bound is the
facade itself: any long-tail drift lands inside one
825-line file and its unit tests, and the first step of
the execution plan below is the syntax probe that turns
that bound into a list before any import happens.

## 5. Price of the migration

Work items, in the order an execution unit would meet
them:

1. Probe: syntax-compile the current facade against the
   candidate headers (the R0 method) and list every
   remaining break beyond the four in Section 4. Half a
   day, bounded by construction.
2. Import: replace the module tree with the candidate
   src/odb (711 src + include files, tests and swig
   excluded on the v2.0 import's stated rules), plus the
   candidate src/utl library files the module now links;
   rewrite PROVENANCE.md with the file-by-file inventory,
   and rewrite the module CMakeLists for the new glob
   sets (db, def, defin, defout, lef, lefin, lefout, cdl,
   zutil, utl; no tm, no zlib). Zero upstream patches is
   the standing target; build accommodations stay at
   build level as they are today (the cstdint force
   include may or may not still be needed; the probe
   answers that).
3. Facade adaptation: the four certain breaks in
   Section 4 plus the probe's list, all inside
   odb_facade.cc and, if streams change error handling,
   its header comments. The facade's public surface
   (eos/db/odb.h) is not expected to change.
4. Manifest and provenance: third_party_manifest.yaml
   opendb entry moves to ead3236 with its reason recorded
   in its own commit (a reviewed change, per the
   manifest's own rule). The openroad-modules entry
   shares today's v2.0 commit; its only imported-as-
   planned consumer, src/synth/openroad-syn, is a planned
   adoption not yet imported, so leaving that entry at
   v2.0 costs a manifest note, not code. That split is
   part of the decision in Section 7.
5. Knock-on format consequence, stated plainly: EOS's
   own WriteDb checkpoint files are schema minor 51
   today and would be minor 74 after the re-pin. Old
   checkpoints do not survive the re-pin (the new reader's
   initial is 57 and the header check is only the first
   gate; per-object streams changed across those
   revisions). Checkpoints are runtime artifacts, not
   committed fixtures; nothing in the tree stores one.
   The reverse direction matters more: after the re-pin
   EOS reads the ORFS v3.0 outputs directly, which is
   the point.

Regression risk, named: the verified STA correlation is
the asset at risk. The timing service eats designs
through the facade (WriteVerilog from the database), so
a behavioural change in the candidate's DEF/LEF readers
or database iteration order could move the numbers in
docs/extraction-correlation.md and its F2 addendum
without any EOS code changing. The risk is real but
checkable, because those numbers are recorded with
tolerances.

## 6. Re-verification plan (what green would mean)

An execution unit is done only when all of these pass,
in this order:

1. The facade syntax probe list is closed (every break
   adapted, none suppressed).
2. Local gates, all green: cmake configure and build,
   ctest (24 suites, 227 GoogleTest cases at the F5
   count), check_layers.py, pytest tools/tests (11).
3. Odb unit tests green, plus one new measured check the
   re-pin exists for: EOS's facade ReadDb on the real
   ORFS v3.0 gcd 6_final.odb (local evidence file, never
   committed) reporting block gcd with 1046 instances
   and 394 nets, the candidate probe's counts from
   Section 2, and a WriteDb / ReadDb round trip of that
   database through the new pin.
4. STA correlation re-run: the unit 3.2 / F2 timing
   comparison re-executed on the re-pinned build, judged
   on the tolerances those documents prestated (setup
   WNS delta 0.10 ns, hold WNS delta 0.05 ns, setup TNS
   delta 0.50 ns). A miss is reported as measured and
   stops the unit; it is not waived inside it.
5. CI green at check-run level on the PR and post-merge,
   as every unit in this track.

## 7. What the re-pin unlocks, and what it does not

Unlocks:

- Direct .odb ingestion of ORFS v3.0 outputs. The DEF
  export detour in the Phase 2 and 3 evidence flows
  (and the disclosed limitation in every report that
  cites P2-5) retires.
- The odb half of the resizer problem. R0
  (docs/rsz-inventory.md, Strategy D) measured the modern
  rsz + dbSta closure failing against EOS on two drifts
  at once: the odb drift and the OpenSTA drift. Re-pinning
  odb to ead3236 removes the odb drift entirely, so a
  future dbSta bridge would compile against the same odb
  it was written for.

Does not unlock, stated as plainly:

- The resizer itself. R0's larger half stands untouched:
  EOS's 2026 OpenSTA removed or renamed the headers and
  the Corner concept the resizer's own sources are
  written against, on both candidate pins. A resizer
  still needs the time-boxed port spike R0 recommended;
  the re-pin only removes one of its two obstacles.
- P2-1 and P2-2. Their remaining gaps need incremental
  STA machinery, which neither the re-pin nor dbSta
  alone supplies. They stay excluded from scope until
  that machinery exists.
- Any other pin sharing the v2.0 commit or the manifest:
  opensta, abc and the rest are separate decisions.

## 8. Options and recommendation

Option A: re-pin odb (and only odb) to ead3236, executed
as one priced unit under Section 6. Cost: the import and
facade work in Section 5, one manifest change, and the
checkpoint format break in Section 5 item 5, which costs
nothing stored. Benefit: Section 7's unlocks, and the
P2-5 limitation closes.

Option B: stay at v2.0. Cost: zero now. The limitation
stays disclosed, the DEF detour stays in every future
ORFS-facing flow, and the odb drift half of any future
resizer work stays. The pin also ages: every future ORFS
upgrade widens a gap that is 23 schema revisions wide
today (51 to 74).

Option C: re-pin to a later upstream master instead.
Unmeasured in this memo (Section 1); it would need its
own probe and counts, and it buys nothing over Option A
for the ORFS v3.0 problem, since ead3236 is the writer's
own pin.

Recommendation: Option A, when the owner judges the
resizer path or direct ORFS ingestion worth the unit it
costs; there is no external deadline forcing it now, and
staying at v2.0 (Option B) remains defensible until one
of those two needs is live. The decision, and its date,
belong in the fix track queue; this memo is the record
the decision is taken on.
