// dbSdcNetwork: the SDC network adapter for the merged dbsta module.
//
// The v2.0 dbSdcNetwork methods (const char* paths, pointer out-params)
// do not override any 2026 base virtual: the 2026 SdcNetwork uses
// string_view paths and return-by-value sequences, and already
// implements the divider/bracket escape fallbacks the v2.0 methods
// carried (see SdcNetwork::findInstance and findInstanceRelative).
// The v2.0 methods were therefore unreachable through the base
// pointer Sta holds, and the step-1 repair run validated with the
// base methods doing the work. This class keeps the concrete type
// dbSta::makeSdcNetwork returns, with no dead methods. See
// PATCHES.md entry M-4.
#ifndef DB_SDC_NETWORK_H
#define DB_SDC_NETWORK_H

#include "sta/SdcNetwork.hh"

namespace sta {

class dbSdcNetwork : public SdcNetwork
{
public:
  dbSdcNetwork(Network *network);
};

} // namespace sta
#endif
