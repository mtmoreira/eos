// timing-eco: the v1 timing ECO adapters (L4 engines).
//
// Two function-preserving edits, both meant to run inside an ECO
// transaction (src/eco/transactions), which supplies the
// checkpoint, the all-scene re-analysis and the commit or roll
// back decision. Called outside a transaction an adapter that
// fails midway can leave the edits it already made in place; the
// transaction is what makes an edit atomic.
//
// - ResizeInstance swaps one instance to another drive variant of
//   the same cell: the two masters must carry the same signal pins
//   (names and directions), so every connection carries over and
//   function is preserved by construction. Which variant is
//   stronger is the caller's knowledge (from the Liberty timing);
//   the adapter checks shape, not speed.
// - InsertBuffer splits one net: the original net keeps its driver
//   and gains the buffer's input; every load moves to a new net
//   driven by the buffer's output. Function is preserved by
//   construction when the buffer cell is a buffer (its output is
//   its input, per its Liberty function); the adapter checks the
//   cell's shape (exactly one input and one output signal pin),
//   not its logic function, and says so here.
//
// Disclosed simplifications (also for the phase exit report):
// - An inserted buffer is placed at its driver instance's
//   location. No placement legalization runs in v1; the designs in
//   scope have room at that location by construction, and a real
//   legalizer is physical-design work, not ECO work.
// - Buffering a net whose driver is a top-level port, or whose
//   loads include a top-level port, is refused (kUnsupported):
//   port connections live outside the instance-pin surface the
//   adapters move. Instance-driven, instance-loaded nets are the
//   v1 scope.
//
// This header is a stable surface.
#ifndef EOS_ECO_TIMING_ECO_H_
#define EOS_ECO_TIMING_ECO_H_

#include <string>

#include "eos/db/odb.h"
#include "eos/foundation/errors.h"

namespace eos::eco {

using foundation::Status;

// Swaps instance to the master new_master in database. Fails with
// kNotFound for an unknown instance or master, with
// kInvalidArgument when new_master is the instance's current
// master or the two masters' signal pin sets differ.
Status ResizeInstance(db::OdbDatabase& database, const std::string& instance,
                      const std::string& new_master);

// Inserts a buffer of master buffer_master on net net_name: the
// new instance new_instance_name is placed at the driver
// instance's location, its input connects to net_name, its output
// drives the new net new_net_name, and every load of net_name
// moves to new_net_name. Fails with kNotFound for an unknown net
// or buffer master, kInvalidArgument for a taken instance or net
// name or a buffer master without exactly one input and one output
// signal pin, and kUnsupported when the net's driver is not
// exactly one instance output pin or any load is a top-level port.
Status InsertBuffer(db::OdbDatabase& database, const std::string& net_name,
                    const std::string& buffer_master,
                    const std::string& new_instance_name,
                    const std::string& new_net_name);

}  // namespace eos::eco

#endif  // EOS_ECO_TIMING_ECO_H_
