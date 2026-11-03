// EOS-owned shim for the load-balancer job description (L4
// engines). Written fresh in EOS style, not an upstream copy: it
// only carries the worker address FlexDR reads back on the
// distributed path, which EOS never takes. Distributed mode is not
// supported in EOS. See src/pd/drt/PATCHES.md.
#pragma once

#include <boost/serialization/base_object.hpp>
#include <string>

#include "dst/JobMessage.h"

namespace boost::serialization {
class access;
}  // namespace boost::serialization

namespace dst {

class BalancerJobDescription : public JobDescription
{
 public:
  BalancerJobDescription() : worker_port_(0) {}
  void setWorkerIP(const std::string& ip) { worker_ip_ = ip; }
  void setWorkerPort(unsigned short port) { worker_port_ = port; }
  std::string getWorkerIP() const { return worker_ip_; }
  unsigned short getWorkerPort() const { return worker_port_; }

 private:
  std::string worker_ip_;
  unsigned short worker_port_;

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version)
  {
    (ar) & boost::serialization::base_object<dst::JobDescription>(*this);
    (ar) & worker_ip_;
    (ar) & worker_port_;
  }
  friend class boost::serialization::access;
};

}  // namespace dst
