// Unit tests: io/gds, the GDSII stream reader.
//
// The fixture is a record writer in the EOS writer's own
// grammar (src/pd/finish/src/gds_stream.cc): its record and
// data type numbers, its timestamp records, its omitted
// ENDEL records. The reader's required input domain is that
// writer's output, so the tests build streams the way the
// writer does and judge the model objects that come back.
#include "eos/io/gds.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace eos::io {
namespace {

// Data types, in the writer's numbering.
constexpr std::uint8_t kDataNone = 0;
constexpr std::uint8_t kDataInt16 = 1;
constexpr std::uint8_t kDataInt32 = 2;
constexpr std::uint8_t kDataReal8 = 3;
constexpr std::uint8_t kDataString = 6;

// Record types the fixture emits.
constexpr std::uint8_t kHeader = 0;
constexpr std::uint8_t kBgnLib = 1;
constexpr std::uint8_t kLibName = 2;
constexpr std::uint8_t kUnits = 3;
constexpr std::uint8_t kEndLib = 4;
constexpr std::uint8_t kBgnStr = 5;
constexpr std::uint8_t kStrName = 6;
constexpr std::uint8_t kEndStr = 7;
constexpr std::uint8_t kBoundary = 8;
constexpr std::uint8_t kPath = 9;
constexpr std::uint8_t kSRef = 10;
constexpr std::uint8_t kARef = 11;
constexpr std::uint8_t kText = 12;
constexpr std::uint8_t kLayer = 13;
constexpr std::uint8_t kDataType = 14;
constexpr std::uint8_t kWidth = 15;
constexpr std::uint8_t kXy = 16;
constexpr std::uint8_t kEndEl = 17;
constexpr std::uint8_t kSName = 18;
constexpr std::uint8_t kStrans = 26;
constexpr std::uint8_t kAngle = 28;

// Encodes one real8 in the writer's format: sign bit,
// base-16 exponent biased by 64, 56-bit fraction with the
// binary point before the first hex digit.
std::vector<std::uint8_t> Real8Bytes(double value) {
  std::vector<std::uint8_t> out(8, 0);
  if (value == 0.0) return out;
  const bool negative = value < 0;
  double fraction = std::fabs(value);
  int exponent = 0;
  while (fraction >= 1.0) {
    fraction /= 16.0;
    ++exponent;
  }
  while (fraction < 1.0 / 16.0) {
    fraction *= 16.0;
    --exponent;
  }
  const std::uint64_t bits_fraction = static_cast<std::uint64_t>(
      std::llround(fraction * 72057594037927936.0));  // 2^56
  std::uint64_t bits =
      (static_cast<std::uint64_t>(exponent + 64) << 56) |
      bits_fraction;
  if (negative) bits |= (1ULL << 63);
  for (int i = 0; i < 8; ++i) {
    out[i] = static_cast<std::uint8_t>((bits >> (56 - i * 8)) & 0xff);
  }
  return out;
}

// Appends records to one stream, the way the writer does.
class StreamBuilder {
 public:
  void Record(std::uint8_t type, std::uint8_t datatype,
              const std::vector<std::uint8_t>& payload) {
    const std::uint16_t length =
        static_cast<std::uint16_t>(payload.size() + 4);
    bytes_.push_back(static_cast<std::uint8_t>(length >> 8));
    bytes_.push_back(static_cast<std::uint8_t>(length & 0xff));
    bytes_.push_back(type);
    bytes_.push_back(datatype);
    bytes_.insert(bytes_.end(), payload.begin(), payload.end());
  }

  void NoneRecord(std::uint8_t type) { Record(type, kDataNone, {}); }

  void Int16Record(std::uint8_t type, std::int16_t value) {
    const std::uint16_t bits = static_cast<std::uint16_t>(value);
    Record(type, kDataInt16,
           {static_cast<std::uint8_t>(bits >> 8),
            static_cast<std::uint8_t>(bits & 0xff)});
  }

  void TimestampRecord(std::uint8_t type) {
    std::vector<std::uint8_t> payload;
    for (int i = 0; i < 12; ++i) {
      payload.push_back(0);
      payload.push_back(static_cast<std::uint8_t>(i + 1));
    }
    Record(type, kDataInt16, payload);
  }

  void StringRecord(std::uint8_t type, const std::string& value) {
    std::vector<std::uint8_t> payload(value.begin(), value.end());
    if (payload.size() % 2 != 0) payload.push_back(0);
    Record(type, kDataString, payload);
  }

  void XyRecord(const std::vector<std::pair<int, int>>& points) {
    std::vector<std::uint8_t> payload;
    for (const auto& point : points) {
      for (int coord : {point.first, point.second}) {
        const std::uint32_t bits = static_cast<std::uint32_t>(coord);
        for (int shift = 24; shift >= 0; shift -= 8) {
          payload.push_back(
              static_cast<std::uint8_t>((bits >> shift) & 0xff));
        }
      }
    }
    Record(kXy, kDataInt32, payload);
  }

  void RealRecord(std::uint8_t type, double value) {
    Record(type, kDataReal8, Real8Bytes(value));
  }

  const std::vector<std::uint8_t>& bytes() const { return bytes_; }

 private:
  std::vector<std::uint8_t> bytes_;
};

class TempDir {
 public:
  TempDir()
      : path_(std::filesystem::temp_directory_path() /
              ("eos_gds_test_" + std::to_string(counter_++))) {
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  std::string Write(const std::string& name,
                    const std::vector<std::uint8_t>& bytes) const {
    const std::filesystem::path file = path_ / name;
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    return file.string();
  }

  std::string Path(const std::string& name) const {
    return (path_ / name).string();
  }

 private:
  std::filesystem::path path_;
  static inline int counter_ = 0;
};

// One library in the writer's grammar: a master structure
// with one boundary, and a top structure with one boundary,
// one path, and two SREFs of the master (one reflected at
// 90 degrees, one plain). With endel set, every element
// ends with an ENDEL record; the writer omits them, and
// the reader must accept both shapes.
std::vector<std::uint8_t> WriterGrammarStream(bool endel) {
  StreamBuilder out;
  out.Int16Record(kHeader, 600);
  out.TimestampRecord(kBgnLib);
  out.StringRecord(kLibName, "EOSMOCK");
  {
    std::vector<std::uint8_t> units = Real8Bytes(0.001);
    const std::vector<std::uint8_t> meters = Real8Bytes(1e-9);
    units.insert(units.end(), meters.begin(), meters.end());
    out.Record(kUnits, kDataReal8, units);
  }
  out.TimestampRecord(kBgnStr);
  out.StringRecord(kStrName, "BUF_X1");
  out.NoneRecord(kBoundary);
  out.Int16Record(kLayer, 1);
  out.Int16Record(kDataType, 0);
  out.XyRecord({{0, 0}, {100, 0}, {100, 100}, {0, 100}});
  if (endel) out.NoneRecord(kEndEl);
  out.NoneRecord(kEndStr);
  out.TimestampRecord(kBgnStr);
  out.StringRecord(kStrName, "top");
  out.NoneRecord(kBoundary);
  out.Int16Record(kLayer, 3);
  out.Int16Record(kDataType, 0);
  out.XyRecord({{-50, -50}, {50, -50}, {50, 50}, {-50, 50}});
  if (endel) out.NoneRecord(kEndEl);
  out.NoneRecord(kPath);
  out.Int16Record(kLayer, 2);
  out.Int16Record(kDataType, 0);
  {
    const std::uint32_t bits = 50;
    out.Record(kWidth, kDataInt32,
               {static_cast<std::uint8_t>(bits >> 24),
                static_cast<std::uint8_t>(bits >> 16),
                static_cast<std::uint8_t>(bits >> 8),
                static_cast<std::uint8_t>(bits)});
  }
  out.XyRecord({{0, 0}, {500, 0}});
  if (endel) out.NoneRecord(kEndEl);
  out.NoneRecord(kSRef);
  out.StringRecord(kSName, "BUF_X1");
  out.Int16Record(kStrans, static_cast<std::int16_t>(0x8000));
  out.RealRecord(kAngle, 90.0);
  out.XyRecord({{1000, 2000}});
  if (endel) out.NoneRecord(kEndEl);
  out.NoneRecord(kSRef);
  out.StringRecord(kSName, "BUF_X1");
  out.XyRecord({{-500, 250}});
  if (endel) out.NoneRecord(kEndEl);
  out.NoneRecord(kEndStr);
  out.NoneRecord(kEndLib);
  return out.bytes();
}

void ExpectWriterGrammarLibrary(const GdsLibrary& library) {
  EXPECT_EQ(library.name, "EOSMOCK");
  EXPECT_NEAR(library.dbu_in_user_units, 0.001, 1e-15);
  EXPECT_NEAR(library.dbu_in_meters, 1e-9, 1e-21);
  ASSERT_EQ(library.structures.size(), 2u);
  const GdsStructure& master = library.structures[0];
  EXPECT_EQ(master.name, "BUF_X1");
  ASSERT_EQ(master.boundaries.size(), 1u);
  EXPECT_EQ(master.boundaries[0].layer, 1);
  EXPECT_EQ(master.boundaries[0].datatype, 0);
  ASSERT_EQ(master.boundaries[0].points.size(), 4u);
  EXPECT_EQ(master.boundaries[0].points[2].x, 100);
  EXPECT_EQ(master.boundaries[0].points[2].y, 100);
  EXPECT_TRUE(master.paths.empty());
  EXPECT_TRUE(master.srefs.empty());
  const GdsStructure& top = library.structures[1];
  EXPECT_EQ(top.name, "top");
  ASSERT_EQ(top.boundaries.size(), 1u);
  EXPECT_EQ(top.boundaries[0].layer, 3);
  EXPECT_EQ(top.boundaries[0].points[0].x, -50);
  ASSERT_EQ(top.paths.size(), 1u);
  EXPECT_EQ(top.paths[0].layer, 2);
  ASSERT_TRUE(top.paths[0].has_width);
  EXPECT_EQ(top.paths[0].width, 50);
  ASSERT_EQ(top.paths[0].points.size(), 2u);
  EXPECT_EQ(top.paths[0].points[1].x, 500);
  ASSERT_EQ(top.srefs.size(), 2u);
  EXPECT_EQ(top.srefs[0].target, "BUF_X1");
  EXPECT_EQ(top.srefs[0].x, 1000);
  EXPECT_EQ(top.srefs[0].y, 2000);
  EXPECT_TRUE(top.srefs[0].reflect);
  ASSERT_TRUE(top.srefs[0].has_angle);
  EXPECT_DOUBLE_EQ(top.srefs[0].angle_deg, 90.0);
  EXPECT_EQ(top.srefs[1].target, "BUF_X1");
  EXPECT_EQ(top.srefs[1].x, -500);
  EXPECT_EQ(top.srefs[1].y, 250);
  EXPECT_FALSE(top.srefs[1].reflect);
  EXPECT_FALSE(top.srefs[1].has_angle);
}

TEST(GdsReader, ParsesWriterGrammar) {
  TempDir dir;
  const std::string path =
      dir.Write("lib.gds", WriterGrammarStream(false));
  const foundation::Result<GdsLibrary> library = ReadGdsFile(path);
  ASSERT_TRUE(library.ok()) << library.status().message();
  ExpectWriterGrammarLibrary(library.value());
}

// The writer omits ENDEL everywhere; a stream that carries
// them (a foreign writer's shape) parses to the same model.
TEST(GdsReader, ToleratesEndel) {
  TempDir dir;
  const std::string path =
      dir.Write("lib.gds", WriterGrammarStream(true));
  const foundation::Result<GdsLibrary> library = ReadGdsFile(path);
  ASSERT_TRUE(library.ok()) << library.status().message();
  ExpectWriterGrammarLibrary(library.value());
}

// Unmodeled elements (AREF, TEXT) and unknown record types
// are skipped by their declared length: the modeled
// elements around them still parse, and nothing of the
// skipped elements lands in the model.
TEST(GdsReader, SkipsUnknownRecords) {
  StreamBuilder out;
  out.Int16Record(kHeader, 600);
  out.TimestampRecord(kBgnLib);
  out.StringRecord(kLibName, "EOSMOCK");
  {
    std::vector<std::uint8_t> units = Real8Bytes(0.001);
    const std::vector<std::uint8_t> meters = Real8Bytes(1e-9);
    units.insert(units.end(), meters.begin(), meters.end());
    out.Record(kUnits, kDataReal8, units);
  }
  out.TimestampRecord(kBgnStr);
  out.StringRecord(kStrName, "top");
  out.NoneRecord(kARef);
  out.StringRecord(kSName, "BUF_X1");
  out.XyRecord({{0, 0}, {100, 0}, {0, 100}});
  out.NoneRecord(kText);
  out.Int16Record(kLayer, 1);
  out.XyRecord({{7, 7}});
  out.Record(99, kDataInt16, {0, 1});
  out.NoneRecord(kBoundary);
  out.Int16Record(kLayer, 1);
  out.Int16Record(kDataType, 0);
  out.XyRecord({{0, 0}, {10, 0}, {10, 10}});
  out.NoneRecord(kEndStr);
  out.NoneRecord(kEndLib);
  TempDir dir;
  const std::string path = dir.Write("lib.gds", out.bytes());
  const foundation::Result<GdsLibrary> library = ReadGdsFile(path);
  ASSERT_TRUE(library.ok()) << library.status().message();
  ASSERT_EQ(library.value().structures.size(), 1u);
  const GdsStructure& top = library.value().structures[0];
  EXPECT_EQ(top.name, "top");
  ASSERT_EQ(top.boundaries.size(), 1u);
  EXPECT_EQ(top.boundaries[0].points.size(), 3u);
  EXPECT_TRUE(top.paths.empty());
  EXPECT_TRUE(top.srefs.empty());
}

// Malformed streams fail fast and total, naming the file
// and the record offset: no partial library comes back.
TEST(GdsReader, RejectsMalformedStreams) {
  TempDir dir;
  const std::vector<std::uint8_t> good = WriterGrammarStream(false);
  // Truncated mid-record.
  {
    std::vector<std::uint8_t> bytes = good;
    bytes.resize(bytes.size() - 3);
    const std::string path = dir.Write("truncated.gds", bytes);
    const foundation::Result<GdsLibrary> library =
        ReadGdsFile(path);
    ASSERT_FALSE(library.ok());
    EXPECT_NE(library.status().message().find(path),
              std::string::npos);
    EXPECT_NE(library.status().message().find("offset"),
              std::string::npos);
  }
  // A record length under 4.
  {
    std::vector<std::uint8_t> bytes = {0, 3, kHeader, kDataInt16, 0, 0};
    const std::string path = dir.Write("short.gds", bytes);
    const foundation::Result<GdsLibrary> library =
        ReadGdsFile(path);
    ASSERT_FALSE(library.ok());
    EXPECT_NE(library.status().message().find("offset"),
              std::string::npos);
  }
  // The first record is not HEADER.
  {
    StreamBuilder out;
    out.TimestampRecord(kBgnLib);
    out.NoneRecord(kEndLib);
    const std::string path = dir.Write("noheader.gds", out.bytes());
    const foundation::Result<GdsLibrary> library =
        ReadGdsFile(path);
    ASSERT_FALSE(library.ok());
  }
  // An XY payload that is not a whole number of points.
  {
    StreamBuilder out;
    out.Int16Record(kHeader, 600);
    out.TimestampRecord(kBgnLib);
    out.StringRecord(kLibName, "EOSMOCK");
    out.TimestampRecord(kBgnStr);
    out.StringRecord(kStrName, "top");
    out.NoneRecord(kBoundary);
    out.Int16Record(kLayer, 1);
    out.Record(kXy, kDataInt32, {0, 0, 0, 5});
    out.NoneRecord(kEndStr);
    out.NoneRecord(kEndLib);
    const std::string path = dir.Write("badxy.gds", out.bytes());
    const foundation::Result<GdsLibrary> library =
        ReadGdsFile(path);
    ASSERT_FALSE(library.ok());
  }
  // A structure with no STRNAME.
  {
    StreamBuilder out;
    out.Int16Record(kHeader, 600);
    out.TimestampRecord(kBgnLib);
    out.StringRecord(kLibName, "EOSMOCK");
    out.TimestampRecord(kBgnStr);
    out.NoneRecord(kEndStr);
    out.NoneRecord(kEndLib);
    const std::string path = dir.Write("noname.gds", out.bytes());
    const foundation::Result<GdsLibrary> library =
        ReadGdsFile(path);
    ASSERT_FALSE(library.ok());
  }
  // An empty file.
  {
    const std::string path = dir.Write("empty.gds", {});
    const foundation::Result<GdsLibrary> library =
        ReadGdsFile(path);
    ASSERT_FALSE(library.ok());
  }
}

// A missing file fails before any parsing, naming the path.
TEST(GdsReader, MissingFileFailsFast) {
  TempDir dir;
  const std::string path = dir.Path("no_such.gds");
  const foundation::Result<GdsLibrary> library = ReadGdsFile(path);
  ASSERT_FALSE(library.ok());
  EXPECT_NE(library.status().message().find("cannot open GDS file " +
                                            path),
            std::string::npos);
}

}  // namespace
}  // namespace eos::io
