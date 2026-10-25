// ids: persistent object ID allocation (L0 foundation).
#include "eos/foundation/ids.h"

namespace eos::foundation {

Result<ObjectId> IdAllocator::Allocate(ObjectKind kind) {
  const auto k = static_cast<std::uint32_t>(kind);
  if (kind == ObjectKind::kNone || k >= kKindCount) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "cannot allocate an object ID of kind none");
  }
  const std::uint32_t index = next_[k];
  if (index > ObjectId::kMaxIndex) {
    return Status::Error(ErrorCode::kOutOfRange,
                         "object ID index space exhausted for kind");
  }
  next_[k] = index + 1;
  return ObjectId::Make(kind, index);
}

std::uint32_t IdAllocator::next_index(ObjectKind kind) const {
  const auto k = static_cast<std::uint32_t>(kind);
  if (k >= kKindCount) {
    return 0;
  }
  return next_[k];
}

Status IdAllocator::RestoreNext(ObjectKind kind, std::uint32_t next_index) {
  const auto k = static_cast<std::uint32_t>(kind);
  if (kind == ObjectKind::kNone || k >= kKindCount) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "cannot restore allocator state for kind none");
  }
  if (next_index < next_[k]) {
    return Status::Error(
        ErrorCode::kInvalidArgument,
        "restoring an allocator index backwards would reuse object IDs");
  }
  if (next_index > ObjectId::kMaxIndex + 1) {
    return Status::Error(ErrorCode::kOutOfRange,
                         "restored allocator index is out of range");
  }
  next_[k] = next_index;
  return Status::Ok();
}

}  // namespace eos::foundation
