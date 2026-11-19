// gds: the GUI's GDS export model (L6 interface, Qt-free).
//
// The model the GDS canvas reads: one recorded export from
// a gds_summary payload, with the file's structure and
// layer censuses, the flattened top-structure geometry the
// canvas draws, and the top's SREFs as written. Parsing is
// all-or-nothing like the other models: a malformed
// payload leaves the previous model untouched and reports
// false. The model validates the grammar's invariants:
// the header comes first, structure names are unique,
// layer (gds, datatype) pairs are unique and ascending,
// geometry lines name declared structures and layers, and
// the recounted structure total and the top structure's
// SREF count equal the header's recorded counts, so a file
// replaced outside the session never renders as the
// recorded export. The geometry bounding box is computed
// here because a GDS file carries no die rectangle (the
// one deliberate difference from the route model, whose
// die and core come from the database).
#ifndef EOS_UI_GDS_H_
#define EOS_UI_GDS_H_

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace eos::ui {

// One structure of the exported file, in file order: its
// element counts as written (not flattened), and whether
// no SREF in the file names it (the top test).
struct GdsStructureInfo {
  std::string name;
  bool top = false;
  std::size_t boundaries = 0;
  std::size_t paths = 0;
  std::size_t srefs = 0;
};

// One (gds layer number, datatype) pair the file's
// elements use, in ascending number order, with its
// boundary and path counts over the whole file. The name
// is the session technology's name for the layer, or
// empty when the query could not name it (the payload's
// "-" token).
struct GdsLayerInfo {
  int gds_layer = 0;
  int datatype = 0;
  std::string name;
  std::size_t boundaries = 0;
  std::size_t paths = 0;
};

// One flattened boundary: the polygon points in file
// coordinates, with the layer it sits on and the name of
// the structure it was defined in (the top itself, or the
// master an SREF pulled it from).
struct GdsBoundaryShape {
  std::string structure;
  std::string layer_name;
  int gds_layer = 0;
  int datatype = 0;
  std::vector<std::array<int, 2>> points;
};

// One flattened path: the centerline points in file
// coordinates, its layer, the structure it was defined
// in, and its width (absent for a path the file carries
// no width for).
struct GdsPathShape {
  std::string structure;
  std::string layer_name;
  int gds_layer = 0;
  int datatype = 0;
  bool has_width = false;
  int width = 0;
  std::vector<std::array<int, 2>> points;
};

// One SREF of the top structure, as written: its target,
// placement, reflection, and angle in degrees (absent for
// an SREF the file carries no angle for).
struct GdsSrefInfo {
  std::string structure;
  std::string target;
  int x = 0;
  int y = 0;
  bool reflect = false;
  bool has_angle = false;
  double angle_degrees = 0.0;
};

class GdsModel {
 public:
  // Parses a gds_summary payload: a "gds" header line,
  // then "structure" lines, then "layer" lines, then the
  // flattened "boundary" and "path" lines and the top's
  // "sref" lines in payload order. Returns false (model
  // unchanged) on any malformed line, unknown key, bad
  // number, or broken invariant (see the file comment).
  // A successful load replaces the previous model.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Loaded() const { return loaded_; }
  bool Empty() const { return !loaded_; }

  const std::string& Path() const { return path_; }
  std::size_t Size() const { return size_; }
  std::size_t Instances() const { return instances_; }
  std::size_t Structures() const { return structures_; }
  const std::string& Library() const { return library_; }
  double DbuPerMicron() const { return dbu_per_micron_; }

  const std::vector<GdsStructureInfo>& StructureInfos() const {
    return structure_infos_;
  }
  const std::vector<GdsLayerInfo>& LayerInfos() const {
    return layer_infos_;
  }
  const std::vector<GdsBoundaryShape>& Boundaries() const {
    return boundaries_;
  }
  const std::vector<GdsPathShape>& Paths() const { return paths_; }
  const std::vector<GdsSrefInfo>& Srefs() const { return srefs_; }

  // The flattened geometry on one layer, by the layer's
  // technology name (empty for an unnamed layer), in
  // payload order: the canvas's per-layer filter.
  std::vector<const GdsBoundaryShape*> BoundariesForLayer(
      const std::string& layer_name) const;
  std::vector<const GdsPathShape*> PathsForLayer(
      const std::string& layer_name) const;

  // The bounding box of the flattened geometry (all
  // boundary and path points), {x1, y1, x2, y2} in file
  // coordinates; meaningful only when HasBoundingBox().
  bool HasBoundingBox() const { return has_bounding_box_; }
  std::array<int, 4> BoundingBox() const { return bounding_box_; }

 private:
  bool loaded_ = false;
  std::string path_;
  std::size_t size_ = 0;
  std::size_t instances_ = 0;
  std::size_t structures_ = 0;
  std::string library_;
  double dbu_per_micron_ = 0.0;
  std::vector<GdsStructureInfo> structure_infos_;
  std::vector<GdsLayerInfo> layer_infos_;
  std::vector<GdsBoundaryShape> boundaries_;
  std::vector<GdsPathShape> paths_;
  std::vector<GdsSrefInfo> srefs_;
  bool has_bounding_box_ = false;
  std::array<int, 4> bounding_box_ = {0, 0, 0, 0};
};

}  // namespace eos::ui

#endif  // EOS_UI_GDS_H_
