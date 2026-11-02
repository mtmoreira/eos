// place_resizer: see place_resizer.h. This unit includes only the
// resizer headers, never the ead3236 Steiner-tree header, so the
// stt::Branch/stt::Tree definitions never collide here.
#include "place_resizer.h"

#include "odb/db.h"
#include "rsz/MakeResizer.hh"
#include "rsz/Resizer.hh"
#include "utl/Logger.h"

namespace eos::pd {

rsz::Resizer* CreateTimingResizer(utl::Logger* logger,
                                  odb::dbDatabase* db, sta::dbSta* sta)
{
  rsz::Resizer* resizer = ord::makeResizer();
  // 0.3 is the global router's default routing alpha; the resizer
  // uses it to weight routing resources when estimating wire
  // parasitics for slack.
  resizer->init(nullptr, logger, db, sta, 0.3);
  return resizer;
}

void DeleteTimingResizer(rsz::Resizer* resizer)
{
  ord::deleteResizer(resizer);
}

}  // namespace eos::pd
