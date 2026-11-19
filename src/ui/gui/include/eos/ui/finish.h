// finish: the GUI's finish model (L6 interface, Qt-free).
//
// The model the Signoff panel's Finish section reads: the
// recorded finish run from a finish_summary payload, which
// is the finish run's own four values under a finish lead
// word. Parsing is all-or-nothing like the other models: a
// malformed payload leaves the previous model untouched and
// reports false. Counts are parsed integers, so the panel
// renders exactly what the query recorded.
#ifndef EOS_UI_FINISH_H_
#define EOS_UI_FINISH_H_

#include <cstddef>

namespace eos::ui {

class FinishModel {
 public:
  // Parses a finish_summary payload: one "finish" header
  // line carrying fillers, grid_shapes, metal_fills, and
  // pdn (true or false), and nothing else. Returns false
  // (model unchanged) on any malformed line, unknown key,
  // or bad number. A successful load replaces the previous
  // model.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Loaded() const { return loaded_; }
  bool Empty() const { return !loaded_; }

  std::size_t Fillers() const { return fillers_; }
  std::size_t GridShapes() const { return grid_shapes_; }
  std::size_t MetalFills() const { return metal_fills_; }
  bool Pdn() const { return pdn_; }

 private:
  bool loaded_ = false;
  std::size_t fillers_ = 0;
  std::size_t grid_shapes_ = 0;
  std::size_t metal_fills_ = 0;
  bool pdn_ = false;
};

}  // namespace eos::ui

#endif  // EOS_UI_FINISH_H_
