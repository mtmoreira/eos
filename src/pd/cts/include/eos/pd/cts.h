// cts: the EOS-owned driver over the imported OpenROAD TritonCTS
// engine (L4 engines).
//
// The runner operates on the live OpenDB database: the caller hands
// it the database the session owns, and the engine mutates that
// database in place (clock buffer instances and clock nets). It
// never copies the design through a file. The imported engine code
// itself lives in this module's src/ directory; this header is the
// only surface other modules use.
//
// Clock selection: the caller names the clock net explicitly
// (options.clock_nets). The engine also reads SDC clocks from the
// STA session when no net is named, but the runner's private STA
// session holds no SDC, so in practice an empty clock list fails
// fast with "no clock" before the engine runs. The engine itself
// only warns on a design with no clock nets, so the fail-fast is
// the runner's: after the run, zero clock roots is a failure, not
// a report footnote.
//
// Liberty: a Liberty library is required for the engine's buffer
// characterization (TechChar errors without a buffer list, and a
// buffer list without Liberty cells cannot be characterized). The
// caller passes the library path (options.liberty_path); the
// runner loads it into its private STA session. Without it the
// runner fails fast instead of letting the engine die inside
// characterization.
//
// Placement precondition, defined precisely: the block must hold
// rows (a floorplanned design, as for place) and at least one
// placed instance. A design with rows but no placed instance has
// no sink positions for the tree builder, so it fails fast as
// "not placed".
//
// Report fields: clock buffers inserted, clock roots, and sinks
// come from the engine. Skew and insertion delay in nanoseconds
// are NOT reported by TritonCTS: its insertion delay is a length,
// not a time, and it computes no ns skew. The runner therefore
// reports both as unavailable with that reason, never invented;
// the eos::cts command appends post-CTS STA slack from the
// session timing service (estimated parasitics, as for place),
// or its unavailability with the reason, in the same payload.
//
// This header is a stable surface.
#ifndef EOS_PD_CTS_H_
#define EOS_PD_CTS_H_

#include <string>
#include <vector>

#include "eos/foundation/errors.h"

namespace odb {
class dbDatabase;
}  // namespace odb

namespace eos::pd {

using foundation::Status;

// Options for one CTS run. clock_nets names the clock nets to
// build trees for (net names, or instance-pin names the engine
// resolves to nets); an empty list means "detect from the STA
// session" and fails fast when the session holds no clock.
// buffer_list names the buffer cells the tree may use; empty
// means "infer from the Liberty library". root_buffer and
// sink_buffer pin the root and sink buffers; empty means the
// engine selects them from the buffer list.
struct CtsOptions {
  std::vector<std::string> clock_nets;
  std::vector<std::string> buffer_list;
  std::string root_buffer;
  std::string sink_buffer;
  std::string liberty_path;
};

// Measured outcome of a run. skew_ns_available and
// insertion_delay_ns_available are always false: see the header
// comment. buffers_inserted is the engine's count of created
// clock buffers; a tree that needs no buffering legitimately
// reports zero, so zero buffers is not a failure.
struct CtsReport {
  int clock_root_count = 0;
  int buffers_inserted = 0;
  int sink_count = 0;
  int clock_subnet_count = 0;
  bool skew_ns_available = false;
  bool insertion_delay_ns_available = false;
};

// Runs TritonCTS on the block of db, and fills report. Fails with
// kInvalidState when the database holds no technology data or no
// mapped design (no top block), when the block holds no rows (an
// un-floorplanned design), when no instance is placed (an
// unplaced design), when no Liberty library path is given, and
// when no clock is found (no clock net named, no SDC clock in
// the session, an unresolvable clock net name, or zero clock
// roots after the engine run). Engine logger errors surface as
// kInvalidState with the engine's message; the database is left
// as the engine left it on such a failure, which the caller
// reports rather than hides.
Status RunCts(odb::dbDatabase* db, const CtsOptions& options,
              CtsReport* report);

}  // namespace eos::pd

#endif  // EOS_PD_CTS_H_
