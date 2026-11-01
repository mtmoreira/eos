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
