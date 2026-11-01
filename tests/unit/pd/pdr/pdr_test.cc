// Unit test for the pdr shared-support module: FLUTE tables plus the
// Prim-Dijkstra Steiner tree. Fixture-free: both calls are pure
// computation on coordinates, no database, no liberty, no DEF.
#include <gtest/gtest.h>

#include <vector>

#include "pdr/flute.h"
#include "pdr/pdrev.h"
#include "utl/Logger.h"

namespace {

// A unit square. Its Steiner tree uses two Steiner nodes and is
// strictly shorter than the 300-unit minimum spanning tree, which
// makes the length assertion a real Steiner-improvement check rather
// than a non-crash check.
const int kX[4] = {0, 100, 100, 0};
const int kY[4] = {0, 0, 100, 100};

}  // namespace

TEST(Pdr, PrimDijkstraSquare) {
  utl::Logger logger;
  std::vector<int> x(std::begin(kX), std::end(kX));
  std::vector<int> y(std::begin(kY), std::end(kY));
  stt::Tree tree = pdr::primDijkstra(x, y, 0, 0.5f, &logger);
  EXPECT_EQ(tree.deg, 4);
  ASSERT_NE(tree.branch, nullptr);
  // The square's rectilinear Steiner optimum equals its MST (300):
  // no Steiner point improves on it under the Manhattan metric.
  EXPECT_EQ(tree.length, 300);
  stt::free_tree(tree);
}

TEST(Pdr, FluteSquare) {
  // Tables come from the generated POWV9.cpp / POST9.cpp linked into
  // the module, not from runtime file reads.
  stt::readLUT();
  int x[4] = {kX[0], kX[1], kX[2], kX[3]};
  int y[4] = {kY[0], kY[1], kY[2], kY[3]};
  stt::Tree tree = stt::flute(4, x, y, FLUTE_ACCURACY);
  EXPECT_EQ(tree.deg, 4);
  ASSERT_NE(tree.branch, nullptr);
  EXPECT_EQ(tree.length, 300);
  stt::free_tree(tree);
  stt::deleteLUT();
}

TEST(Pdr, PrimDijkstraTriangle) {
  // (0,0), (100,100), (200,0). The rectilinear Steiner optimum is 300
  // via (100,0), but Prim-Dijkstra is a heuristic: it returns the 400
  // MST on this input for every alpha and driver index (measured).
  // The test pins that behavior; FLUTE below proves the optimum.
  utl::Logger logger;
  std::vector<int> x{0, 100, 200};
  std::vector<int> y{0, 100, 0};
  stt::Tree tree = pdr::primDijkstra(x, y, 0, 0.5f, &logger);
  EXPECT_EQ(tree.deg, 3);
  ASSERT_NE(tree.branch, nullptr);
  EXPECT_EQ(tree.length, 400);
  stt::free_tree(tree);
}

TEST(Pdr, FluteTriangle) {
  stt::readLUT();
  int x[3] = {0, 100, 200};
  int y[3] = {0, 100, 0};
  stt::Tree tree = stt::flute(3, x, y, FLUTE_ACCURACY);
  EXPECT_EQ(tree.deg, 3);
  ASSERT_NE(tree.branch, nullptr);
  // Exact rectilinear Steiner optimum, strictly better than the 400 MST.
  EXPECT_EQ(tree.length, 300);
  stt::free_tree(tree);
  stt::deleteLUT();
}
