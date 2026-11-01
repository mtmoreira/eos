# dbsta (L4 engines)

The OpenROAD dbSta network bridge: sta::dbNetwork, the adapter
between OpenDB and the in-tree OpenSTA network API, merged from
the OpenDB pin commit ead3236 (P1 side) and OpenROAD v2.0 at
a5a56584 (R1 side), plus the gui stub and the EOS-owned ord
shim surface.

Status: adopted in place-and-route unit P1, as the bridge the
floorplan engine (ifp) needs, and extended in R1 step 2 with
the merged adapter: dbSta itself, dbReadVerilog and
dbSdcNetwork join dbNetwork in this module, so the resizer
(rsz) and any later engine share one adapter instead of a
second parallel implementation. The port copies, their
adaptations to the in-tree STA API and the licence audit are
documented in PROVENANCE.md and PATCHES.md.

Rules for consumers of this module (from the R1 port, kept
here so no consumer re-learns them by crashing):

- A scratch STA (the cellWireDelay path) creates its own Tcl
  interpreter, never shares the main one (the 2026 ReportTcl
  destructor unstacks channels unconditionally), and calls
  dbSta::setReportLogger at construction (a null report
  logger segfaults inside errorMsg on delay-calc errors).
  Full dbSta::init is not used for a scratch STA: it would
  hijack the global Sta singleton via Sta::setSta.
- Wire signal RC is caller configuration, never an engine
  default: with it unset, wire-length math divides by zero
  and the zero-RC pi model NaNs. The Q6 policy (tech-derived
  default, overridable by command, fail-fast backstop) lives
  in the session layer, not in this module; repair reads the
  scenario's values and never derives them.
- The dbSta Tcl init surface (Dbsta_Init, dbSta_tcl_inits)
  does not exist in this module: dbSta.tcl is not imported.
  The EOS session performs in-tree STA Tcl init itself.

The stable public API of this module is the ported db_sta
header (include/db_sta/dbNetwork.hh) and the ord shim header
(include/ord/OpenRoad.hh), versioned per the EOS Code Structure document.
