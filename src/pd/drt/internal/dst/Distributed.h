// EOS-owned shim for the distributed-routing backend (L4 engines).
//
// This header is NOT an upstream file and NOT a copy of one: it is
// written fresh in EOS style to give the drt port copies the small
// dst API surface they reference on distributed-only paths. EOS
// runs single-host only and never constructs a dst::Distributed, so
// every method here throws: a distributed call is a loud failure,
// never a silent no-op and never a network operation. The real dst
// engine is not imported. See src/pd/drt/PATCHES.md.
#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace dst {

namespace asio = boost::asio;
using asio::ip::tcp;
using socket = asio::basic_stream_socket<tcp>;

class JobMessage;
class JobCallBack;

class Distributed
{
 public:
  Distributed() = default;
  ~Distributed() = default;

  bool sendJob(JobMessage& /*msg*/,
               const char* /*ip*/,
               unsigned short /*port*/,
               JobMessage& /*result*/)
  {
    throw std::runtime_error(
        "distributed routing is not supported in EOS");
  }
  bool sendJobMultiResult(JobMessage& /*msg*/,
                          const char* /*ip*/,
                          unsigned short /*port*/,
                          JobMessage& /*result*/)
  {
    throw std::runtime_error(
        "distributed routing is not supported in EOS");
  }
  bool sendResult(JobMessage& /*msg*/, socket& /*sock*/)
  {
    throw std::runtime_error(
        "distributed routing is not supported in EOS");
  }
  void addCallBack(JobCallBack* cb) { callbacks_.push_back(cb); }
  const std::vector<JobCallBack*>& getCallBacks() const
  {
    return callbacks_;
  }

 private:
  std::vector<JobCallBack*> callbacks_;
};

}  // namespace dst
