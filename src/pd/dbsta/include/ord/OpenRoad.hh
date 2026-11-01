// ord shim: the EOS-owned OpenRoad session surface (L4 engines).
//
// Upstream engines include ord/OpenRoad.hh and reach the design
// through ord::OpenRoad accessors. EOS has no upstream OpenRoad
// application object: the session owns the database, so this shim
// is a small singleton the EOS runner fills in before an engine
// runs (database, dbNetwork, logger, Tcl interpreter when one
// exists) and the engines read back. It is EOS-owned glue, not an
// upstream file, and it grows unit by unit: P1 provides the
// accessors the floorplan closure uses (getDb, getDbNetwork,
// getLogger, tclInterp) plus the query free functions P0 section
// 5a records (get_db_block, get_db_tech, db_has_tech,
// microns_to_dbu). ensure_linked is a placeholder in P1: no P1
// engine calls it, and the first unit whose engine does must give
// it link-or-error semantics instead of assuming this no-op.
//
// This header is EOS-owned code, not a port copy: it carries no
// upstream text and no licence header beyond the module's own.
#pragma once

extern "C" {
struct Tcl_Interp;
}

namespace odb {
class dbBlock;
class dbDatabase;
class dbTech;
}  // namespace odb

namespace sta {
class dbNetwork;
}  // namespace sta

namespace utl {
class Logger;
}  // namespace utl

namespace ord {

class OpenRoad
{
 public:
  // Process-wide shim instance, owned by the shim itself.
  static OpenRoad* openRoad();

  Tcl_Interp* tclInterp() const { return tcl_interp_; }
  utl::Logger* getLogger() const { return logger_; }
  odb::dbDatabase* getDb() const { return db_; }
  sta::dbNetwork* getDbNetwork() const { return db_network_; }

  void setTclInterp(Tcl_Interp* interp) { tcl_interp_ = interp; }
  void setLogger(utl::Logger* logger) { logger_ = logger; }
  void setDb(odb::dbDatabase* db) { db_ = db; }
  void setDbNetwork(sta::dbNetwork* network) { db_network_ = network; }

 private:
  OpenRoad() = default;

  Tcl_Interp* tcl_interp_ = nullptr;
  utl::Logger* logger_ = nullptr;
  odb::dbDatabase* db_ = nullptr;
  sta::dbNetwork* db_network_ = nullptr;
};

// Query helpers over the shim singleton's database. They return
// null / false / 0 when no database is set; they never create one.
odb::dbBlock* get_db_block();
odb::dbTech* get_db_tech();
bool db_has_tech();
// Converts microns to database units using the singleton
// database's technology; 0 when no technology is set.
int microns_to_dbu(double microns);
// Placeholder in P1 (see header note): returns without action.
// No P1 engine calls it.
void ensure_linked();

}  // namespace ord
