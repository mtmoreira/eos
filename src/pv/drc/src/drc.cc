// drc: the LEF-driven in-tree layout DRC engine (F13 DRC unit, L4).
//
// Rules come from the technology LEF through OpenDB (dbTechLayer
// tables); geometry comes from the live ODB through RawDatabase(),
// reusing the geometry walk the GDS writer proves (wire paths via
// dbWireDecoder, master pin rectangles via dbMTerm/dbMPin). The
// engine checks the live database, NOT the GDS file (no GDS
// reader exists).
//
// A layer with no rule for a family is not checked for that
// family. A technology with no rule on any layer fails fast
// rather than reporting a vacuous zero. Metal fills are not
// checked in this version. A wire under a non-default rule is
// measured at the rule width when the decoder reports one, else
// at the layer default width.
//
// Pairwise spacing is O(n^2) per layer: fine at mock and
// fixture scale, documented, not a production-scale sweep.
#include "eos/pv/drc.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "odb/db.h"
#include "odb/dbTransform.h"
#include "odb/dbWireCodec.h"

namespace eos::pv {
namespace {

// One metal rect on a routing layer, owned by a net.
struct MetalShape {
  std::string net;
  odb::dbTechLayer* layer = nullptr;
  odb::Rect rect;
  bool is_wire = false;  // from a wire path or patch rect
  bool is_pin = false;   // instance or top pin rect
  bool is_via_metal = false;
};

// One cut rect on a cut layer, owned by a net.
struct CutShape {
  std::string net;
  odb::dbTechLayer* layer = nullptr;
  odb::Rect rect;
};

// One placed via: its cut rects and metal rects, grouped by
// layer, all in absolute coordinates.
struct ViaInstance {
  std::string net;
  std::map<odb::dbTechLayer*, std::vector<odb::Rect>> cuts;
  std::map<odb::dbTechLayer*, std::vector<odb::Rect>> metal;
};

// One wire path run: the effective centerline points of a
// single path on one layer, for the min-step check.
struct PathRun {
  std::string net;
  odb::dbTechLayer* layer = nullptr;
  std::vector<odb::Point> points;
};

struct Geometry {
  std::vector<MetalShape> metal;
  std::vector<CutShape> cuts;
  std::vector<ViaInstance> vias;
  std::vector<PathRun> paths;
  // Wire rects, patch rects, special-wire rects, and via
  // instances. Zero means the design holds no routing at all.
  std::size_t wire_shape_count = 0;
};

// Edge-to-edge distance squared between two rects: 0 when
// they touch or overlap. Compared against spacing squared so
// no floating point is needed.
long long EdgeGapSquared(const odb::Rect& a, const odb::Rect& b) {
  long long dx = std::max<long long>(a.xMin() - b.xMax(),
                                     b.xMin() - a.xMax());
  dx = std::max<long long>(dx, 0);
  long long dy = std::max<long long>(a.yMin() - b.yMax(),
                                     b.yMin() - a.yMax());
  dy = std::max<long long>(dy, 0);
  return dx * dx + dy * dy;
}

bool RectsTouchOrOverlap(const odb::Rect& a, const odb::Rect& b) {
  return a.xMin() <= b.xMax() && b.xMin() <= a.xMax() &&
         a.yMin() <= b.yMax() && b.yMin() <= a.yMax();
}

long long RectArea(const odb::Rect& rect) {
  return static_cast<long long>(rect.dx()) *
         static_cast<long long>(rect.dy());
}

// The rect a wire segment sweeps: the segment's bounding box
// expanded by half the wire width on every side. The width is
// split floor/ceil, so the rect is exactly `width` across the
// segment (an even split would lose 1 DBU on an odd width and
// turn a minimum-width wire into a false violation).
odb::Rect SegmentRect(const odb::Point& from, const odb::Point& to,
                      int width) {
  int lo = width / 2;
  int hi = width - lo;
  return odb::Rect(std::min(from.x(), to.x()) - lo,
                   std::min(from.y(), to.y()) - lo,
                   std::max(from.x(), to.x()) + hi,
                   std::max(from.y(), to.y()) + hi);
}

// The default wire width for a layer, in DBU.
int LayerWidth(odb::dbTechLayer* layer) {
  if (layer == nullptr) {
    return 0;
  }
  return static_cast<int>(layer->getWidth());
}

// The rect for one via box, translated from the via's local
// origin to the via's placed point.
odb::Rect ViaBoxRect(const odb::Rect& box, const odb::Point& at) {
  return odb::Rect(box.xMin() + at.x(), box.yMin() + at.y(),
                   box.xMax() + at.x(), box.yMax() + at.y());
}

void AddViaBoxes(odb::dbSet<odb::dbBox> boxes, const odb::Point& at,
                 const std::string& net, Geometry* geom) {
  ViaInstance via;
  via.net = net;
  for (odb::dbBox* box : boxes) {
    odb::dbTechLayer* layer = box->getTechLayer();
    if (layer == nullptr) {
      continue;
    }
    odb::Rect rect = ViaBoxRect(box->getBox(), at);
    if (layer->getType() == odb::dbTechLayerType::CUT) {
      via.cuts[layer].push_back(rect);
      geom->cuts.push_back(CutShape{net, layer, rect});
    } else if (layer->getType() == odb::dbTechLayerType::ROUTING) {
      via.metal[layer].push_back(rect);
      geom->metal.push_back(
          MetalShape{net, layer, rect, false, false, true});
    }
  }
  if (!via.cuts.empty() || !via.metal.empty()) {
    geom->vias.push_back(via);
    ++geom->wire_shape_count;
  }
}

// Decodes one dbWire (a net wire or a special wire) into the
// collected geometry. A path run is flushed whenever a new
// path, junction, short, virtual wire, via, tech via, or patch
// rect starts; the run's segments become wire rects and the
// run's centerline is kept for the min-step check. Junction
// points seed the new run, so a branch's first segment runs
// from the junction point.
void DecodeWire(odb::dbWire* wire, const std::string& net,
                Geometry* geom) {
  if (wire == nullptr) {
    return;
  }
  odb::dbWireDecoder decoder;
  decoder.begin(wire);
  odb::dbTechLayer* layer = nullptr;
  int width = 0;
  std::vector<odb::Point> points;
  std::map<int, odb::Point> junctions;
  odb::Point last(0, 0);
  bool have_last = false;

  auto flush_path = [&]() {
    if (points.size() >= 2 && layer != nullptr && width > 0) {
      for (std::size_t i = 1; i < points.size(); ++i) {
        geom->metal.push_back(MetalShape{
            net, layer, SegmentRect(points[i - 1], points[i], width),
            true, false, false});
        ++geom->wire_shape_count;
      }
      geom->paths.push_back(PathRun{net, layer, points});
    }
    points.clear();
  };

  while (true) {
    odb::dbWireDecoder::OpCode op = decoder.next();
    if (op == odb::dbWireDecoder::END_DECODE) {
      break;
    }
    switch (op) {
      case odb::dbWireDecoder::PATH:
      case odb::dbWireDecoder::JUNCTION:
      case odb::dbWireDecoder::SHORT:
      case odb::dbWireDecoder::VWIRE: {
        flush_path();
        layer = decoder.getLayer();
        width = LayerWidth(layer);
        if (op != odb::dbWireDecoder::PATH) {
          std::map<int, odb::Point>::const_iterator it =
              junctions.find(decoder.getJunctionValue());
          if (it != junctions.end()) {
            points.push_back(it->second);
          }
        }
        break;
      }
      case odb::dbWireDecoder::POINT: {
        int x = 0;
        int y = 0;
        decoder.getPoint(x, y);
        odb::Point point(x, y);
        junctions[decoder.getJunctionId()] = point;
        points.push_back(point);
        last = point;
        have_last = true;
        break;
      }
      case odb::dbWireDecoder::POINT_EXT: {
        int x = 0;
        int y = 0;
        int ext = 0;
        decoder.getPoint(x, y, ext);
        odb::Point point(x, y);
        junctions[decoder.getJunctionId()] = point;
        // The extension runs past the point in the direction
        // of travel from the previous point of this run.
        if (!points.empty() && ext != 0) {
          odb::Point prev = points.back();
          int dx = (point.x() > prev.x()) - (point.x() < prev.x());
          int dy = (point.y() > prev.y()) - (point.y() < prev.y());
          point = odb::Point(point.x() + dx * ext,
                             point.y() + dy * ext);
        }
        points.push_back(point);
        last = point;
        have_last = true;
        break;
      }
      case odb::dbWireDecoder::VIA: {
        flush_path();
        if (have_last) {
          odb::dbVia* via = decoder.getVia();
          if (via != nullptr) {
            AddViaBoxes(via->getBoxes(), last, net, geom);
          }
        }
        break;
      }
      case odb::dbWireDecoder::TECH_VIA: {
        flush_path();
        if (have_last) {
          odb::dbTechVia* via = decoder.getTechVia();
          if (via != nullptr) {
            AddViaBoxes(via->getBoxes(), last, net, geom);
          }
        }
        break;
      }
      case odb::dbWireDecoder::RECT: {
        flush_path();
        if (have_last) {
          int dx1 = 0;
          int dy1 = 0;
          int dx2 = 0;
          int dy2 = 0;
          decoder.getRect(dx1, dy1, dx2, dy2);
          odb::dbTechLayer* rect_layer = decoder.getLayer();
          if (rect_layer != nullptr) {
            geom->metal.push_back(MetalShape{
                net, rect_layer,
                odb::Rect(last.x() + dx1, last.y() + dy1,
                          last.x() + dx2, last.y() + dy2),
                true, false, false});
            ++geom->wire_shape_count;
          }
        }
        break;
      }
      case odb::dbWireDecoder::RULE: {
        odb::dbTechLayerRule* rule = decoder.getRule();
        if (rule != nullptr && rule->getWidth() > 0) {
          width = rule->getWidth();
        }
        break;
      }
      case odb::dbWireDecoder::ITERM:
      case odb::dbWireDecoder::BTERM:
      case odb::dbWireDecoder::END_DECODE:
        break;
    }
  }
  flush_path();
}

void CollectGeometry(odb::dbBlock* block, Geometry* geom) {
  for (odb::dbNet* net : block->getNets()) {
    std::string net_name = net->getName();
    DecodeWire(net->getWire(), net_name, geom);
    for (odb::dbSWire* swire : net->getSWires()) {
      for (odb::dbSBox* sbox : swire->getWires()) {
        odb::dbTechLayer* layer = sbox->getTechLayer();
        if (layer == nullptr) {
          continue;
        }
        geom->metal.push_back(
            MetalShape{net_name, layer, sbox->getBox(), true, false, false});
      }
    }
  }
  for (odb::dbInst* inst : block->getInsts()) {
    odb::dbTransform transform;
    inst->getTransform(transform);
    for (odb::dbITerm* iterm : inst->getITerms()) {
      odb::dbNet* net = iterm->getNet();
      std::string net_name = net == nullptr ? "" : net->getName();
      odb::dbMTerm* mterm = iterm->getMTerm();
      if (mterm == nullptr) {
        continue;
      }
      for (odb::dbMPin* mpin : mterm->getMPins()) {
        for (odb::dbBox* box : mpin->getGeometry()) {
          odb::dbTechLayer* layer = box->getTechLayer();
          if (layer == nullptr) {
            continue;
          }
          odb::Rect rect = box->getBox();
          transform.apply(rect);
          geom->metal.push_back(
              MetalShape{net_name, layer, rect, false, true, false});
        }
      }
    }
  }
  for (odb::dbBTerm* bterm : block->getBTerms()) {
    odb::dbNet* net = bterm->getNet();
    std::string net_name = net == nullptr ? "" : net->getName();
    for (odb::dbBPin* bpin : bterm->getBPins()) {
      for (odb::dbBox* box : bpin->getBoxes()) {
        odb::dbTechLayer* layer = box->getTechLayer();
        if (layer == nullptr) {
          continue;
        }
        geom->metal.push_back(MetalShape{net_name, layer, box->getBox(),
                                         false, true, false});
      }
    }
  }
}

// The rules for one layer, read from the tech LEF through
// OpenDB. A value of 0 means the layer carries no rule for
// that family and is not checked for it. Min-area prefers
// the plain AREA form (layer->getArea()) and falls back
// to the first LEF58 area rule; min-step prefers the plain
// MINSTEP form and falls back to the first min-step rule.
struct LayerRules {
  odb::dbTechLayer* layer = nullptr;
  int min_width = 0;
  int spacing = 0;
  long long min_area = 0;
  int min_step = 0;
  int cut_spacing = 0;
  int enclosure_below = 0;
  int enclosure_above = 0;
  bool has_enclosure = false;
};

LayerRules ReadLayerRules(odb::dbTechLayer* layer) {
  LayerRules rules;
  rules.layer = layer;
  if (layer->getType() == odb::dbTechLayerType::ROUTING) {
    rules.min_width = static_cast<int>(layer->getMinWidth());
    rules.spacing = layer->getSpacing();
    if (layer->hasArea()) {
      // getArea() is denominated in square microns; convert to
      // square database units with the tech's DBU-per-micron.
      const int dbu_per_micron = layer->getTech()->getDbUnitsPerMicron();
      const double scale =
          static_cast<double>(dbu_per_micron) * dbu_per_micron;
      rules.min_area = static_cast<long long>(
          std::llround(layer->getArea() * scale));
    } else {
      for (odb::dbTechLayerAreaRule* area_rule :
           layer->getTechLayerAreaRules()) {
        rules.min_area = area_rule->getArea();
        break;
      }
    }
    if (layer->hasMinStep()) {
      rules.min_step = static_cast<int>(layer->getMinStep());
    } else {
      for (odb::dbTechLayerMinStepRule* step_rule :
           layer->getTechLayerMinStepRules()) {
        rules.min_step = step_rule->getMinStepLength();
        break;
      }
    }
  } else if (layer->getType() == odb::dbTechLayerType::CUT) {
    rules.cut_spacing = layer->getSpacing();
    if (rules.cut_spacing == 0) {
      for (odb::dbTechLayerCutSpacingRule* spacing_rule :
           layer->getTechLayerCutSpacingRules()) {
        rules.cut_spacing = spacing_rule->getCutSpacing();
        break;
      }
    }
    for (odb::dbTechLayerCutEnclosureRule* enc_rule :
         layer->getTechLayerCutEnclosureRules()) {
      if (enc_rule->getCutClass() == nullptr) {
        rules.enclosure_below = enc_rule->getFirstOverhang();
        rules.enclosure_above = enc_rule->getSecondOverhang();
        rules.has_enclosure = true;
        break;
      }
    }
    if (!rules.has_enclosure) {
      for (odb::dbTechLayerCutEnclosureRule* enc_rule :
           layer->getTechLayerCutEnclosureRules()) {
        rules.enclosure_below = enc_rule->getFirstOverhang();
        rules.enclosure_above = enc_rule->getSecondOverhang();
        rules.has_enclosure = true;
        break;
      }
    }
  }
  return rules;
}

bool ViolationLess(const DrcViolation& a, const DrcViolation& b) {
  if (a.net != b.net) {
    return a.net < b.net;
  }
  if (a.x1 != b.x1) {
    return a.x1 < b.x1;
  }
  if (a.y1 != b.y1) {
    return a.y1 < b.y1;
  }
  if (a.x2 != b.x2) {
    return a.x2 < b.x2;
  }
  return a.y2 < b.y2;
}

void SortViolations(DrcCheckResult* result) {
  std::sort(result->violations.begin(), result->violations.end(),
            ViolationLess);
}

// Width: every wire, pin, and via-metal rect on the layer
// must have min dimension >= MINWIDTH.
void CheckWidth(const LayerRules& rules,
                const std::vector<MetalShape>& shapes,
                DrcCheckResult* result) {
  result->check = rules.layer->getName() + ".width";
  result->layer = rules.layer->getName();
  result->rule_value = rules.min_width;
  for (const MetalShape& shape : shapes) {
    if (shape.layer != rules.layer) {
      continue;
    }
    int width = std::min(shape.rect.dx(), shape.rect.dy());
    if (width < rules.min_width) {
      result->violations.push_back(DrcViolation{
          result->check, result->layer, shape.net,
          "width=" + std::to_string(width), shape.rect.xMin(),
          shape.rect.yMin(), shape.rect.xMax(), shape.rect.yMax()});
    }
  }
  SortViolations(result);
}

// Spacing: same-layer rect pairs from different nets must
// sit at least SPACING apart edge to edge. Overlap counts
// as gap 0, so a short between two nets is flagged here.
void CheckSpacing(const LayerRules& rules,
                  const std::vector<MetalShape>& shapes,
                  DrcCheckResult* result) {
  result->check = rules.layer->getName() + ".spacing";
  result->layer = rules.layer->getName();
  result->rule_value = rules.spacing;
  long long spacing2 =
      static_cast<long long>(rules.spacing) * rules.spacing;
  std::vector<const MetalShape*> on_layer;
  for (const MetalShape& shape : shapes) {
    if (shape.layer == rules.layer) {
      on_layer.push_back(&shape);
    }
  }
  for (std::size_t i = 0; i < on_layer.size(); ++i) {
    for (std::size_t j = i + 1; j < on_layer.size(); ++j) {
      if (on_layer[i]->net == on_layer[j]->net) {
        continue;
      }
      long long gap2 =
          EdgeGapSquared(on_layer[i]->rect, on_layer[j]->rect);
      if (gap2 < spacing2) {
        long long gap =
            static_cast<long long>(std::llround(std::sqrt(
                static_cast<double>(gap2))));
        odb::Rect bbox = on_layer[i]->rect;
        bbox.merge(on_layer[j]->rect);
        result->violations.push_back(DrcViolation{
            result->check, result->layer, on_layer[i]->net,
            "gap=" + std::to_string(gap), bbox.xMin(), bbox.yMin(),
            bbox.xMax(), bbox.yMax()});
      }
    }
  }
  SortViolations(result);
}

// Min-area: merged shape per net per layer (union of
// touching/overlapping rects) must have area >= AREA.
// The area is the sum of the rect areas, which double-counts
// the overlap where two rects join; that over-counts area,
// so it can only miss a violation, never invent one.
void CheckMinArea(const LayerRules& rules,
                  const std::vector<MetalShape>& shapes,
                  DrcCheckResult* result) {
  result->check = rules.layer->getName() + ".min-area";
  result->layer = rules.layer->getName();
  result->rule_value = rules.min_area;
  std::map<std::string, std::vector<const MetalShape*>> by_net;
  for (const MetalShape& shape : shapes) {
    if (shape.layer == rules.layer) {
      by_net[shape.net].push_back(&shape);
    }
  }
  for (const auto& entry : by_net) {
    const std::vector<const MetalShape*>& rects = entry.second;
    std::vector<int> parent(rects.size());
    for (std::size_t i = 0; i < rects.size(); ++i) {
      parent[i] = static_cast<int>(i);
    }
    std::function<int(int)> find = [&](int x) {
      while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
      }
      return x;
    };
    for (std::size_t i = 0; i < rects.size(); ++i) {
      for (std::size_t j = i + 1; j < rects.size(); ++j) {
        if (RectsTouchOrOverlap(rects[i]->rect, rects[j]->rect)) {
          int ri = find(static_cast<int>(i));
          int rj = find(static_cast<int>(j));
          if (ri != rj) {
            parent[ri] = rj;
          }
        }
      }
    }
    std::map<int, std::vector<const MetalShape*>> groups;
    for (std::size_t i = 0; i < rects.size(); ++i) {
      groups[find(static_cast<int>(i))].push_back(rects[i]);
    }
    for (const auto& group : groups) {
      long long area = 0;
      odb::Rect bbox;
      bbox.mergeInit();
      for (const MetalShape* shape : group.second) {
        area += RectArea(shape->rect);
        bbox.merge(shape->rect);
      }
      if (area < rules.min_area) {
        result->violations.push_back(DrcViolation{
            result->check, result->layer, entry.first,
            "area=" + std::to_string(area), bbox.xMin(), bbox.yMin(),
            bbox.xMax(), bbox.yMax()});
      }
    }
  }
  SortViolations(result);
}

// Min-step: a wire-path jog, a centerline segment
// perpendicular to both its neighbors with length <
// MINSTEP, is one violation. Path jogs only; notches in
// merged shapes are not covered in this version.
void CheckMinStep(const LayerRules& rules,
                  const std::vector<PathRun>& paths,
                  DrcCheckResult* result) {
  result->check = rules.layer->getName() + ".min-step";
  result->layer = rules.layer->getName();
  result->rule_value = rules.min_step;
  for (const PathRun& run : paths) {
    if (run.layer != rules.layer || run.points.size() < 4) {
      continue;
    }
    for (std::size_t i = 1; i + 2 < run.points.size(); ++i) {
      odb::Point p0 = run.points[i - 1];
      odb::Point p1 = run.points[i];
      odb::Point p2 = run.points[i + 1];
      odb::Point p3 = run.points[i + 2];
      int dx_prev = p1.x() - p0.x();
      int dy_prev = p1.y() - p0.y();
      int dx_jog = p2.x() - p1.x();
      int dy_jog = p2.y() - p1.y();
      int dx_next = p3.x() - p2.x();
      int dy_next = p3.y() - p2.y();
      if (dx_jog == 0 && dy_jog == 0) {
        continue;
      }
      if ((dx_prev == 0 && dy_prev == 0) ||
          (dx_next == 0 && dy_next == 0)) {
        continue;
      }
      bool jog_horizontal = dy_jog == 0;
      bool jog_vertical = dx_jog == 0;
      if (!jog_horizontal && !jog_vertical) {
        continue;
      }
      bool prev_perpendicular =
          jog_horizontal ? dx_prev == 0 : dy_prev == 0;
      bool next_perpendicular =
          jog_horizontal ? dx_next == 0 : dy_next == 0;
      if (!prev_perpendicular || !next_perpendicular) {
        continue;
      }
      int length = jog_horizontal ? std::abs(dx_jog)
                                  : std::abs(dy_jog);
      if (length < rules.min_step) {
        odb::Rect bbox = SegmentRect(p1, p2, LayerWidth(run.layer));
        result->violations.push_back(DrcViolation{
            result->check, result->layer, run.net,
            "step=" + std::to_string(length), bbox.xMin(),
            bbox.yMin(), bbox.xMax(), bbox.yMax()});
      }
    }
  }
  SortViolations(result);
}

// Enclosure: every cut rect must be enclosed by the metal
// rects on the routing layers directly below and above its
// cut layer by at least the LEF ENCLOSURE overhangs (first
// = below, second = above). One violation per (cut rect,
// metal layer). A missing metal layer is a violation with
// the expected layer named.
void CheckEnclosure(const LayerRules& rules, odb::dbTech* tech,
                    const std::vector<ViaInstance>& vias,
                    DrcCheckResult* result) {
  result->check = rules.layer->getName() + ".enclosure";
  result->layer = rules.layer->getName();
  result->rule_value = rules.enclosure_below;
  odb::dbTechLayer* below =
      tech->findLayer(rules.layer->getNumber() - 1);
  odb::dbTechLayer* above =
      tech->findLayer(rules.layer->getNumber() + 1);
  for (const ViaInstance& via : vias) {
    std::map<odb::dbTechLayer*, std::vector<odb::Rect>>::const_iterator
        cuts = via.cuts.find(rules.layer);
    if (cuts == via.cuts.end()) {
      continue;
    }
    for (const odb::Rect& cut : cuts->second) {
      std::pair<odb::dbTechLayer*, int> sides[2] = {
          {below, rules.enclosure_below},
          {above, rules.enclosure_above}};
      for (const auto& side : sides) {
        odb::dbTechLayer* metal_layer = side.first;
        int required = side.second;
        if (metal_layer == nullptr) {
          continue;
        }
        int best = std::numeric_limits<int>::min();
        std::map<odb::dbTechLayer*,
                 std::vector<odb::Rect>>::const_iterator metals =
            via.metal.find(metal_layer);
        if (metals != via.metal.end()) {
          for (const odb::Rect& metal : metals->second) {
            int overhang = std::min(
                std::min(cut.xMin() - metal.xMin(),
                         cut.yMin() - metal.yMin()),
                std::min(metal.xMax() - cut.xMax(),
                         metal.yMax() - cut.yMax()));
            best = std::max(best, overhang);
          }
        }
        if (best < required) {
          result->violations.push_back(DrcViolation{
              result->check, result->layer, via.net,
              "metal=" + metal_layer->getName(), cut.xMin(),
              cut.yMin(), cut.xMax(), cut.yMax()});
        }
      }
    }
  }
  SortViolations(result);
}

// Cut spacing: cut-rect pairs from different nets on the
// same cut layer must sit at least the cut layer's SPACING
// apart edge to edge.
void CheckCutSpacing(const LayerRules& rules,
                     const std::vector<CutShape>& cuts,
                     DrcCheckResult* result) {
  result->check = rules.layer->getName() + ".cut-spacing";
  result->layer = rules.layer->getName();
  result->rule_value = rules.cut_spacing;
  long long spacing2 =
      static_cast<long long>(rules.cut_spacing) * rules.cut_spacing;
  std::vector<const CutShape*> on_layer;
  for (const CutShape& cut : cuts) {
    if (cut.layer == rules.layer) {
      on_layer.push_back(&cut);
    }
  }
  for (std::size_t i = 0; i < on_layer.size(); ++i) {
    for (std::size_t j = i + 1; j < on_layer.size(); ++j) {
      if (on_layer[i]->net == on_layer[j]->net) {
        continue;
      }
      long long gap2 =
          EdgeGapSquared(on_layer[i]->rect, on_layer[j]->rect);
      if (gap2 < spacing2) {
        long long gap =
            static_cast<long long>(std::llround(std::sqrt(
                static_cast<double>(gap2))));
        odb::Rect bbox = on_layer[i]->rect;
        bbox.merge(on_layer[j]->rect);
        result->violations.push_back(DrcViolation{
            result->check, result->layer, on_layer[i]->net,
            "gap=" + std::to_string(gap), bbox.xMin(), bbox.yMin(),
            bbox.xMax(), bbox.yMax()});
      }
    }
  }
  SortViolations(result);
}

}  // namespace

bool RunDrc(odb::dbDatabase* db, DrcRecord* record,
            std::string* error) {
  record->checks.clear();
  record->source = "live-database";
  if (db == nullptr) {
    *error = "no database: no ODB database is loaded";
    return false;
  }
  odb::dbTech* tech = db->getTech();
  if (tech == nullptr) {
    *error =
        "no technology data loaded: run eos::import_odb first";
    return false;
  }
  odb::dbChip* chip = db->getChip();
  if (chip == nullptr) {
    *error = "no design loaded: no chip is present in the database";
    return false;
  }
  odb::dbBlock* block = chip->getBlock();
  if (block == nullptr) {
    *error = "no top block: no block is present in the design";
    return false;
  }
  Geometry geom;
  CollectGeometry(block, &geom);
  if (geom.wire_shape_count == 0) {
    *error = "no routed design: the top block holds no wires";
    return false;
  }
  for (odb::dbTechLayer* layer : tech->getLayers()) {
    LayerRules rules = ReadLayerRules(layer);
    if (layer->getType() == odb::dbTechLayerType::ROUTING) {
      if (rules.min_width > 0) {
        DrcCheckResult result;
        CheckWidth(rules, geom.metal, &result);
        record->checks.push_back(result);
      }
      if (rules.spacing > 0) {
        DrcCheckResult result;
        CheckSpacing(rules, geom.metal, &result);
        record->checks.push_back(result);
      }
      if (rules.min_area > 0) {
        DrcCheckResult result;
        CheckMinArea(rules, geom.metal, &result);
        record->checks.push_back(result);
      }
      if (rules.min_step > 0) {
        DrcCheckResult result;
        CheckMinStep(rules, geom.paths, &result);
        record->checks.push_back(result);
      }
    } else if (layer->getType() == odb::dbTechLayerType::CUT) {
      if (rules.has_enclosure) {
        DrcCheckResult result;
        CheckEnclosure(rules, tech, geom.vias, &result);
        record->checks.push_back(result);
      }
      if (rules.cut_spacing > 0) {
        DrcCheckResult result;
        CheckCutSpacing(rules, geom.cuts, &result);
        record->checks.push_back(result);
      }
    }
  }
  if (record->checks.empty()) {
    *error =
        "no DRC rules found in the technology data: the tech "
        "LEF carries no width, spacing, area, step, enclosure, "
        "or cut-spacing rule on any layer";
    return false;
  }
  return true;
}

}  // namespace eos::pv
