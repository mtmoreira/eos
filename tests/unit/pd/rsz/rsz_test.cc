// Unit test for the rsz engine module: factory smoke test only.
// A Resizer needs a full db/STA session for repair, so the C++
// unit test covers what is testable without one: the ord factory
// constructs and destroys a Resizer, and a fresh instance reports
// no estimated parasitics. Real repair coverage is the frozen
// degraded gcd repair run (both-consumer gate) plus the adapted
// Tcl regression suite under the module tests/ dir.
#include <gtest/gtest.h>

#include "rsz/MakeResizer.hh"
#include "rsz/Resizer.hh"

TEST(Rsz, FactoryConstructsAndDestroys) {
  rsz::Resizer *resizer = ord::makeResizer();
  ASSERT_NE(resizer, nullptr);
  EXPECT_FALSE(resizer->haveEstimatedParasitics());
  ord::deleteResizer(resizer);
}

TEST(Rsz, DirectConstructs) {
  rsz::Resizer resizer;
  EXPECT_FALSE(resizer.haveEstimatedParasitics());
}
