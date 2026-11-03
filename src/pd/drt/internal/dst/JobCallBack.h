// EOS-owned shim for the distributed-routing callback interface
// (L4 engines). Written fresh in EOS style, not an upstream copy:
// it only declares the interface drt's RoutingCallBack implements.
// Distributed mode is not supported in EOS (see Distributed.h).
// See src/pd/drt/PATCHES.md.
#pragma once

#include "dst/Distributed.h"

namespace dst {

class JobMessage;

class JobCallBack
{
 public:
  virtual void onRoutingJobReceived(JobMessage& msg, socket& sock) = 0;
  virtual void onFrDesignUpdated(JobMessage& msg, socket& sock) = 0;
  virtual void onPinAccessJobReceived(JobMessage& msg, socket& sock) = 0;
  virtual void onGRDRInitJobReceived(JobMessage& msg, socket& sock) = 0;
  virtual ~JobCallBack() = default;
};

}  // namespace dst
