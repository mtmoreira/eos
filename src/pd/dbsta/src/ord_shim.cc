// ord shim implementation: EOS-owned glue, see include/ord/OpenRoad.hh.
#include "ord/OpenRoad.hh"

#include "odb/db.h"

namespace ord {

OpenRoad* OpenRoad::openRoad()
{
  static OpenRoad instance;
  return &instance;
}

odb::dbBlock* get_db_block()
{
  odb::dbDatabase* db = OpenRoad::openRoad()->getDb();
  if (db == nullptr || db->getChip() == nullptr) {
    return nullptr;
  }
  return db->getChip()->getBlock();
}

odb::dbTech* get_db_tech()
{
  odb::dbDatabase* db = OpenRoad::openRoad()->getDb();
  if (db == nullptr) {
    return nullptr;
  }
  return db->getTech();
}

bool db_has_tech()
{
  return get_db_tech() != nullptr;
}

int microns_to_dbu(double microns)
{
  odb::dbTech* tech = get_db_tech();
  if (tech == nullptr) {
    return 0;
  }
  return static_cast<int>(microns * tech->getDbUnitsPerMicron());
}

void ensure_linked()
{
  // Placeholder in P1: no P1 engine calls this. See the header note.
}

}  // namespace ord
