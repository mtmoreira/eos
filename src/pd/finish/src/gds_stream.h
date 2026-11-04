// gds_stream: EOS-owned GDSII stream writer, internal to the finish
// module (consumed only by src/finish.cc). See include/eos/pd/finish.h
// for the documented writer scope: top structure with one SREF per
// placed instance, one structure per used master with pin rectangles
// as boundaries, routed wires as PATH elements, vias as their own
// layer boxes, special-wire boxes (the power grid), and metal fills
// as boundaries. No text labels, no blockages.
//
// GDSII record layout follows the stream format: each record is a
// 2-byte big-endian length (including the 4 header bytes), a
// 1-byte record type, and a 1-byte data type, followed by the data.
// Integers are big-endian; reals are the GDS 8-byte float (base 16,
// biased exponent), not IEEE doubles.
#ifndef EOS_PD_FINISH_GDS_STREAM_H_
#define EOS_PD_FINISH_GDS_STREAM_H_

#include <string>

namespace odb {
class dbDatabase;
}  // namespace odb

namespace eos::pd::gds {

struct GdsStreamReport {
  int instance_count = 0;
  int structure_count = 0;
};

// Writes the block of db to path as GDSII. Returns false with error
// set when the file cannot be opened or the database holds no top
// block. The caller (the finish runner) owns the finish-marker and
// path checks; this writer only streams what the database holds.
bool WriteGdsStream(odb::dbDatabase* db, const std::string& path,
                    GdsStreamReport* report, std::string* error);

}  // namespace eos::pd::gds

#endif  // EOS_PD_FINISH_GDS_STREAM_H_
