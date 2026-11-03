# extract: port adaptations (EOS-owned changes to the imported copies)

The pin tree is never modified. Every change below lives only in
the EOS port copies in this module, and is recorded here so the
difference from upstream ead3236 is auditable. Nothing else in
this module differs from upstream except the EOS-owned files
(CMakeLists.txt, README.md, PROVENANCE.md, this file, LICENSE,
src/extract.cc, and include/eos/pv/extract.h) and the port copies
listed below, if any.

## 1. Not imported

- MakeOpenRCX.cpp: Tcl/SWIG factory, not imported. The EOS runner
  constructs rcx::Ext directly.
- MakeOpenRCX.h: the factory header, not imported for the same
  reason.
- All .tcl and .i (SWIG) surfaces, the upstream test directory,
  calibration, doc, and example directories: EOS writes its own
  CMakeLists and its own tests.
- No EOS-owned shim headers were needed: unlike drt, rcx references
  no ord singleton, no Tcl interpreter, and no distributed engine,
  so the imported sources build against the in-tree odb and utl
  headers unchanged.

## 2. Build notes (EOS-owned, not engine changes)

- The module links eda_pd_dbsta, eda_db_odb, eda_analysis_sta, and
  eda_foundation_errors, the same in-tree dependencies the route
  runner uses; no new system dependency is introduced by this
  import (no Boost, OpenMP, or ZLIB requirement beyond what the
  in-tree libraries already carry).
- The extractor's RC tables come from a rules file supplied per run
  (the runner's model_path); EOS ships no default model file.
