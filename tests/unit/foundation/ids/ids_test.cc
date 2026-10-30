// Unit tests: foundation/ids.
#include "eos/foundation/ids.h"

#include <gtest/gtest.h>

namespace eos::foundation {
namespace {

TEST(ObjectId, NullIsKindNoneIndexZero) {
  const ObjectId id;
  EXPECT_TRUE(id.is_null());
  EXPECT_EQ(id.kind(), ObjectKind::kNone);
  EXPECT_EQ(id.index(), 0u);
  EXPECT_EQ(id.raw(), 0u);
}

TEST(ObjectId, PackingRoundTrips) {
  const ObjectId id = ObjectId::Make(ObjectKind::kNet, 12345);
  EXPECT_EQ(id.kind(), ObjectKind::kNet);
  EXPECT_EQ(id.index(), 12345u);
  EXPECT_FALSE(id.is_null());
  EXPECT_EQ(ObjectId(id.raw()), id);
}

TEST(ObjectId, MaxIndexRoundTrips) {
  const ObjectId id =
      ObjectId::Make(ObjectKind::kInstance, ObjectId::kMaxIndex);
  EXPECT_EQ(id.kind(), ObjectKind::kInstance);
  EXPECT_EQ(id.index(), ObjectId::kMaxIndex);
}

TEST(IdAllocator, HandsOutMonotonicPerKindIndices) {
  IdAllocator alloc;
  const Result<ObjectId> a = alloc.Allocate(ObjectKind::kInstance);
  const Result<ObjectId> b = alloc.Allocate(ObjectKind::kInstance);
  const Result<ObjectId> n = alloc.Allocate(ObjectKind::kNet);
  ASSERT_TRUE(a.ok());
  ASSERT_TRUE(b.ok());
  ASSERT_TRUE(n.ok());
  EXPECT_EQ(a.value().index(), 1u);
  EXPECT_EQ(b.value().index(), 2u);
  EXPECT_EQ(n.value().index(), 1u);
  EXPECT_EQ(n.value().kind(), ObjectKind::kNet);
  EXPECT_NE(a.value(), b.value());
}

TEST(IdAllocator, RejectsKindNone) {
  IdAllocator alloc;
  const Result<ObjectId> r = alloc.Allocate(ObjectKind::kNone);
  ASSERT_FALSE(r.ok());
  EXPECT_EQ(r.status().code(), ErrorCode::kInvalidArgument);
}

TEST(IdAllocator, RestoreNextContinuesFromRestoredIndex) {
  IdAllocator alloc;
  ASSERT_TRUE(alloc.RestoreNext(ObjectKind::kCell, 41).ok());
  const Result<ObjectId> r = alloc.Allocate(ObjectKind::kCell);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value().index(), 41u);
  EXPECT_EQ(alloc.next_index(ObjectKind::kCell), 42u);
}

TEST(IdAllocator, RestoreNextRefusesToGoBackwards) {
  IdAllocator alloc;
  ASSERT_TRUE(alloc.Allocate(ObjectKind::kPin).ok());
  ASSERT_TRUE(alloc.Allocate(ObjectKind::kPin).ok());
  const Status s = alloc.RestoreNext(ObjectKind::kPin, 2);
  ASSERT_FALSE(s.ok());
  EXPECT_EQ(s.code(), ErrorCode::kInvalidArgument);
  // The refused restore changed nothing.
  const Result<ObjectId> r = alloc.Allocate(ObjectKind::kPin);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value().index(), 3u);
}

TEST(IdAllocator, ExhaustionIsAnErrorNotAReuse) {
  IdAllocator alloc;
  ASSERT_TRUE(
      alloc.RestoreNext(ObjectKind::kTerm, ObjectId::kMaxIndex).ok());
  const Result<ObjectId> last = alloc.Allocate(ObjectKind::kTerm);
  ASSERT_TRUE(last.ok());
  EXPECT_EQ(last.value().index(), ObjectId::kMaxIndex);
  const Result<ObjectId> over = alloc.Allocate(ObjectKind::kTerm);
  ASSERT_FALSE(over.ok());
  EXPECT_EQ(over.status().code(), ErrorCode::kOutOfRange);
}

// S6 backfill: ordering is raw-value order, so kind dominates index.
TEST(ObjectId, OrderingIsKindThenIndex) {
  const ObjectId a = ObjectId::Make(ObjectKind::kInstance, 2);
  const ObjectId b = ObjectId::Make(ObjectKind::kInstance, 10);
  const ObjectId c = ObjectId::Make(ObjectKind::kNet, 1);
  EXPECT_TRUE(a < b);
  EXPECT_FALSE(b < a);
  EXPECT_FALSE(a < a);
  EXPECT_TRUE(b < c);
  EXPECT_TRUE(a != b);
}

}  // namespace
}  // namespace eos::foundation
