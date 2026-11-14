// timing-eco implementation: sizing and buffering over the ODB
// facade. Every check that can be made before touching the design
// is made first, so a refused edit changes nothing; atomicity for
// failures after that point is the ECO transaction's job (see the
// header).
#include "eos/eco/timing_eco.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "odb/db.h"

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

// The sizing family's naming split (see the header): the stem is
// the master name with its trailing drive digits, and one
// trailing underscore, stripped; drive is that number, or -1 when
// the name carries none.
struct DriveStem {
  std::string stem;
  long drive = -1;  // -1: the name carries no trailing number
};

DriveStem SplitDrive(const std::string& name) {
  std::size_t digits = 0;
  while (digits < name.size() &&
         name[name.size() - 1 - digits] >= '0' &&
         name[name.size() - 1 - digits] <= '9') {
    ++digits;
  }
  if (digits == 0) {
    return DriveStem{name, -1};
  }
  DriveStem split;
  split.stem = name.substr(0, name.size() - digits);
  if (!split.stem.empty() && split.stem.back() == '_') {
    split.stem.pop_back();
  }
  split.drive = std::stol(name.substr(name.size() - digits));
  return split;
}

std::string PinSignature(const std::vector<db::OdbMasterPin>& pins) {
  std::string signature;
  for (const db::OdbMasterPin& pin : pins) {
    signature += pin.name + (pin.is_input ? ":i" : "") +
                 (pin.is_output ? ":o" : "") + ";";
  }
  return signature;
}

// Buffer shape: exactly two signal pins, one input-only and one
// output-only. The same test the buffer-master derivation applies.
bool BufferShaped(const std::vector<db::OdbMasterPin>& pins) {
  if (pins.size() != 2) return false;
  std::size_t inputs = 0;
  std::size_t outputs = 0;
  for (const db::OdbMasterPin& pin : pins) {
    if (pin.is_input && !pin.is_output) ++inputs;
    if (pin.is_output && !pin.is_input) ++outputs;
  }
  return inputs == 1 && outputs == 1;
}

bool StemNamesBuffer(const std::string& master) {
  std::string lower = SplitDrive(master).stem;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return lower.find("buf") != std::string::npos;
}

struct CandidateSets {
  std::vector<std::vector<std::string>> families;
  std::vector<std::string> buffer_masters;
};

// One pass over the library deriving both candidate sets, so the
// two queries below and the closure's derivation can never drift.
CandidateSets DeriveCandidates(const db::OdbDatabase& database) {
  CandidateSets sets;
  std::map<std::string, std::vector<std::pair<long, std::string>>> groups;
  for (const std::string& master : database.MasterNames()) {
    const std::vector<db::OdbMasterPin> pins = database.MasterPins(master);
    if (pins.empty()) {
      continue;
    }
    const DriveStem split = SplitDrive(master);
    groups[split.stem + "|" + PinSignature(pins)].push_back(
        {split.drive, master});
    if (BufferShaped(pins) && StemNamesBuffer(master)) {
      sets.buffer_masters.push_back(master);
    }
  }
  for (auto& entry : groups) {
    std::vector<std::pair<long, std::string>>& members = entry.second;
    if (members.size() < 2) {
      continue;
    }
    std::sort(members.begin(), members.end());
    std::vector<std::string> family;
    for (const auto& member : members) {
      family.push_back(member.second);
    }
    sets.families.push_back(std::move(family));
  }
  std::sort(sets.buffer_masters.begin(), sets.buffer_masters.end(),
            [](const std::string& a, const std::string& b) {
              const long da = SplitDrive(a).drive;
              const long db = SplitDrive(b).drive;
              if (da != db) return da > db;
              return a < b;
            });
  return sets;
}

// The placement site grid of one master, through the facade's
// raw-database accessor (the header of odb.h reserves it for L4
// engine modules): the site the master's LEF names, in database
// units. False when the master or its site cannot be read, in
// which case the caller places unsnapped.
bool MasterSiteSize(const db::OdbDatabase& database,
                    const std::string& master, int* width, int* height) {
  odb::dbDatabase* raw = database.RawDatabase();
  if (raw == nullptr) return false;
  for (odb::dbLib* lib : raw->getLibs()) {
    odb::dbMaster* found = lib->findMaster(master.c_str());
    if (found == nullptr) continue;
    odb::dbSite* site = found->getSite();
    if (site == nullptr) return false;
    *width = static_cast<int>(site->getWidth());
    *height = static_cast<int>(site->getHeight());
    return *width > 0 && *height > 0;
  }
  return false;
}

long SnapToGrid(long value, int grid) {
  if (grid <= 0) return value;
  return std::lround(static_cast<double>(value) / grid) * grid;
}

}  // namespace

std::vector<std::vector<std::string>> SizingFamilies(
    const db::OdbDatabase& database) {
  return DeriveCandidates(database).families;
}

std::vector<std::string> BufferMasters(const db::OdbDatabase& database) {
  return DeriveCandidates(database).buffer_masters;
}

bool IsBufferMaster(const db::OdbDatabase& database,
                    const std::string& master) {
  return BufferShaped(database.MasterPins(master)) &&
         StemNamesBuffer(master);
}

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
  return InsertBuffer(database, net_name, buffer_master, new_instance_name,
                      new_net_name, 0.0);
}

Status InsertBuffer(db::OdbDatabase& database, const std::string& net_name,
                    const std::string& buffer_master,
                    const std::string& new_instance_name,
                    const std::string& new_net_name, double position) {
  if (!(position >= 0.0 && position <= 1.0)) {
    return Status::Error(ErrorCode::kOutOfRange,
                         "buffer position is outside [0, 1]: " +
                             std::to_string(position));
  }
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

  // The buffer's placement (the header's -position rule): at the
  // driver instance's location for position 0; above 0, the point
  // interpolated from the driver's location toward the centroid
  // of the loads' locations, snapped to the buffer master's site
  // grid. With no readable load location the buffer follows the
  // driver, as at position 0.
  int place_x = driver_info.x;
  int place_y = driver_info.y;
  if (position > 0.0) {
    double centroid_x = 0.0;
    double centroid_y = 0.0;
    std::size_t located = 0;
    for (const db::OdbNetConnection& load : loads) {
      db::OdbInstanceInfo load_info;
      if (!database.FindInstance(load.instance, &load_info)) continue;
      centroid_x += load_info.x;
      centroid_y += load_info.y;
      ++located;
    }
    if (located > 0) {
      centroid_x /= static_cast<double>(located);
      centroid_y /= static_cast<double>(located);
      long x = std::lround(driver_info.x +
                           position * (centroid_x - driver_info.x));
      long y = std::lround(driver_info.y +
                           position * (centroid_y - driver_info.y));
      int site_width = 0;
      int site_height = 0;
      if (MasterSiteSize(database, buffer_master, &site_width,
                         &site_height)) {
        x = SnapToGrid(x, site_width);
        y = SnapToGrid(y, site_height);
      }
      place_x = static_cast<int>(x);
      place_y = static_cast<int>(y);
    }
  }

  // The edit itself.
  Status status = database.CreateNet(new_net_name);
  if (!status.ok()) return status;
  status = database.CreateInstance(new_instance_name, buffer_master,
                                   place_x, place_y);
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

Status RemoveBuffer(db::OdbDatabase& database, const std::string& instance) {
  db::OdbInstanceInfo info;
  if (!database.FindInstance(instance, &info)) {
    return NotFound("instance", instance);
  }
  if (!IsBufferMaster(database, info.master)) {
    return Status::Error(
        ErrorCode::kInvalidArgument,
        "instance " + instance + " is a " + info.master +
            ", not a buffer: remove_buffer removes an instance whose "
            "master is buffer-shaped (exactly one input and one "
            "output signal pin) with \"buf\" in its name stem");
  }
  // The buffer's two pins, by the identity rule exactly one input
  // and one output.
  std::string input_pin;
  std::string output_pin;
  for (const db::OdbMasterPin& pin : database.MasterPins(info.master)) {
    if (pin.is_input && !pin.is_output) input_pin = pin.name;
    if (pin.is_output && !pin.is_input) output_pin = pin.name;
  }

  // Locate the two nets before touching anything: the net holding
  // the buffer's input pin, and the net its output pin drives.
  std::string in_net;
  std::string out_net;
  for (const std::string& net : database.NetNames()) {
    for (const db::OdbNetConnection& connection :
         database.NetConnections(net)) {
      if (connection.is_port || connection.instance != instance) {
        continue;
      }
      if (connection.pin == input_pin) in_net = net;
      if (connection.pin == output_pin) out_net = net;
    }
  }
  std::vector<db::OdbNetConnection> out_loads;
  if (!out_net.empty()) {
    for (const db::OdbNetConnection& connection :
         database.NetConnections(out_net)) {
      if (!connection.is_driver) out_loads.push_back(connection);
    }
  }
  if (!in_net.empty() && in_net == out_net) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "buffer " + instance +
                             " has its input and output on the same "
                             "net " +
                             in_net + "; there is no split to invert");
  }
  for (const db::OdbNetConnection& load : out_loads) {
    if (load.is_port) {
      return Status::Error(
          ErrorCode::kUnsupported,
          "net " + out_net +
              " drives a top-level port; v1 removal moves instance "
              "loads only");
    }
  }
  if (in_net.empty() && !out_loads.empty()) {
    return Status::Error(ErrorCode::kInvalidArgument,
                         "buffer " + instance +
                             " has no input connection: removing it "
                             "would orphan the loads on " +
                             out_net);
  }

  // The edit itself: the output net's loads move to the input
  // net, the buffer is deleted, and the output net, left with no
  // connections, is deleted with it.
  for (const db::OdbNetConnection& load : out_loads) {
    Status status =
        database.DisconnectInstancePin(load.instance, load.pin);
    if (!status.ok()) return status;
    status = database.ConnectInstancePin(load.instance, load.pin, in_net);
    if (!status.ok()) return status;
  }
  Status status = database.DeleteInstance(instance);
  if (!status.ok()) return status;
  if (!out_net.empty()) {
    status = database.DeleteNet(out_net);
    if (!status.ok()) return status;
  }
  return Status::Ok();
}

}  // namespace eos::eco
