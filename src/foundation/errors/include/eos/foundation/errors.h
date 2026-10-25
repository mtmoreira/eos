// errors: error and result types (L0 foundation).
//
// The EOS core does not throw exceptions across module boundaries.
// Fallible operations return Status (no value) or Result<T> (a value or
// a Status). ErrorCode is deliberately generic: L0 must not know what a
// net, a scenario or a stale revision means to any particular caller,
// only the shape of the failure. Callers that need domain detail put it
// in the message.
//
// This header is a stable surface.
#ifndef EOS_FOUNDATION_ERRORS_H_
#define EOS_FOUNDATION_ERRORS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace eos::foundation {

enum class ErrorCode : std::int32_t {
  kOk = 0,
  kInvalidArgument = 1,
  kNotFound = 2,
  kInvalidState = 3,
  kStaleRevision = 4,
  kOutOfRange = 5,
  kIoError = 6,
  kUnsupported = 7,
};

// A success or failure with no payload. Construct failures with
// Status::Error(code, message); success is Status::Ok().
class Status {
 public:
  Status() : code_(ErrorCode::kOk) {}
  Status(ErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  static Status Ok() { return Status(); }
  static Status Error(ErrorCode code, std::string message) {
    return Status(code, std::move(message));
  }

  bool ok() const { return code_ == ErrorCode::kOk; }
  ErrorCode code() const { return code_; }
  const std::string& message() const { return message_; }

 private:
  ErrorCode code_;
  std::string message_;
};

// A value, or the Status explaining why there is none. value() has the
// precondition ok(); check ok() or status() first.
template <typename T>
class Result {
 public:
  Result(T value) : value_(std::move(value)) {}
  Result(Status status) : status_(std::move(status)) {}

  bool ok() const { return value_.has_value(); }
  const Status& status() const { return status_; }
  const T& value() const& { return *value_; }
  T& value() & { return *value_; }
  T&& value() && { return std::move(*value_); }

 private:
  std::optional<T> value_;
  Status status_;
};

}  // namespace eos::foundation

#endif  // EOS_FOUNDATION_ERRORS_H_
