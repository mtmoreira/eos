// lvs: the connectivity LVS engine (F13 unit, L4).
//
// The comparison is between two independent data structures
// inside the one live database: the logical topology (dbNet
// ITerms and block BTerms) and geometric connectivity over
// the routed shapes, collected by this module's own geometry
// walk in the DRC walk's shape (dbWireDecoder over the same
// opcode set, via boxes translated by the decoder's last
// point, pin rectangles from ITerm MPin geometry and BTerm
// BPin boxes, special-wire boxes). The walk is duplicated,
// not shared: the DRC walk is internal to drc.cc, the two
// engines are sibling L4 modules, and neither includes the
// other's internals.
//
// Union rule: two shapes on the same layer union when their
// bounding boxes overlap in area or abut on an edge (an
// edge-or-overlap contact); all boxes of one via instance
// union together, because a via is one conductor, which is
// what joins layers. The ITerm-to-net assignment plays no
// part in building the components: a component is pure
// geometry. As built, the rule is narrowed from the design's
// closed-box rule (corner abut counted): the engine suite's
// diagonal-abut case fired under the closed-box rule (two
// nets on adjacent tracks whose segment ends abut diagonally
// read as a short), and design section 11 prescribes the
// narrowing to edge-or-overlap contact in exactly that case.
// Pin-contact amendment (design section 18, Matheus ruling
// 2026-11-22): pin shapes union on overlap only. LEF pin
// rectangles are access geometry, not poured metal, and
// adjacent instances' pin rects abutting edge to edge read
// as shorts under the plain edge-or-overlap rule (two named
// pairs on the clocked demo); an edge abutment counts only
// between wire shapes, and between a pin and a wire shape.
#include "eos/pv/lvs.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "odb/db.h"
#include "odb/dbTransform.h"
#include "odb/dbWireCodec.h"

namespace eos::pv {
namespace {

// One collected shape. Wire, via-metal, and special-wire
// shapes carry is_wire; pin shapes carry is_pin and the pin
// identity ("<inst>:<pin>" or the BTerm name). Every shape
// carries its net's name (empty for a pin with no net, which
// never enters the comparison) and its layer's name.
struct Shape {
  std::string net;
  std::string layer;
  odb::Rect rect;
  bool is_wire = false;
  bool is_pin = false;
  std::string pin;
};

struct Geometry {
  std::vector<Shape> shapes;
  // Wire rects, patch rects, special-wire rects, and via
  // instances. Zero means the design holds no routing at all.
  std::size_t wire_shape_count = 0;
};

// Edge-or-overlap contact: the boxes overlap in area, or
// abut along an edge segment of positive length. A
// corner-only (point) contact does not union: see the
// header note on the diagonal-abut narrowing.
bool RectsTouch(const odb::Rect& a, const odb::Rect& b) {
  const int x_overlap =
      std::min(a.xMax(), b.xMax()) - std::max(a.xMin(), b.xMin());
  const int y_overlap =
      std::min(a.yMax(), b.yMax()) - std::max(a.yMin(), b.yMin());
  return x_overlap >= 0 && y_overlap >= 0 &&
         (x_overlap > 0 || y_overlap > 0);
}

// Area overlap: the boxes share a region of positive area.
// The pin-contact amendment's predicate for pin-pin contact.
bool RectsOverlap(const odb::Rect& a, const odb::Rect& b) {
  const int x_overlap =
      std::min(a.xMax(), b.xMax()) - std::max(a.xMin(), b.xMin());
  const int y_overlap =
      std::min(a.yMax(), b.yMax()) - std::max(a.yMin(), b.yMin());
  return x_overlap > 0 && y_overlap > 0;
}

// Whether two shapes connect under the union rule (design
// section 18): pin-pin contact unions on area overlap only;
// wire-wire and pin-wire contact union on edge-or-overlap.
// The union loop and the short-contact search both use this
// one predicate, so a reported short's contact box is a
// contact the rule itself counts.
bool ShapesConnect(const Shape& a, const Shape& b) {
  if (a.is_pin && b.is_pin) {
    return RectsOverlap(a.rect, b.rect);
  }
  return RectsTouch(a.rect, b.rect);
}

// The intersection of two touching rects (the contact box).
odb::Rect RectIntersection(const odb::Rect& a, const odb::Rect& b) {
  return odb::Rect(std::max(a.xMin(), b.xMin()),
                   std::max(a.yMin(), b.yMin()),
                   std::min(a.xMax(), b.xMax()),
                   std::min(a.yMax(), b.yMax()));
}

// The rect a wire segment sweeps: the segment's bounding box
// expanded by half the wire width on every side, split
// floor/ceil so the rect is exactly `width` across.
odb::Rect SegmentRect(const odb::Point& from, const odb::Point& to,
                      int width) {
  int lo = width / 2;
  int hi = width - lo;
  return odb::Rect(std::min(from.x(), to.x()) - lo,
                   std::min(from.y(), to.y()) - lo,
                   std::max(from.x(), to.x()) + hi,
                   std::max(from.y(), to.y()) + hi);
}

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

// Emits one via instance's metal boxes as wire shapes. The
// caller unions all of the via's shapes together (a via is
// one conductor), so the shapes are appended as one run.
void AddViaBoxes(odb::dbSet<odb::dbBox> boxes, const odb::Point& at,
                 const std::string& net, Geometry* geom) {
  bool any = false;
  for (odb::dbBox* box : boxes) {
    odb::dbTechLayer* layer = box->getTechLayer();
    if (layer == nullptr) {
      continue;
    }
    if (layer->getType() != odb::dbTechLayerType::ROUTING) {
      continue;  // cut boxes carry no plan-view metal contact
    }
    geom->shapes.push_back(Shape{net, layer->getName(),
                                 ViaBoxRect(box->getBox(), at), true,
                                 false, ""});
    any = true;
  }
  if (any) {
    ++geom->wire_shape_count;
  }
}

// Decodes one dbWire (a net wire or a special wire) into the
// collected geometry, in the DRC walk's shape: a path run is
// flushed whenever a new path, junction, short, virtual wire,
// via, tech via, or patch rect starts; junction points seed
// the new run. Via shape runs are recorded so the caller can
// union each via's boxes together.
void DecodeWire(odb::dbWire* wire, const std::string& net,
                Geometry* geom,
                std::vector<std::pair<std::size_t, std::size_t>>*
                    via_runs) {
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
        geom->shapes.push_back(
            Shape{net, layer->getName(),
                  SegmentRect(points[i - 1], points[i], width), true,
                  false, ""});
        ++geom->wire_shape_count;
      }
    }
    points.clear();
  };
  auto add_via = [&](odb::dbSet<odb::dbBox> boxes) {
    flush_path();
    if (!have_last) {
      return;
    }
    const std::size_t begin = geom->shapes.size();
    AddViaBoxes(boxes, last, net, geom);
    if (geom->shapes.size() > begin) {
      via_runs->push_back({begin, geom->shapes.size()});
    }
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
        odb::dbVia* via = decoder.getVia();
        if (via != nullptr) {
          add_via(via->getBoxes());
        } else {
          flush_path();
        }
        break;
      }
      case odb::dbWireDecoder::TECH_VIA: {
        odb::dbTechVia* via = decoder.getTechVia();
        if (via != nullptr) {
          add_via(via->getBoxes());
        } else {
          flush_path();
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
            geom->shapes.push_back(
                Shape{net, rect_layer->getName(),
                      odb::Rect(last.x() + dx1, last.y() + dy1,
                                last.x() + dx2, last.y() + dy2),
                      true, false, ""});
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

// A pin of a compared net: its identity and one shape index
// per pin rectangle (a pin with several MPin boxes has
// several shapes, all the same pin).
struct PinRef {
  std::string name;
  std::vector<std::size_t> shapes;
};

struct NetInfo {
  std::string name;
  std::vector<PinRef> pins;  // sorted by pin name
};

// Collects the layout side (all shapes) and the logical side
// (the compared nets and their pins). Excluded nets (POWER,
// GROUND, special) are counted and their geometry is not
// walked. Terminal-less masters contribute nothing: their
// ITerms do not exist. A pin with no net is on neither side.
void Collect(odb::dbBlock* block, Geometry* geom,
             std::vector<NetInfo>* nets,
             std::vector<std::pair<std::size_t, std::size_t>>*
                 via_runs,
             std::size_t* special_skipped) {
  std::set<std::string> compared;
  for (odb::dbNet* net : block->getNets()) {
    const odb::dbSigType sig = net->getSigType();
    if (sig == odb::dbSigType::POWER ||
        sig == odb::dbSigType::GROUND || net->isSpecial()) {
      ++*special_skipped;
      continue;
    }
    compared.insert(net->getName());
    NetInfo info;
    info.name = net->getName();
    std::map<std::string, PinRef> pins;
    for (odb::dbITerm* iterm : net->getITerms()) {
      odb::dbInst* inst = iterm->getInst();
      odb::dbMTerm* mterm = iterm->getMTerm();
      if (inst == nullptr || mterm == nullptr) {
        continue;
      }
      PinRef& pin = pins[inst->getName() + ":" + mterm->getName()];
      pin.name = inst->getName() + ":" + mterm->getName();
      odb::dbTransform transform;
      inst->getTransform(transform);
      for (odb::dbMPin* mpin : mterm->getMPins()) {
        for (odb::dbBox* box : mpin->getGeometry()) {
          odb::dbTechLayer* layer = box->getTechLayer();
          if (layer == nullptr) {
            continue;
          }
          odb::Rect rect = box->getBox();
          transform.apply(rect);
          pin.shapes.push_back(geom->shapes.size());
          geom->shapes.push_back(Shape{info.name, layer->getName(),
                                       rect, false, true, pin.name});
        }
      }
    }
    for (odb::dbBTerm* bterm : block->getBTerms()) {
      if (bterm->getNet() != net) {
        continue;
      }
      PinRef& pin = pins[bterm->getName()];
      pin.name = bterm->getName();
      for (odb::dbBPin* bpin : bterm->getBPins()) {
        for (odb::dbBox* box : bpin->getBoxes()) {
          odb::dbTechLayer* layer = box->getTechLayer();
          if (layer == nullptr) {
            continue;
          }
          pin.shapes.push_back(geom->shapes.size());
          geom->shapes.push_back(Shape{info.name, layer->getName(),
                                       box->getBox(), false, true,
                                       pin.name});
        }
      }
    }
    for (auto& entry : pins) {
      info.pins.push_back(std::move(entry.second));
    }
    nets->push_back(std::move(info));
  }
  for (odb::dbNet* net : block->getNets()) {
    if (compared.find(net->getName()) == compared.end()) {
      continue;
    }
    DecodeWire(net->getWire(), net->getName(), geom, via_runs);
    for (odb::dbSWire* swire : net->getSWires()) {
      for (odb::dbSBox* sbox : swire->getWires()) {
        odb::dbTechLayer* layer = sbox->getTechLayer();
        if (layer == nullptr) {
          continue;
        }
        geom->shapes.push_back(Shape{net->getName(),
                                     layer->getName(), sbox->getBox(),
                                     true, false, ""});
        ++geom->wire_shape_count;
      }
    }
  }
}

class UnionFind {
 public:
  explicit UnionFind(std::size_t size) : parent_(size) {
    for (std::size_t i = 0; i < size; ++i) {
      parent_[i] = static_cast<int>(i);
    }
  }
  int Find(int x) {
    while (parent_[x] != x) {
      parent_[x] = parent_[parent_[x]];
      x = parent_[x];
    }
    return x;
  }
  void Unite(int a, int b) {
    int ra = Find(a);
    int rb = Find(b);
    if (ra != rb) {
      parent_[ra] = rb;
    }
  }

 private:
  std::vector<int> parent_;
};

int KindRank(const std::string& kind) {
  if (kind == "open") return 0;
  if (kind == "short") return 1;
  return 2;  // floating
}

bool MismatchLess(const LvsMismatch& a, const LvsMismatch& b) {
  if (KindRank(a.kind) != KindRank(b.kind)) {
    return KindRank(a.kind) < KindRank(b.kind);
  }
  if (a.net != b.net) return a.net < b.net;
  if (a.pin != b.pin) return a.pin < b.pin;
  if (a.other_net != b.other_net) return a.other_net < b.other_net;
  if (a.x1 != b.x1) return a.x1 < b.x1;
  if (a.y1 != b.y1) return a.y1 < b.y1;
  if (a.x2 != b.x2) return a.x2 < b.x2;
  return a.y2 < b.y2;
}

}  // namespace

std::size_t LvsRecord::CountKind(const std::string& kind) const {
  std::size_t count = 0;
  for (const LvsMismatch& mismatch : mismatches) {
    if (mismatch.kind == kind) {
      ++count;
    }
  }
  return count;
}

bool RunConnectivityLvs(odb::dbDatabase* db, LvsRecord* record,
                        std::string* error) {
  record->source = "live-database";
  record->nets_compared = 0;
  record->pins_compared = 0;
  record->single_pin_nets = 0;
  record->special_nets_skipped = 0;
  record->mismatches.clear();
  if (db == nullptr) {
    *error = "no database: no ODB database is loaded";
    return false;
  }
  if (db->getTech() == nullptr) {
    *error = "no technology data loaded: run eos::import_odb first";
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
  std::vector<NetInfo> nets;
  std::vector<std::pair<std::size_t, std::size_t>> via_runs;
  Collect(block, &geom, &nets, &via_runs,
          &record->special_nets_skipped);
  if (geom.wire_shape_count == 0) {
    *error = "no routed design: the top block holds no wires";
    return false;
  }
  std::size_t total_pins = 0;
  for (const NetInfo& net : nets) {
    total_pins += net.pins.size();
  }
  if (total_pins == 0) {
    *error =
        "no logical netlist: no net in the top block carries a pin";
    return false;
  }
  record->nets_compared = nets.size();
  record->pins_compared = total_pins;
  for (const NetInfo& net : nets) {
    if (net.pins.size() == 1) {
      ++record->single_pin_nets;
    }
  }

  // Components: union shapes that touch on the same layer,
  // and union each via instance's boxes together.
  const std::size_t shape_count = geom.shapes.size();
  UnionFind components(shape_count);
  std::map<std::string, std::vector<std::size_t>> by_layer;
  for (std::size_t i = 0; i < shape_count; ++i) {
    by_layer[geom.shapes[i].layer].push_back(i);
  }
  for (const auto& entry : by_layer) {
    const std::vector<std::size_t>& on_layer = entry.second;
    for (std::size_t i = 0; i < on_layer.size(); ++i) {
      for (std::size_t j = i + 1; j < on_layer.size(); ++j) {
        if (ShapesConnect(geom.shapes[on_layer[i]],
                          geom.shapes[on_layer[j]])) {
          components.Unite(static_cast<int>(on_layer[i]),
                           static_cast<int>(on_layer[j]));
        }
      }
    }
  }
  for (const auto& run : via_runs) {
    for (std::size_t i = run.first + 1; i < run.second; ++i) {
      components.Unite(static_cast<int>(run.first),
                       static_cast<int>(i));
    }
  }


  // Component membership: shapes, pins, and nets per root.
  std::map<int, std::vector<std::size_t>> members;
  for (std::size_t i = 0; i < shape_count; ++i) {
    members[components.Find(static_cast<int>(i))].push_back(i);
  }
  std::map<std::string, std::map<std::string, int>> pin_root;
  for (const NetInfo& net : nets) {
    for (const PinRef& pin : net.pins) {
      if (!pin.shapes.empty()) {
        pin_root[net.name][pin.name] =
            components.Find(static_cast<int>(pin.shapes.front()));
      }
    }
  }

  // Opens: a net's pins in more than one component. The
  // primary component holds the net's alphabetically first
  // pin (pins are stored sorted by name).
  for (const NetInfo& net : nets) {
    if (net.pins.size() < 2) {
      continue;
    }
    std::map<std::string, int>::const_iterator first =
        pin_root[net.name].find(net.pins.front().name);
    // A pin with no geometry is its own component: it gets a
    // unique negative root that no real component (roots are
    // shape indices) and no other pin-less pin can share.
    const int primary = first == pin_root[net.name].end()
                            ? -1
                            : first->second;
    for (std::size_t p = 1; p < net.pins.size(); ++p) {
      const PinRef& pin = net.pins[p];
      std::map<std::string, int>::const_iterator found =
          pin_root[net.name].find(pin.name);
      const int root = found == pin_root[net.name].end()
                           ? -1 - static_cast<int>(p)
                           : found->second;
      if (root == primary) {
        continue;
      }
      // The bbox is the pin rectangle: the first shape, or a
      // zero box at the origin for a pin with no geometry.
      odb::Rect bbox(0, 0, 0, 0);
      if (!pin.shapes.empty()) {
        bbox = geom.shapes[pin.shapes.front()].rect;
      }
      record->mismatches.push_back(LvsMismatch{
          "open", net.name, "", pin.name, "", bbox.xMin(),
          bbox.yMin(), bbox.xMax(), bbox.yMax()});
    }
  }

  // Shorts and floating, per component.
  for (const auto& entry : members) {
    const std::vector<std::size_t>& shapes = entry.second;
    std::set<std::string> comp_nets;
    bool has_pin = false;
    bool has_wire = false;
    for (std::size_t index : shapes) {
      comp_nets.insert(geom.shapes[index].net);
      has_pin = has_pin || geom.shapes[index].is_pin;
      has_wire = has_wire || geom.shapes[index].is_wire;
    }
    if (comp_nets.size() >= 2) {
      const std::string first_net = *comp_nets.begin();
      for (auto it = std::next(comp_nets.begin());
           it != comp_nets.end(); ++it) {
        // The contact: the first touching pair (in shape
        // order) between the first net and this net.
        bool found = false;
        std::string layer;
        odb::Rect contact(0, 0, 0, 0);
        for (std::size_t i = 0; i < shapes.size() && !found; ++i) {
          for (std::size_t j = 0; j < shapes.size() && !found;
               ++j) {
            const Shape& a = geom.shapes[shapes[i]];
            const Shape& b = geom.shapes[shapes[j]];
            const bool pair = (a.net == first_net && b.net == *it) ||
                              (a.net == *it && b.net == first_net);
            if (pair && a.layer == b.layer &&
                ShapesConnect(a, b)) {
              layer = a.layer;
              contact = RectIntersection(a.rect, b.rect);
              found = true;
            }
          }
        }
        if (!found) {
          // The two nets share the component through a via
          // union or a chain, with no direct same-layer
          // contact; name the first net's first shape layer
          // and the component bbox.
          odb::Rect bbox;
          bbox.mergeInit();
          for (std::size_t index : shapes) {
            if (geom.shapes[index].net == first_net && layer.empty()) {
              layer = geom.shapes[index].layer;
            }
            bbox.merge(geom.shapes[index].rect);
          }
          contact = bbox;
        }
        record->mismatches.push_back(LvsMismatch{
            "short", first_net, *it, "", layer, contact.xMin(),
            contact.yMin(), contact.xMax(), contact.yMax()});
      }
    } else if (comp_nets.size() == 1 && has_wire && !has_pin) {
      odb::Rect bbox;
      bbox.mergeInit();
      for (std::size_t index : shapes) {
        bbox.merge(geom.shapes[index].rect);
      }
      record->mismatches.push_back(LvsMismatch{
          "floating", *comp_nets.begin(), "", "",
          geom.shapes[shapes.front()].layer, bbox.xMin(),
          bbox.yMin(), bbox.xMax(), bbox.yMax()});
    }
  }

  std::sort(record->mismatches.begin(), record->mismatches.end(),
            MismatchLess);
  return true;
}

}  // namespace eos::pv
