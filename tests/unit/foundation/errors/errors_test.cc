// Unit tests: foundation/errors.
#include "eos/foundation/errors.h"

#include <gtest/gtest.h>

namespace eos::foundation {
namespace {

TEST(Status, DefaultIsOk) {
  const Status s;
  EXPECT_TRUE(s.ok());
  EXPECT_EQ(s.code(), ErrorCode::kOk);
}

TEST(Status, ErrorCarriesCodeAndMessage) {
  const Status s = Status::Error(ErrorCode::kNotFound, "no such object");
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), ErrorCode::kNotFound);
  EXPECT_EQ(s.message(), "no such object");
}

TEST(Result, HoldsValueOnSuccess) {
  const Result<int> r(42);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value(), 42);
}

TEST(Result, HoldsStatusOnFailure) {
  const Result<int> r(Status::Error(ErrorCode::kStaleRevision, "old"));
  ASSERT_FALSE(r.ok());
  EXPECT_EQ(r.status().code(), ErrorCode::kStaleRevision);
  EXPECT_EQ(r.status().message(), "old");
}

}  // namespace
}  // namespace eos::foundation
