// ids: persistent object IDs (L0 foundation).
//
// A persistent object ID is a single 32-bit value: the top 4 bits carry
// the object kind, the low 28 bits carry a per-kind index. Indices are
// handed out by IdAllocator, are never reused within a session, and are
// stable across checkpoint and restore (the allocator state is part of
// what a checkpoint saves; see the L2 checkpoint module).
//
// This header is a stable surface: the packing layout is a written
// format, because IDs are stored inside checkpoints and the state
// ledger. Changing the layout is a format change, not an internal one.
#ifndef EOS_FOUNDATION_IDS_H_
#define EOS_FOUNDATION_IDS_H_

#include <cstdint>

#include "eos/foundation/errors.h"

namespace eos::foundation {

// The design-object kinds a persistent ID can name. Values are part of
// the written format; do not renumber.
enum class ObjectKind : std::uint32_t {
  kNone = 0,
  kInstance = 1,
  kNet = 2,
  kPin = 3,
  kPort = 4,
  kTerm = 5,
  kCell = 6,
  kDesign = 7,
};

class ObjectId {
 public:
  static constexpr std::uint32_t kKindShift = 28;
  static constexpr std::uint32_t kIndexMask = (1u << kKindShift) - 1u;
  static constexpr std::uint32_t kMaxIndex = kIndexMask;

  constexpr ObjectId() : value_(0) {}
  constexpr explicit ObjectId(std::uint32_t raw) : value_(raw) {}

  static constexpr ObjectId Make(ObjectKind kind, std::uint32_t index) {
    return ObjectId((static_cast<std::uint32_t>(kind) << kKindShift) |
                    (index & kIndexMask));
  }

  constexpr std::uint32_t raw() const { return value_; }
  constexpr ObjectKind kind() const {
    return static_cast<ObjectKind>(value_ >> kKindShift);
  }
  constexpr std::uint32_t index() const { return value_ & kIndexMask; }
  constexpr bool is_null() const { return value_ == 0; }

  friend constexpr bool operator==(ObjectId a, ObjectId b) {
    return a.value_ == b.value_;
  }
  friend constexpr bool operator!=(ObjectId a, ObjectId b) {
    return a.value_ != b.value_;
  }
  friend constexpr bool operator<(ObjectId a, ObjectId b) {
    return a.value_ < b.value_;
  }

 private:
  std::uint32_t value_;
};

// Hands out per-kind indices, monotonically, starting at 1 (index 0 is
// reserved so that a zero ObjectId of any kind reads as null for kNone
// and as "unset" elsewhere). Not thread-safe; allocation is a session
// level operation, not a hot loop.
class IdAllocator {
 public:
  IdAllocator() = default;

  // Allocates the next ID of the given kind. Fails with kOutOfRange
  // when the kind's 28-bit index space is exhausted, and with
  // kInvalidArgument for ObjectKind::kNone.
  Result<ObjectId> Allocate(ObjectKind kind);

  // The index the next Allocate call for this kind will hand out.
  std::uint32_t next_index(ObjectKind kind) const;

  // Restores allocator state, as checkpoint restore must: after this
  // call, Allocate continues from next_index for the kind. Fails with
  // kInvalidArgument when asked to move a kind backwards (that would
  // reuse an index) or for ObjectKind::kNone.
  Status RestoreNext(ObjectKind kind, std::uint32_t next_index);

 private:
  static constexpr int kKindCount = 8;
  std::uint32_t next_[kKindCount] = {1, 1, 1, 1, 1, 1, 1, 1};
};

}  // namespace eos::foundation

#endif  // EOS_FOUNDATION_IDS_H_
