// cts_resizer: see cts_resizer.h. This unit includes only the
// resizer headers, never the ead3236 Steiner-tree header, so the
// stt::Branch/stt::Tree definitions never collide here.
#include "cts_resizer.h"

#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "rsz/MakeResizer.hh"
#include "rsz/Resizer.hh"
#include "utl/Logger.h"

namespace eos::pd {

rsz::Resizer* CreateCtsResizer(utl::Logger* logger, odb::dbDatabase* db,
                               sta::dbSta* sta)
{
  rsz::Resizer* resizer = ord::makeResizer();
  // 0.3 is the global router's default routing alpha, the same
  // value the place runner uses; the resizer uses it to weight
  // routing resources when estimating wire parasitics.
  resizer->init(nullptr, logger, db, sta, 0.3);
  return resizer;
}

void DeleteCtsResizer(rsz::Resizer* resizer)
{
  ord::deleteResizer(resizer);
}

}  // namespace eos::pd
