// Copyright 2026 The EOS Authors
// SPDX-License-Identifier: Apache-2.0
//
// L1 GDSII reader: parses a GDSII stream file into model
// objects (libraries, structures, boundaries, paths, and
// structure references). It is the read side of the layout
// file surface whose write side lives in pd/finish (the
// EOS-owned stream writer there; no OpenDB GDS writer exists
// at the OpenDB pin). See the F8 finish-and-GDS design,
// section 2.1.
//
// The required input domain is the EOS writer's own output:
// HEADER, BGNLIB, LIBNAME, UNITS, BGNSTR/STRNAME, BOUNDARY,
// PATH, and SREF elements with LAYER, DATATYPE, WIDTH, XY,
// SNAME, STRANS, and ANGLE records, ENDSTR, ENDLIB. General
// GDS input is out of scope: AREF, TEXT, NODE, and BOX
// elements and property records are not modeled; a file
// using them still parses if its modeled elements are
// well-formed, with the unmodeled elements skipped by their
// declared length, as every GDS reader must. ENDEL records
// are accepted and ignored whether present or absent (the
// EOS writer omits them everywhere).
//
// Error behavior is fail-fast and total: a file that cannot
// be opened, and a stream malformed in a way that makes
// geometry ambiguous (a record length under 4 or odd, a
// record running past the end of the file, a truncated
// element, a BGNSTR without STRNAME, an element outside any
// structure, a non-integer XY payload), fails the whole
// read with a message naming the file and the record
// offset; no partial library is returned. Timestamps are
// parsed for validity of shape and otherwise discarded:
// nothing in the model carries them (the writer stamps
// wall-clock time, so two exports of one design are judged
// equal on census and geometry, never byte identity).

#ifndef EOS_IO_GDS_H_
#define EOS_IO_GDS_H_

#include <string>
#include <vector>

#include "eos/foundation/errors.h"

namespace eos::io {

struct GdsPoint {
  int x = 0;
  int y = 0;
};

struct GdsBoundary {
  int layer = 0;
  int datatype = 0;
  // As written: closed polygons repeat the first point last.
  std::vector<GdsPoint> points;
};

struct GdsPath {
  int layer = 0;
  int datatype = 0;
  bool has_width = false;
  int width = 0;
  std::vector<GdsPoint> points;
};

struct GdsSref {
  std::string target;
  int x = 0;
  int y = 0;
  bool reflect = false;     // STRANS reflection bit.
  bool has_angle = false;
  double angle_deg = 0.0;   // ANGLE, degrees.
};

struct GdsStructure {
  std::string name;
  std::vector<GdsBoundary> boundaries;
  std::vector<GdsPath> paths;
  std::vector<GdsSref> srefs;
};

struct GdsLibrary {
  std::string name;                 // LIBNAME.
  double dbu_in_user_units = 0.0;   // UNITS first real: one
                                    // database unit, in user
                                    // units.
  double dbu_in_meters = 0.0;       // UNITS second real: one
                                    // database unit, in meters.
  // File order. The top structure is the one no SREF names,
  // not a stored field.
  std::vector<GdsStructure> structures;
};

// Reads one GDSII stream file into model objects. See the
// header comment for the input domain and the failure
// conditions.
foundation::Result<GdsLibrary> ReadGdsFile(const std::string& path);

}  // namespace eos::io

#endif  // EOS_IO_GDS_H_
