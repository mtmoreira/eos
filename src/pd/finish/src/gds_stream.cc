// gds_stream: EOS-owned GDSII stream writer. See gds_stream.h.
#include "gds_stream.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "odb/db.h"
#include "odb/dbShape.h"
#include "odb/dbWireCodec.h"

namespace eos::pd::gds {

namespace {

// GDSII record types and data types used here.
constexpr uint8_t kHeader = 0;
constexpr uint8_t kBgnLib = 1;
constexpr uint8_t kLibName = 2;
constexpr uint8_t kUnits = 3;
constexpr uint8_t kEndLib = 4;
constexpr uint8_t kBgnStr = 5;
constexpr uint8_t kStrName = 6;
constexpr uint8_t kEndStr = 7;
constexpr uint8_t kBoundary = 8;
constexpr uint8_t kPath = 9;
constexpr uint8_t kSRef = 10;
constexpr uint8_t kLayer = 13;
constexpr uint8_t kDataType = 14;
constexpr uint8_t kWidth = 15;
constexpr uint8_t kXy = 16;
constexpr uint8_t kStrans = 26;
constexpr uint8_t kAngle = 28;
constexpr uint8_t kSRefName = 18;  // SNAME record type
constexpr uint8_t kDataNone = 0;
constexpr uint8_t kDataInt16 = 1;
constexpr uint8_t kDataInt32 = 2;
constexpr uint8_t kDataReal8 = 3;
constexpr uint8_t kDataString = 6;

struct Point {
  int x = 0;
  int y = 0;
};

// GDSII names hold at most 32 characters and no whitespace: sanitize
// anything else to '_', and truncate. An empty name becomes "top".
std::string SanitizeName(const std::string& name)
{
  std::string out;
  for (char c : name) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0') {
      out.push_back('_');
    } else {
      out.push_back(c);
    }
  }
  if (out.empty()) {
    out = "top";
  }
  if (out.size() > 32) {
    out.resize(32);
  }
  return out;
}

class Writer {
 public:
  explicit Writer(std::ofstream& out) : out_(out) {}

  bool good() const { return out_.good(); }

  void U16(uint16_t v)
  {
    out_.put(static_cast<char>((v >> 8) & 0xff));
    out_.put(static_cast<char>(v & 0xff));
  }

  void I16(int16_t v) { U16(static_cast<uint16_t>(v)); }

  void I32(int32_t v)
  {
    out_.put(static_cast<char>((v >> 24) & 0xff));
    out_.put(static_cast<char>((v >> 16) & 0xff));
    out_.put(static_cast<char>((v >> 8) & 0xff));
    out_.put(static_cast<char>(v & 0xff));
  }

  void Bytes(const std::string& data) { out_.write(data.data(), data.size()); }

  // One record: length counts the 4 header bytes plus the data.
  void Record(uint8_t type, uint8_t datatype,
              const std::string& data)
  {
    U16(static_cast<uint16_t>(4 + data.size()));
    out_.put(static_cast<char>(type));
    out_.put(static_cast<char>(datatype));
    Bytes(data);
  }

  void Int16Record(uint8_t type, int16_t v)
  {
    std::string data;
    data.push_back(static_cast<char>((v >> 8) & 0xff));
    data.push_back(static_cast<char>(v & 0xff));
    Record(type, kDataInt16, data);
  }

  void Int32Record(uint8_t type, int32_t v)
  {
    std::string data;
    data.push_back(static_cast<char>((v >> 24) & 0xff));
    data.push_back(static_cast<char>((v >> 16) & 0xff));
    data.push_back(static_cast<char>((v >> 8) & 0xff));
    data.push_back(static_cast<char>(v & 0xff));
    Record(type, kDataInt32, data);
  }

  void StringRecord(uint8_t type, const std::string& value)
  {
    std::string data = value;
    if (data.size() % 2 != 0) {
      data.push_back('\0');
    }
    Record(type, kDataString, data);
  }

  // GDS 8-byte real: bit 63 sign, bits 62-56 exponent (base 16,
  // bias 64), bits 55-0 a 56-bit fraction with the binary point
  // before the first hex digit. Zero is all zeros.
  static std::string RealBytes(double value)
  {
    std::string data;
    uint64_t bits = 0;
    if (value != 0.0) {
      const bool negative = value < 0.0;
      double mantissa = std::fabs(value);
      int exponent = 0;
      while (mantissa >= 1.0) {
        mantissa /= 16.0;
        ++exponent;
      }
      while (mantissa < 0.0625) {
        mantissa *= 16.0;
        --exponent;
      }
      uint64_t fraction =
          static_cast<uint64_t>(mantissa * 72057594037927936.0);
      bits = (negative ? (1ULL << 63) : 0ULL) |
             (static_cast<uint64_t>(exponent + 64) << 56) |
             (fraction & 0x00ffffffffffffffULL);
    }
    for (int i = 7; i >= 0; --i) {
      data.push_back(static_cast<char>((bits >> (i * 8)) & 0xff));
    }
    return data;
  }

  void RealRecord(uint8_t type, double value)
  {
    Record(type, kDataReal8, RealBytes(value));
  }

  void Timestamps(uint8_t type)
  {
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    std::string data;
    auto put16 = [&data](int v) {
      data.push_back(static_cast<char>((v >> 8) & 0xff));
      data.push_back(static_cast<char>(v & 0xff));
    };
    // GDS timestamps are two 6-word groups (modification, access):
    // year, month, day, hour, minute, second. The year is the
    // calendar year (not since 1900) in the stream format's
    // two-byte field as written by common tools.
    for (int group = 0; group < 2; ++group) {
      put16(tmv.tm_year + 1900);
      put16(tmv.tm_mon + 1);
      put16(tmv.tm_mday);
      put16(tmv.tm_hour);
      put16(tmv.tm_min);
      put16(tmv.tm_sec);
    }
    Record(type, kDataInt16, data);
  }

  void Xy(const std::vector<Point>& points)
  {
    std::string data;
    for (const Point& p : points) {
      auto put32 = [&data](int32_t v) {
        data.push_back(static_cast<char>((v >> 24) & 0xff));
        data.push_back(static_cast<char>((v >> 16) & 0xff));
        data.push_back(static_cast<char>((v >> 8) & 0xff));
        data.push_back(static_cast<char>(v & 0xff));
      };
      put32(p.x);
      put32(p.y);
    }
    Record(kXy, kDataInt32, data);
  }

  void Boundary(int layer, const std::vector<Point>& points)
  {
    Record(kBoundary, kDataNone, "");
    Int16Record(kLayer, static_cast<int16_t>(layer));
    Int16Record(kDataType, 0);
    Xy(points);
  }

  void RectBoundary(int layer, int x1, int y1, int x2, int y2)
  {
    Boundary(layer, {{x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}, {x1, y1}});
  }

 private:
  std::ofstream& out_;
};

// GDS layer number for a technology layer: the mask-order number
// OpenDB carries (getNumber), falling back to the layer's position
// in the technology's layer list when the number is zero.
int GdsLayer(odb::dbTech* tech, odb::dbTechLayer* layer)
{
  if (layer == nullptr) {
    return 0;
  }
  const int number = layer->getNumber();
  if (number > 0) {
    return number;
  }
  int index = 1;
  for (odb::dbTechLayer* candidate : tech->getLayers()) {
    if (candidate == layer) {
      return index;
    }
    ++index;
  }
  return index;
}

// Orientation to GDS SREF angle and reflection flag. GDS applies
// reflection (about the x-axis) first, then rotation. MY is a
// y-mirror, which equals an x-mirror rotated 180 degrees; MYR90
// equals an x-mirror rotated 270 degrees.
void OrientToGds(odb::dbOrientType orient, double& angle, bool& reflect)
{
  using Value = odb::dbOrientType::Value;
  switch (orient.getValue()) {
    case Value::R0:
      angle = 0.0;
      reflect = false;
      break;
    case Value::R90:
      angle = 90.0;
      reflect = false;
      break;
    case Value::R180:
      angle = 180.0;
      reflect = false;
      break;
    case Value::R270:
      angle = 270.0;
      reflect = false;
      break;
    case Value::MX:
      angle = 0.0;
      reflect = true;
      break;
    case Value::MXR90:
      angle = 90.0;
      reflect = true;
      break;
    case Value::MY:
      angle = 180.0;
      reflect = true;
      break;
    case Value::MYR90:
      angle = 270.0;
      reflect = true;
      break;
    default:
      angle = 0.0;
      reflect = false;
      break;
  }
}

}  // namespace

bool WriteGdsStream(odb::dbDatabase* db, const std::string& path,
                    GdsStreamReport* report, std::string* error)
{
  odb::dbTech* tech = db != nullptr ? db->getTech() : nullptr;
  odb::dbBlock* block =
      (db != nullptr && db->getChip() != nullptr)
          ? db->getChip()->getBlock()
          : nullptr;
  if (tech == nullptr || block == nullptr) {
    if (error != nullptr) {
      *error = "write_gds: no technology data or mapped design";
    }
    return false;
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.good()) {
    if (error != nullptr) {
      *error = "write_gds: cannot open file " + path;
    }
    return false;
  }
  Writer w(out);

  const int dbu_per_micron = tech->getDbUnitsPerMicron();
  w.Int16Record(kHeader, 3);
  w.Timestamps(kBgnLib);
  w.StringRecord(kLibName, "eos");
  {
    // UNITS: size of one database unit in user units (microns),
    // then size of one database unit in meters.
    const double per_micron =
        dbu_per_micron > 0 ? 1.0 / dbu_per_micron : 0.001;
    w.Record(kUnits, kDataReal8,
             Writer::RealBytes(per_micron) +
                 Writer::RealBytes(per_micron * 1e-6));
  }

  // Masters used by placed instances, in first-use order.
  std::vector<odb::dbMaster*> masters;
  std::set<odb::dbMaster*> seen_masters;
  for (odb::dbInst* inst : block->getInsts()) {
    if (!inst->isPlaced()) {
      continue;
    }
    odb::dbMaster* master = inst->getMaster();
    if (master != nullptr && seen_masters.insert(master).second) {
      masters.push_back(master);
    }
  }

  int structure_count = 0;
  for (odb::dbMaster* master : masters) {
    w.Timestamps(kBgnStr);
    w.StringRecord(kStrName, SanitizeName(master->getName()));
    for (odb::dbMTerm* mterm : master->getMTerms()) {
      for (odb::dbMPin* mpin : mterm->getMPins()) {
        for (odb::dbBox* box : mpin->getGeometry()) {
          odb::dbTechLayer* layer = box->getTechLayer();
          if (layer == nullptr) {
            continue;
          }
          w.RectBoundary(GdsLayer(tech, layer), box->xMin(),
                         box->yMin(), box->xMax(), box->yMax());
        }
      }
    }
    w.Record(kEndStr, kDataNone, "");
    ++structure_count;
  }

  // Top structure: instances, wires, special wires, fills.
  w.Timestamps(kBgnStr);
  w.StringRecord(kStrName, SanitizeName(block->getName()));
  int instance_count = 0;
  for (odb::dbInst* inst : block->getInsts()) {
    if (!inst->isPlaced()) {
      continue;
    }
    odb::dbMaster* master = inst->getMaster();
    if (master == nullptr) {
      continue;
    }
    int ox = 0;
    int oy = 0;
    inst->getOrigin(ox, oy);
    double angle = 0.0;
    bool reflect = false;
    OrientToGds(inst->getOrient(), angle, reflect);
    w.Record(kSRef, kDataNone, "");
    w.StringRecord(kSRefName, SanitizeName(master->getName()));
    if (reflect) {
      w.Int16Record(kStrans, static_cast<int16_t>(0x8000));
    }
    if (angle != 0.0) {
      w.RealRecord(kAngle, angle);
    }
    w.Xy({{ox, oy}});
    ++instance_count;
  }

  // Routed wires, decoded opcode by opcode. A PATH run accumulates
  // points until the layer changes, a via appears, or the wire
  // ends; junction branches are seeded from the junction point map
  // (each POINT records its junction id's latest point).
  for (odb::dbNet* net : block->getNets()) {
    odb::dbWire* wire = net->getWire();
    if (wire == nullptr) {
      continue;
    }
    odb::dbWireDecoder decoder;
    decoder.begin(wire);
    std::vector<Point> current;
    odb::dbTechLayer* current_layer = nullptr;
    int current_width = 0;
    std::map<int, Point> junction_points;
    // Last decoded point, for RECT opcodes: the encoder stores a
    // patch rect as deltas from the point added just before it
    // (see dbWireEncoder::addRect and its drt caller).
    Point last_point{0, 0};
    bool have_last_point = false;
    auto flush_path = [&]() {
      if (current.size() >= 2 && current_layer != nullptr) {
        w.Record(kPath, kDataNone, "");
        w.Int16Record(kLayer, static_cast<int16_t>(
                                   GdsLayer(tech, current_layer)));
        w.Int16Record(kDataType, 0);
        if (current_width > 0) {
          w.Int32Record(kWidth, current_width);
        }
        w.Xy(current);
      }
      current.clear();
      current_layer = nullptr;
      current_width = 0;
    };
    while (true) {
      const odb::dbWireDecoder::OpCode op = decoder.next();
      if (op == odb::dbWireDecoder::END_DECODE) {
        break;
      }
      switch (op) {
        case odb::dbWireDecoder::PATH: {
          flush_path();
          current_layer = decoder.getLayer();
          current_width = current_layer != nullptr
                              ? current_layer->getWidth()
                              : 0;
          break;
        }
        case odb::dbWireDecoder::JUNCTION:
        case odb::dbWireDecoder::SHORT:
        case odb::dbWireDecoder::VWIRE: {
          flush_path();
          current_layer = decoder.getLayer();
          current_width = current_layer != nullptr
                              ? current_layer->getWidth()
                              : 0;
          auto it = junction_points.find(decoder.getJunctionValue());
          if (it != junction_points.end()) {
            current.push_back(it->second);
          }
          break;
        }
        case odb::dbWireDecoder::POINT: {
          int x = 0;
          int y = 0;
          decoder.getPoint(x, y);
          const Point p{x, y};
          junction_points[decoder.getJunctionId()] = p;
          last_point = p;
          have_last_point = true;
          current.push_back(p);
          break;
        }
        case odb::dbWireDecoder::POINT_EXT: {
          int x = 0;
          int y = 0;
          int ext = 0;
          decoder.getPoint(x, y, ext);
          const Point p{x, y};
          junction_points[decoder.getJunctionId()] = p;
          last_point = p;
          have_last_point = true;
          current.push_back(p);
          break;
        }
        case odb::dbWireDecoder::VIA: {
          flush_path();
          odb::dbVia* via = decoder.getVia();
          if (via != nullptr) {
            for (odb::dbBox* box : via->getBoxes()) {
              odb::dbTechLayer* layer = box->getTechLayer();
              if (layer == nullptr) {
                continue;
              }
              w.RectBoundary(GdsLayer(tech, layer), box->xMin(),
                             box->yMin(), box->xMax(), box->yMax());
            }
          }
          break;
        }
        case odb::dbWireDecoder::TECH_VIA: {
          flush_path();
          odb::dbTechVia* via = decoder.getTechVia();
          if (via != nullptr) {
            for (odb::dbBox* box : via->getBoxes()) {
              odb::dbTechLayer* layer = box->getTechLayer();
              if (layer == nullptr) {
                continue;
              }
              w.RectBoundary(GdsLayer(tech, layer), box->xMin(),
                             box->yMin(), box->xMax(), box->yMax());
            }
          }
          break;
        }
        case odb::dbWireDecoder::RECT: {
          flush_path();
          int dx1 = 0;
          int dy1 = 0;
          int dx2 = 0;
          int dy2 = 0;
          decoder.getRect(dx1, dy1, dx2, dy2);
          // Patch rect, stored as deltas from the last point, on
          // the layer the decoder still reports for this opcode
          // (RECT does not change the decoder's layer).
          if (have_last_point) {
            odb::dbTechLayer* rect_layer = decoder.getLayer();
            if (rect_layer != nullptr) {
              w.RectBoundary(GdsLayer(tech, rect_layer),
                             last_point.x + dx1, last_point.y + dy1,
                             last_point.x + dx2, last_point.y + dy2);
            }
          }
          break;
        }
        default:
          break;
      }
    }
    flush_path();

    // Special wires (the power grid) live beside the regular wire.
    for (odb::dbSWire* swire : net->getSWires()) {
      for (odb::dbSBox* sbox : swire->getWires()) {
        odb::dbTechLayer* layer = sbox->getTechLayer();
        if (layer == nullptr) {
          continue;
        }
        w.RectBoundary(GdsLayer(tech, layer), sbox->xMin(),
                       sbox->yMin(), sbox->xMax(), sbox->yMax());
      }
    }
  }

  // Metal fills.
  for (odb::dbFill* fill : block->getFills()) {
    odb::dbTechLayer* layer = fill->getTechLayer();
    if (layer == nullptr) {
      continue;
    }
    odb::Rect rect;
    fill->getRect(rect);
    w.RectBoundary(GdsLayer(tech, layer), rect.xMin(), rect.yMin(),
                   rect.xMax(), rect.yMax());
  }

  w.Record(kEndStr, kDataNone, "");
  ++structure_count;
  w.Record(kEndLib, kDataNone, "");
  out.flush();
  if (!out.good()) {
    if (error != nullptr) {
      *error = "write_gds: failed while writing " + path;
    }
    return false;
  }
  out.close();
  if (report != nullptr) {
    report->instance_count = instance_count;
    report->structure_count = structure_count;
  }
  return true;
}

}  // namespace eos::pd::gds
