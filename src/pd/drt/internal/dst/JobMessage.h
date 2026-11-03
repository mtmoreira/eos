// EOS-owned shim for the distributed-routing message types (L4 engines).
//
// This header is NOT an upstream file and NOT a copy of one: it is
// written fresh in EOS style to give the drt port copies the small
// dst API surface they reference on distributed-only paths. EOS
// runs single-host only: the runner never enables distributed mode
// and never constructs a dst::Distributed, so these types are link
// closure for dead code paths, never a distributed implementation.
// The real dst engine (Distributed.cc, JobMessage.cc, workers, load
// balancer) is not imported. See src/pd/drt/PATCHES.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace boost::serialization {
class access;
}  // namespace boost::serialization

namespace dst {

// Base of every distributed job description. The derived descriptions
// in drt's distributed/ directory serialize through this base.
class JobDescription
{
 public:
  JobDescription() = default;
  virtual ~JobDescription() = default;

 private:
  template <class Archive>
  void serialize(Archive& /*ar*/, const unsigned int /*version*/)
  {
  }
  friend class boost::serialization::access;
};

// A distributed job message: a type, a destination scope, and the
// descriptions it carries. Only the accessors drt uses are provided.
class JobMessage
{
 public:
  enum JobType : int8_t
  {
    ROUTING,
    UPDATE_DESIGN,
    BALANCER,
    PIN_ACCESS,
    GRDR_INIT,
    SUCCESS,
    ERROR,
    NONE
  };
  enum MessageType : int8_t
  {
    UNICAST,
    BROADCAST
  };

  JobMessage(JobType job_type = NONE, MessageType msg_type = UNICAST)
      : msg_type_(msg_type), job_type_(job_type)
  {
  }

  void setJobDescription(std::unique_ptr<JobDescription> in)
  {
    desc_ = std::move(in);
  }
  std::unique_ptr<JobDescription>& getJobDescriptionRef() { return desc_; }
  void addJobDescription(std::unique_ptr<JobDescription> in)
  {
    descs_.push_back(std::move(in));
  }
  const std::vector<std::unique_ptr<JobDescription>>& getAllJobDescriptions()
  {
    return descs_;
  }
  void setJobType(JobType in) { job_type_ = in; }
  JobDescription* getJobDescription() { return desc_.get(); }
  JobType getJobType() const { return job_type_; }
  MessageType getMessageType() const { return msg_type_; }

 private:
  MessageType msg_type_;
  JobType job_type_;
  std::unique_ptr<JobDescription> desc_;
  std::vector<std::unique_ptr<JobDescription>> descs_;
};

}  // namespace dst
