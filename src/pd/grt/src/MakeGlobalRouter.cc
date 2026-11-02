// MakeGlobalRouter: EOS-owned factory, see include/grt/MakeGlobalRouter.h.
#include "grt/MakeGlobalRouter.h"

#include <memory>

#include "grt/GlobalRouter.h"
#include "heatMap.h"

namespace grt {

GlobalRouter* MakeGlobalRouter(utl::Logger* logger,
                               stt::SteinerTreeBuilder* stt_builder,
                               odb::dbDatabase* db, sta::dbSta* sta,
                               rsz::Resizer* resizer,
                               ant::AntennaChecker* antenna_checker,
                               dpl::Opendp* opendp)
{
  GlobalRouter* router = new GlobalRouter();
  router->init(logger, stt_builder, db, sta, resizer, antenna_checker,
               opendp,
               std::make_unique<RoutingCongestionDataSource>(logger, db));
  return router;
}

void DeleteGlobalRouter(GlobalRouter* router)
{
  delete router;
}

}  // namespace grt
