// timing-eco implementation: sizing and buffering over the ODB
// facade. Every check that can be made before touching the design
// is made first, so a refused edit changes nothing; atomicity for
// failures after that point is the ECO transaction's job (see the
// header).
#include "eos/eco/timing_eco.h"

#include <set>
#include <utility>
#include <vector>

namespace eos::eco {
namespace {

using foundation::ErrorCode;

Status NotFound(const std::string& what, const std::string& name) {
  return Status::Error(ErrorCode::kNotFound, what + " not found: " + name);
}

std::set<std::pair<std::string, std::pair<bool, bool>>> PinSet(
    const std::vector<db::OdbMasterPin>& pins) {
  std::set<std::pair<std::string, std::pair<bool, bool>>> set;
  for (const db::OdbMasterPin& pin : pins) {
    set.insert({pin.name, {pin.is_input, pin.is_output}});
  }
  return set;
}

}  // namespace

Status ResizeInstance(db::OdbDatabase& database, const std::string& instance,
                      const std::string& new_master) {
  db::OdbInstanceInfo info;
  if (!database.FindInstance(instance, &info)) {
    return NotFound("instance", instance);
  }
  if (info.master == new_master) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "instance " + instance + " is already a " +
                             new_master);
  }
  const std::vector<db::OdbMasterPin> target = database.MasterPins(new_master);
  if (target.empty()) {
    return NotFound("master", new_master);
  }
  if (PinSet(database.MasterPins(info.master)) != PinSet(target)) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "master " + new_master +
                             " does not carry the same signal pins as " +
                             info.master + "; a sizing swap must preserve "
                             "every connection by construction");
  }
  return database.SwapInstanceMaster(instance, new_master);
}

Status InsertBuffer(db::OdbDatabase& database, const std::string& net_name,
                    const std::string& buffer_master,
                    const std::string& new_instance_name,
                    const std::string& new_net_name) {
  if (!database.HasNet(net_name)) {
    return NotFound("net", net_name);
  }
  if (database.HasInstance(new_instance_name)) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "instance name already taken: " +
                             new_instance_name);
  }
  if (database.HasNet(new_net_name)) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "net name already taken: " + new_net_name);
  }
  // The buffer cell's shape: exactly one input and one output
  // signal pin (see the header on what shape does and does not
  // prove).
  const std::vector<db::OdbMasterPin> pins =
      database.MasterPins(buffer_master);
  if (pins.empty()) {
    return NotFound("master", buffer_master);
  }
  std::string input_pin;
  std::string output_pin;
  for (const db::OdbMasterPin& pin : pins) {
    if (pin.is_input && !pin.is_output) {
      if (!input_pin.empty()) {
        return Status::Error(
            ErrorCode::kInvalidArgument,
            "buffer master " + buffer_master +
                " has more than one input signal pin");
      }
      input_pin = pin.name;
    } else if (pin.is_output && !pin.is_input) {
      if (!output_pin.empty()) {
        return Status::Error(
            ErrorCode::kInvalidArgument,
            "buffer master " + buffer_master +
                " has more than one output signal pin");
      }
      output_pin = pin.name;
    } else {
      return Status::Error(ErrorCode::kInvalidArgument,
                           "buffer master " + buffer_master +
                               " has an inout signal pin " + pin.name +
                               "; v1 buffers are one input, one output");
    }
  }
  if (input_pin.empty() || output_pin.empty()) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "buffer master " + buffer_master +
                             " is not one input pin and one output pin");
  }

  // The net's shape: exactly one driver, an instance output pin;
  // at least one load, and every load an instance pin (ports are
  // out of v1 scope, per the header).
  const std::vector<db::OdbNetConnection> connections =
      database.NetConnections(net_name);
  const db::OdbNetConnection* driver = nullptr;
  std::vector<db::OdbNetConnection> loads;
  for (const db::OdbNetConnection& connection : connections) {
    if (connection.is_driver) {
      if (driver != nullptr) {
        return Status::Error(ErrorCode::kInvalidArgument,
                             "net " + net_name +
                                 " has more than one driver");
      }
      driver = &connection;
    } else {
      loads.push_back(connection);
    }
  }
  if (driver == nullptr) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "net " + net_name + " has no driver to buffer");
  }
  if (driver->is_port) {
    return Status::Error(ErrorCode::kUnsupported,
                         "net " + net_name +
                             " is driven by a top-level port; v1 "
                             "buffering covers instance-driven nets only");
  }
  if (loads.empty()) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "net " + net_name + " has no loads to buffer");
  }
  for (const db::OdbNetConnection& load : loads) {
    if (load.is_port) {
      return Status::Error(
          ErrorCode::kUnsupported,
          "net " + net_name +
              " drives a top-level port; v1 buffering moves instance "
              "loads only");
    }
  }
  db::OdbInstanceInfo driver_info;
  if (!database.FindInstance(driver->instance, &driver_info)) {
    return NotFound("driver instance", driver->instance);
  }

  // The edit itself. The new buffer sits at the driver instance's
  // location (the disclosed v1 placement simplification).
  Status status = database.CreateNet(new_net_name);
  if (!status.ok()) return status;
  status = database.CreateInstance(new_instance_name, buffer_master,
                                   driver_info.x, driver_info.y);
  if (!status.ok()) return status;
  status = database.ConnectInstancePin(new_instance_name, input_pin,
                                       net_name);
  if (!status.ok()) return status;
  status = database.ConnectInstancePin(new_instance_name, output_pin,
                                       new_net_name);
  if (!status.ok()) return status;
  for (const db::OdbNetConnection& load : loads) {
    status = database.DisconnectInstancePin(load.instance, load.pin);
    if (!status.ok()) return status;
    status = database.ConnectInstancePin(load.instance, load.pin,
                                         new_net_name);
    if (!status.ok()) return status;
  }
  return Status::Ok();
}

}  // namespace eos::eco
