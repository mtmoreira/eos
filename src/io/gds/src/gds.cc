// Copyright 2026 The EOS Authors
// SPDX-License-Identifier: Apache-2.0
//
// gds: the GDSII stream reader (L1 io). See eos/io/gds.h for
// the surface, the input domain, and the failure conditions.
// The parser walks the file record by record: a 4-byte header
// (total length including the header, record type, data type)
// plus the declared payload. The record and data type numbers
// below are the ones the EOS writer emits (src/pd/finish/src/
// gds_stream.cc); the reader's domain is that writer's output.
#include "eos/io/gds.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <utility>

namespace eos::io {
namespace {

// Record types the parser acts on. Any other type is skipped
// by its declared length.
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
constexpr std::uint8_t kNode = 21;
constexpr std::uint8_t kStrans = 26;
constexpr std::uint8_t kAngle = 28;
constexpr std::uint8_t kBox = 45;

// Data types, in the writer's numbering.
constexpr std::uint8_t kDataNone = 0;
constexpr std::uint8_t kDataInt16 = 1;
constexpr std::uint8_t kDataInt32 = 2;
constexpr std::uint8_t kDataReal8 = 3;
constexpr std::uint8_t kDataString = 6;

bool IsElementStart(std::uint8_t type) {
  return type == kBoundary || type == kPath || type == kSRef ||
         type == kARef || type == kText || type == kNode ||
         type == kBox;
}

std::int16_t ReadInt16(const std::uint8_t* p) {
  const std::uint16_t bits =
      static_cast<std::uint16_t>((p[0] << 8) | p[1]);
  return static_cast<std::int16_t>(bits);
}

std::int32_t ReadInt32(const std::uint8_t* p) {
  const std::uint32_t bits = (static_cast<std::uint32_t>(p[0]) << 24) |
                             (static_cast<std::uint32_t>(p[1]) << 16) |
                             (static_cast<std::uint32_t>(p[2]) << 8) |
                             static_cast<std::uint32_t>(p[3]);
  return static_cast<std::int32_t>(bits);
}

// The inverse of the writer's RealBytes: bit 63 sign, bits
// 62-56 exponent (base 16, bias 64), bits 55-0 a 56-bit
// fraction with the binary point before the first hex digit.
double DecodeReal8(const std::uint8_t* p) {
  std::uint64_t bits = 0;
  for (int i = 0; i < 8; ++i) {
    bits = (bits << 8) | p[i];
  }
  if (bits == 0) {
    return 0.0;
  }
  const bool negative = (bits >> 63) != 0;
  const int exponent =
      static_cast<int>((bits >> 56) & 0x7f) - 64;
  const std::uint64_t fraction = bits & 0x00ffffffffffffffULL;
  double value = static_cast<double>(fraction) /
                 72057594037927936.0;  // 2^56
  value *= std::pow(16.0, exponent);
  return negative ? -value : value;
}

std::string ReadString(const std::uint8_t* p, std::size_t size) {
  std::string out(reinterpret_cast<const char*>(p), size);
  while (!out.empty() && out.back() == '\0') {
    out.pop_back();
  }
  return out;
}

}  // namespace

foundation::Result<GdsLibrary> ReadGdsFile(const std::string& path) {
  using foundation::ErrorCode;
  using foundation::Status;

  std::ifstream in(path, std::ios::binary);
  if (!in.good()) {
    return Status::Error(ErrorCode::kIoError,
                         "cannot open GDS file " + path);
  }
  const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
  if (data.empty()) {
    return Status::Error(
        ErrorCode::kInvalidArgument,
        path + ": malformed GDS stream: empty file, no HEADER record");
  }

  auto malformed = [&path](const std::string& detail, std::size_t offset) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         path + ": malformed GDS stream: " + detail +
                             " at offset " + std::to_string(offset));
  };

  GdsLibrary library;
  std::size_t pos = 0;
  bool seen_header = false;
  bool seen_endlib = false;
  // Index of the structure being read, or -1 outside one.
  long current = -1;
  bool need_strname = false;

  // The element being accumulated, if any. kSkip covers the
  // unmodeled element types: their attribute records are
  // consumed and dropped until the element ends.
  enum class Element { kNone, kBoundary, kPath, kSref, kSkip };
  Element element = Element::kNone;
  GdsBoundary boundary;
  GdsPath path_element;
  GdsSref sref;
  bool element_has_xy = false;

  // Closes the pending element into the current structure.
  // Returns an error status for a truncated modeled element.
  auto finish_element = [&](std::size_t offset) -> Status {
    if (element == Element::kBoundary || element == Element::kPath ||
        element == Element::kSref) {
      if (!element_has_xy) {
        return malformed("truncated element (no XY record)", offset);
      }
      GdsStructure& structure =
          library.structures[static_cast<std::size_t>(current)];
      if (element == Element::kBoundary) {
        structure.boundaries.push_back(std::move(boundary));
        boundary = GdsBoundary();
      } else if (element == Element::kPath) {
        structure.paths.push_back(std::move(path_element));
        path_element = GdsPath();
      } else {
        structure.srefs.push_back(std::move(sref));
        sref = GdsSref();
      }
    }
    element = Element::kNone;
    element_has_xy = false;
    return Status::Ok();
  };

  while (!seen_endlib) {
    const std::size_t offset = pos;
    if (pos + 4 > data.size()) {
      return malformed("unexpected end of file (no ENDLIB record)",
                       offset);
    }
    const std::uint16_t length =
        static_cast<std::uint16_t>((data[pos] << 8) | data[pos + 1]);
    const std::uint8_t type = data[pos + 2];
    const std::uint8_t datatype = data[pos + 3];
    if (length < 4 || (length % 2) != 0) {
      return malformed("record length " + std::to_string(length) +
                           " is under 4 or odd",
                       offset);
    }
    if (pos + length > data.size()) {
      return malformed("record runs past the end of the file", offset);
    }
    const std::uint8_t* payload = data.data() + pos + 4;
    const std::size_t payload_size = length - 4;
    pos += length;

    if (!seen_header) {
      if (type != kHeader) {
        return malformed("first record is not HEADER", offset);
      }
      if (datatype != kDataInt16 || payload_size != 2) {
        return malformed("HEADER record has the wrong shape", offset);
      }
      seen_header = true;
      continue;
    }

    if (IsElementStart(type)) {
      if (current < 0) {
        return malformed("element outside any structure", offset);
      }
      if (need_strname) {
        return malformed("BGNSTR without STRNAME", offset);
      }
      if (Status status = finish_element(offset); !status.ok()) {
        return status;
      }
      element_has_xy = false;
      if (type == kBoundary) {
        element = Element::kBoundary;
        boundary = GdsBoundary();
      } else if (type == kPath) {
        element = Element::kPath;
        path_element = GdsPath();
      } else if (type == kSRef) {
        element = Element::kSref;
        sref = GdsSref();
      } else {
        element = Element::kSkip;
      }
      continue;
    }

    switch (type) {
      case kBgnLib:
      case kBgnStr: {
        if (datatype != kDataInt16 || payload_size != 24) {
          return malformed("timestamp record has the wrong shape",
                           offset);
        }
        if (type == kBgnStr) {
          if (current >= 0) {
            return malformed("BGNSTR inside a structure", offset);
          }
          if (Status status = finish_element(offset); !status.ok()) {
            return status;
          }
          library.structures.push_back(GdsStructure());
          current =
              static_cast<long>(library.structures.size()) - 1;
          need_strname = true;
        }
        break;
      }
      case kLibName: {
        if (datatype != kDataString) {
          return malformed("LIBNAME record has the wrong shape",
                           offset);
        }
        library.name = ReadString(payload, payload_size);
        break;
      }
      case kUnits: {
        if (datatype != kDataReal8 || payload_size != 16) {
          return malformed("UNITS record has the wrong shape", offset);
        }
        library.dbu_in_user_units = DecodeReal8(payload);
        library.dbu_in_meters = DecodeReal8(payload + 8);
        break;
      }
      case kStrName: {
        if (current < 0 || !need_strname) {
          return malformed("STRNAME outside a structure start",
                           offset);
        }
        if (datatype != kDataString) {
          return malformed("STRNAME record has the wrong shape",
                           offset);
        }
        library.structures[static_cast<std::size_t>(current)].name =
            ReadString(payload, payload_size);
        need_strname = false;
        break;
      }
      case kEndStr: {
        if (current < 0) {
          return malformed("ENDSTR outside any structure", offset);
        }
        if (need_strname) {
          return malformed("BGNSTR without STRNAME", offset);
        }
        if (Status status = finish_element(offset); !status.ok()) {
          return status;
        }
        current = -1;
        break;
      }
      case kEndLib: {
        if (current >= 0) {
          return malformed("ENDLIB inside a structure", offset);
        }
        if (Status status = finish_element(offset); !status.ok()) {
          return status;
        }
        seen_endlib = true;
        break;
      }
      case kEndEl: {
        if (Status status = finish_element(offset); !status.ok()) {
          return status;
        }
        break;
      }
      case kLayer: {
        if (element == Element::kBoundary ||
            element == Element::kPath) {
          if (datatype != kDataInt16 || payload_size != 2) {
            return malformed("LAYER record has the wrong shape",
                             offset);
          }
          if (element == Element::kBoundary) {
            boundary.layer = ReadInt16(payload);
          } else {
            path_element.layer = ReadInt16(payload);
          }
        }
        break;
      }
      case kDataType: {
        if (element == Element::kBoundary ||
            element == Element::kPath) {
          if (datatype != kDataInt16 || payload_size != 2) {
            return malformed("DATATYPE record has the wrong shape",
                             offset);
          }
          if (element == Element::kBoundary) {
            boundary.datatype = ReadInt16(payload);
          } else {
            path_element.datatype = ReadInt16(payload);
          }
        }
        break;
      }
      case kWidth: {
        if (element == Element::kPath) {
          if (datatype != kDataInt32 || payload_size != 4) {
            return malformed("WIDTH record has the wrong shape",
                             offset);
          }
          path_element.width = ReadInt32(payload);
          path_element.has_width = true;
        }
        break;
      }
      case kXy: {
        if (element == Element::kBoundary ||
            element == Element::kPath ||
            element == Element::kSref) {
          if (datatype != kDataInt32) {
            return malformed("non-integer XY payload", offset);
          }
          if (payload_size == 0 || (payload_size % 8) != 0) {
            return malformed("XY payload is not a whole number of "
                             "points",
                             offset);
          }
          const std::size_t count = payload_size / 8;
          if (element == Element::kSref && count != 1) {
            return malformed("SREF XY holds more than one point",
                             offset);
          }
          for (std::size_t i = 0; i < count; ++i) {
            const GdsPoint point{ReadInt32(payload + i * 8),
                                 ReadInt32(payload + i * 8 + 4)};
            if (element == Element::kBoundary) {
              boundary.points.push_back(point);
            } else if (element == Element::kPath) {
              path_element.points.push_back(point);
            } else {
              sref.x = point.x;
              sref.y = point.y;
            }
          }
          element_has_xy = true;
        }
        break;
      }
      case kSName: {
        if (element == Element::kSref) {
          if (datatype != kDataString) {
            return malformed("SNAME record has the wrong shape",
                             offset);
          }
          sref.target = ReadString(payload, payload_size);
        }
        break;
      }
      case kStrans: {
        if (element == Element::kSref) {
          if (datatype != kDataInt16 || payload_size != 2) {
            return malformed("STRANS record has the wrong shape",
                             offset);
          }
          sref.reflect =
              (static_cast<std::uint16_t>(ReadInt16(payload)) &
               0x8000) != 0;
        }
        break;
      }
      case kAngle: {
        if (element == Element::kSref) {
          if (datatype != kDataReal8 || payload_size != 8) {
            return malformed("ANGLE record has the wrong shape",
                             offset);
          }
          sref.angle_deg = DecodeReal8(payload);
          sref.has_angle = true;
        }
        break;
      }
      default: {
        // Unknown record types and records for skipped elements
        // are skipped by their declared length, as every GDS
        // reader must.
        break;
      }
    }
  }
  return library;
}

}  // namespace eos::io
