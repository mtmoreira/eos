# dbsta: patch list (P1 port of ead3236 dbSta pieces)

Every change below lives in the port copies in this module; the
pin tree at ~/workspace/openroad-ead3236 is untouched, and no
file under src/analysis/timing/sta is modified. Classes, as in
the resizer spike memo: (M) mechanical rename/signature, (A)
API-shape adaptation, (S) semantic-mapping decision needing
validation. The in-tree STA is OpenSTA commit 0f2529d; the pin
vendors OpenSTA 7a184d00, so the drift is the same class the
spike ported for v2.0, re-derived here against the ead3236
sources (no spike file was copied blindly: the ead3236
dbNetwork differs from the v2.0 one, and each entry below was
verified against the ead3236 source and the in-tree header it
adapts to).

## dbNetwork.hh / dbNetwork.cc

- D-1 (M): const char* to std::string round. In-tree
  ConcreteNetwork::name returns std::string for Instance, Cell,
  Port and Net, so dbNetwork::name(Instance*) and name(Net*)
  change return type, and their bodies return the odb name
  directly instead of tmpStringCopy. Call sites that consumed a
  const char* name now hold a std::string and pass .c_str() at
  odb boundaries (findPin, connect, makePort, makeNet,
  makeInstance, findInstNetsMatching, findChild, findInstance,
  findNet). tmpStringCopy is deleted: it called makeTmpString,
  which is gone in-tree.
- D-2 (M): string_view signatures. linkNetwork, findInstance,
  findChild, findPin, findNet, makeInstance, makeNet and
  makePort take std::string_view where the in-tree Network /
  NetworkEdit / NetworkReader declare them, and
  highestConnectedNet takes const Net* (in-tree Network form).
  Odb calls that need const char* build a std::string first, so
  a non-terminated view is never passed to odb.
- D-3 (M): groupBusPorts predicate. In-tree groupBusPorts takes
  std::function<bool(std::string_view)>; the two call sites now
  pass string_view lambdas (makeCell keeps "assume msb first",
  makeTopCell keeps portMsbFirst). portMsbFirst itself takes
  std::string_view; its body is unchanged apart from
  std::string qualification (the pin file's bare `string` did
  not compile in-tree).
- D-4 (M): removed STA containers. net_drvr_pin_map_.findKey
  (a member in the pin era) becomes sta::findKey(map, key) from
  ContainerHelpers.hh (exists-out semantics preserved: null
  when absent). The lambda capture in makeTopCell's
  groupBusPorts call changes from [=] to [this] (implicit this
  capture is deprecated in C++20).
- D-5 (M): dead iterator deleted. DbLibraryIterator1 and its
  ConcreteLibraryIterator dependency are deleted: the iterator
  type is gone in-tree and the class is unreferenced.
- D-6 (A): nullptr filenames become empty strings. Base
  makeLibrary / makeCell take std::string_view filenames; the
  pin calls passed nullptr, which builds a string_view from
  null (the probe's -Wnonnull warnings). The port passes "".
- D-7 (A): checkLibertyCorners becomes checkLibertyScenes
  (the in-tree Network name for the same check).
- D-8 (A/S): findPgPort dropped, semantics preserved. In-tree
  LibertyCell has no findPgPort: pg pins are LibertyPorts in
  the cell's port map (LibertyReader builds them as ports, and
  findLibertyPort searches that map), so a null findLibertyPort
  result already covers the pin-era pg case. Both warning
  conditions in makeCell and readLibertyAfter drop the
  findPgPort conjunct; the warning still fires exactly when a
  LEF pin has no liberty counterpart at all. This is the one
  entry whose equivalence rests on the in-tree port-map fact
  above, not on a signature change; it is validated by the P1
  tests only in the no-liberty direction (no liberty loaded,
  so no warning path runs), and a liberty-loaded run must
  re-check it before any unit relies on the warning.
- D-9 (M): dbITerm connect/disconnect are instance methods in
  ead3236 odb (iterm->connect(dnet), iterm->disconnect()), as
  the pin code already uses them; no change was needed beyond
  D-1/D-2. Recorded here so a reader does not mistake the
  absence of a static-connect patch for an oversight.

## ord shim (EOS-owned, O-1)

- O-1 (A): include/ord/OpenRoad.hh and src/ord_shim.cc are
  EOS-authored, not port copies. The shim is a singleton the
  EOS runner fills per run (database, dbNetwork, logger, Tcl
  interpreter) and clears when the run ends, so it never dangles
  on stack objects. It provides getDb, getDbNetwork, getLogger
  and tclInterp (the accessors the gui stub and future engines
  call) plus the query free functions get_db_block,
  get_db_tech, db_has_tech and microns_to_dbu over the
  singleton database. ensure_linked is a documented placeholder
  in P1: no P1 engine calls it, and the first unit whose engine
  does must give it link-or-error semantics. The upstream Tcl
  free-function surface beyond these queries (readLef, readDef,
  linkDesign and the rest of ord::OpenRoad) is not shimmed in
  P1: EOS loads designs through its own commands, not through
  an ord application object.

## gui stub (port copies, no patches beyond include reach)

- G-1: stub.cpp and stub_heatMap.cpp compile unmodified
  against the EOS ord shim header (stub.cpp's only ord use is
  openroad->tclInterp() in initGui) and the in-tree utl Logger.
  gui.h, heatMap.h and MakeGui.h are copied unmodified. The
  stub path needs no Qt, as P0 records.

## Merged (R1 step 2): P1 ead3236 side plus v2.0 side

The merge keeps the P1 file as the base (it is the ead3236-era
port, already proven by the P1 floorplan tests and the ifp
probe) and adopts the R1 items below. D-1 through D-9, O-1 and
G-1 stand unchanged. R1 items already covered by P1 entries
(D-2 covers P-26 string_view virtuals, D-6 covers P-25 nullptr
filenames, D-1 covers the name/tmpStringCopy changes, D-4
covers the container changes, D-5 covers the dead iterator)
are not duplicated here.

- M-1 (S, adopted from R1 P-27): Pin and Instance id()
  bodies replaced with the R1 versions. P1's Pin id offset
  iterm ids by the bterm count, which can still collide at
  the boundary (iterm id 0 maps onto bterm id N), and P1's
  top-instance id was 0, indistinguishable from the STA null
  id; P1's Pin id also dereferenced bterm unconditionally
  when the pin was neither iterm nor bterm. The R1 versions
  are null-safe (null handle is 0), tag bterm ids with the
  high bit (0x80000000U, collision-free against raw iterm
  ids), and give the top instance the 0xFFFFFFFFU sentinel.
  The LoadPinIndexMap collapse the R1 version fixes (four
  distinct clock loads collapsing to one entry under the
  inherited ConcreteNetwork::id) is the reason this entry
  exists: the resizer's delay calc indexes by Pin id, and
  only an injective id keeps it sound. Net and Term id()
  keep the P1 bodies (staToDb(net)->getId() and
  staToDb(term)->getId()), which are already odb-backed and
  strictly better than the R1 side's inherited versions.
  Net/Term id() are therefore closed by this merge, not
  carried as an open item.
- M-2 (A, adopted from the R1 port): setBlock binds db_
  from the block (db_ = block->getDataBase()) before
  readDbNetlistAfter. P1's setBlock left db_ untouched, so
  a network bound by setBlock alone (the cellWireDelay
  scratch path, which never calls init) had a null database
  for dbuToMeters and the tech queries. When init was
  already called this assigns the same database again.
- M-3 (A, EOS-owned adaptation of dbSta.cc): the SWIG init
  call (Dbsta_Init) and the encoded dbSta Tcl eval
  (evalTclInit with dbSta_tcl_inits) are dropped: dbSta.tcl
  is not imported, EOS drives via the C++ API plus its own
  registry, and the EOS session performs in-tree STA Tcl
  init itself where a Tcl session is needed. makeComponents
  binds the network through dbNetwork::init (the merged
  module's single db+logger binding) when db_ and logger_
  are set; on the scratch path both are null and the
  network binds later via setBlock (M-2). The dbSta
  constructor nulls logger_, db_network_ and db_report_
  (the port left logger_ uninitialized, which the
  makeComponents guard would otherwise read). dbSta.cc
  drops its <tcl.h> include: Sta.hh forward-declares
  Tcl_Interp and this file calls no Tcl C function after
  the adaptation. Tcl headers are still required transitively
  (sta/ReportTcl.hh includes <tcl.h>), so the target keeps
  P1's TCL conditional include/link block; the change only
  removes dbSta.cc's direct dependency. The setLogger open item is closed
  for the main path (init binds logger_ through
  makeComponents); the scratch path intentionally carries
  no dbNetwork logger, matching the validated step-1 run.
- M-4 (A, dbSdcNetwork re-derivation): the v2.0
  dbSdcNetwork methods are dropped, not ported. They take
  const char* paths and pointer out-params, which override
  no 2026 base virtual (the 2026 SdcNetwork uses
  string_view paths and return-by-value sequences), so they
  were unreachable through the base pointer Sta holds, and
  the step-1 repair run validated with the base methods
  doing the work. The 2026 base already implements the
  divider/bracket escape fallbacks the v2.0 methods
  carried (SdcNetwork::findInstance and
  findInstanceRelative). The class stays as the concrete
  type dbSta::makeSdcNetwork returns, constructor only.
- M-5 (M): dbReadVerilog carried as ported (P-0 include
  prefix, P-11 explicit VerilogReader lifecycle already
  applied in the port copy). No further adaptation: it
  uses only base Network methods (report, debug,
  findAnyCell) that the P1 dbNetwork inherits.

## R1 patch disposition (complete set, merged module)

- P-25 (nullptr filename): covered by D-6, unchanged.
- P-26 (string_view virtuals): covered by D-2, unchanged.
- P-27 (Pin/Instance id): adopted, see M-1.
- P-28 (graph refresh in repairSetup): lives in the rsz
  module, not here (see the rsz PATCHES.md).
- P-29 (InstanceSet/PinSet comparator binding): lives in
  the rsz module and dbSta init members where they occur.
- P-30 (scratch-sta own interp): rule for consumers of
  this module (see the module README); no code here.
- P-31 (unique scratch block names): lives in the rsz
  module (cellWireDelay path).
- P-32 (dbSta::setReportLogger): carried in dbSta.hh/cc;
  every scratch-sta construction site must call it.
- P-33 (driver-only wire RC): becomes the Q6 policy, not
  a code patch; see the rsz module and the session layer.
- P-12 (groupBusPorts predicate returning false): SUPERSEDED
  by D-3 (P1's treatment preserves upstream intent: makeCell
  assumes msb first, makeTopCell keeps portMsbFirst). The
  R1 predicate is unvalidated and inert for gcd (no buses).
  Bus-ordering validation against liberty ordering is a
  phase-2 work item before any unit relies on the predicate.
- D-8 (findPgPort drop): re-check status at merge time:
  the P1 tests cover only the no-liberty direction. A
  liberty-loaded run must re-check the warning path before
  any unit relies on it; that re-check rides with the rsz
  Tcl suite (repair runs load liberty).
- P-3/P-22 (ord/gui strips): stand; no PathRenderer, no
  highlightSteiner reintroduction in the adapter. GUI
  re-wire is a later EOS GUI decision.
