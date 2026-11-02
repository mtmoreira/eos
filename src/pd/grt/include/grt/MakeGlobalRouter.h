// MakeGlobalRouter: EOS-owned factory for the global router (L4
// engines). Upstream's MakeGlobalRouter is Tcl/SWIG glue and is not
// imported; EOS registers no Tcl commands through the engines, so
// this factory is the only construction path. It builds the router
// with the routing-congestion heat-map data source the router's
// init requires (heatMap.cpp, internal to this module); callers
// never reach into this module's src/ directory for it.
//
// The caller owns the returned router and must delete it with
// DeleteGlobalRouter (or delete) after the run; the router does
// not own the logger, database, STA session, resizer, antenna
// checker, or detailed placer passed in.
#pragma once

namespace odb {
class dbDatabase;
}  // namespace odb

namespace sta {
class dbSta;
}  // namespace sta

namespace utl {
class Logger;
}  // namespace utl

namespace ant {
class AntennaChecker;
}  // namespace ant

namespace dpl {
class Opendp;
}  // namespace dpl

namespace rsz {
class Resizer;
}  // namespace rsz

namespace stt {
class SteinerTreeBuilder;
}  // namespace stt

namespace grt {

class GlobalRouter;

// Constructs a GlobalRouter and initializes it with the given
// engines. All pointer arguments except resizer must be non-null;
// resizer may be null when timing-driven routing is off (the
// placement path), matching Replace's null-resizer tolerance.
GlobalRouter* MakeGlobalRouter(utl::Logger* logger,
                               stt::SteinerTreeBuilder* stt_builder,
                               odb::dbDatabase* db, sta::dbSta* sta,
                               rsz::Resizer* resizer,
                               ant::AntennaChecker* antenna_checker,
                               dpl::Opendp* opendp);

void DeleteGlobalRouter(GlobalRouter* router);

}  // namespace grt
