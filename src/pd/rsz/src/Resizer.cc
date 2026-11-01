/////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2019, The Regents of the University of California
// All rights reserved.
//
// BSD 3-Clause License
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#include "rsz/Resizer.hh"
#include "sta/ContainerHelpers.hh"
#include <tcl.h>

#include "rsz/SteinerTree.hh"

#include "utl/Logger.h"

#include "sta/Report.hh"
#include "sta/FuncExpr.hh"
#include "sta/PortDirection.hh"
#include "sta/TimingRole.hh"
#include "sta/Units.hh"
#include "sta/Liberty.hh"
#include "sta/TimingArc.hh"
#include "sta/TimingModel.hh"
#include "sta/Network.hh"
#include "sta/Graph.hh"
#include "sta/ArcDelayCalc.hh"
#include "sta/GraphDelayCalc.hh"
#include "sta/Parasitics.hh"
#include "sta/Sdc.hh"
#include "sta/SearchPred.hh"
#include "sta/Bfs.hh"
#include "sta/Search.hh"
#include "sta/Path.hh"
#include "sta/PathExpanded.hh"
#include "sta/StaMain.hh"
#include "sta/Fuzzy.hh"
#include <format>

// multi-corner support
// http://vlsicad.eecs.umich.edu/BK/Slots/cache/dropzone.tamu.edu/~zhuoli/GSRC/fast_buffer_insertion.html

namespace rsz {

using std::abs;
using std::min;
using std::max;
using std::string;
using std::to_string;
using std::vector;
using std::map;
using std::pair;

using utl::RSZ;

using odb::dbInst;
using odb::dbPlacementStatus;
using odb::Rect;
using odb::dbOrientType;
using odb::dbMPin;
using odb::dbBox;
using odb::dbMasterType;

using sta::Level;
using sta::Network;
using sta::NetworkEdit;
using sta::NetPinIterator;
using sta::NetConnectedPinIterator;
using sta::InstancePinIterator;
using sta::LeafInstanceIterator;
using sta::LibertyLibraryIterator;
using sta::LibertyCellIterator;
using sta::TimingArcSet;
using sta::TimingArcSetSeq;
using sta::GateTimingModel;
using sta::TimingRole;
using sta::FuncExpr;
using sta::Term;
using sta::Port;
using sta::PinSeq;
using sta::NetIterator;
using sta::PinConnectedPinIterator;
using sta::FindNetDrvrLoads;;
using sta::VertexIterator;
using sta::VertexOutEdgeIterator;
using sta::Edge;
using sta::Search;
using sta::BfsBkwdIterator;
using sta::BfsFwdIterator;
using sta::BfsIndex;
using sta::Clock;
using sta::ClkTreeSearchPred;
using sta::SearchPred0;
using sta::Graph;
using sta::Mode;
using sta::PathExpanded;
using sta::INF;
using sta::fuzzyEqual;
using sta::fuzzyLess;
using sta::fuzzyLessEqual;
using sta::fuzzyGreater;
using sta::fuzzyGreaterEqual;
using sta::Unit;
using sta::ArcDelayCalc;

Resizer::Resizer() :
  StaState(),
  wire_signal_res_(0.0),
  wire_signal_cap_(0.0),
  wire_clk_res_(0.0),
  wire_clk_cap_(0.0),
  max_area_(0.0),
  sta_(nullptr),
  // R-1: v2.0 GlobalRouter default alpha; init() overwrites it.
  routing_alpha_(0.3),
  db_network_(nullptr),
  db_(nullptr),
  core_exists_(false),
  max_(MinMax::max()),
  have_estimated_parasitics_(false),
  target_load_map_(nullptr),
  level_drvr_vertices_valid_(false),
  tgt_slews_{0.0, 0.0},
  tgt_slew_scene_(nullptr),
  tgt_slew_dcalc_ap_(-1),
  unique_net_index_(1),
  unique_inst_index_(1),
  resize_count_(0),
  design_area_(0.0)
{
}

void
Resizer::init(Tcl_Interp *interp,
              Logger *logger,
              dbDatabase *db,
              dbSta *sta,
              float routing_alpha)
{
  logger_ = logger;
  db_ = db;
  block_ = nullptr;
  sta_ = sta;
  routing_alpha_ = routing_alpha;
  db_network_ = sta->getDbNetwork();
  copyState(sta);
  // Port adapter (P-29): 2026 STA InstanceSet/PinSet order by network id
  // through a comparator bound at construction, while v2.0 ordered by
  // pointer. Bare declarations bind a null network, so the first insert
  // segfaults inside the comparator (observed: journalMakeBuffer into
  // inserted_buffers_). Bind both member sets to the network here; the
  // sets are empty at init, so assignment only rebinds the comparator.
  inserted_buffers_ = InstanceSet(network_);
  resized_multi_output_insts_ = InstanceSet(network_);
  // R-2: the upstream SWIG init (Rsz_Init plus evalTclInit over
  // sta::rsz_tcl_inits) is dropped: Resizer.i is not imported, EOS
  // drives via the C++ API plus its own registry, and the EOS
  // session performs in-tree STA Tcl init itself where a Tcl
  // session is needed (dbsta M-3 precedent). The interp param is
  // retained for the session API; it is otherwise unused here.
  (void) interp;
}

void
Resizer::dcalcApParts(DcalcAPIndex dcalc_ap,
                      const Scene *&scene,
                      const MinMax *&min_max) const
{
  scene = sta_->scenes()[Scene::indexFromDcalcAP(dcalc_ap)];
  min_max = MinMax::find(dcalc_ap % MinMax::index_count);
}

DcalcAPIndex
Resizer::makeDcalcAp(const Scene *scene,
                     const MinMax *min_max) const
{
  return scene->index() * MinMax::index_count + min_max->index();
}

// 2021 Sta::isConstant(pin) was mode-independent. 2026 Sta::isConstant
// takes a Mode*; a pin is constant if it is constant in any mode.
bool
Resizer::isConstantPin(const Pin *pin)
{
  for (Mode *mode : sta_->modes())
    if (sta_->isConstant(pin, mode))
      return true;
  return false;
}

// 2021 Sta::isClock(pin) was mode-independent. 2026 Sta::isClock takes a
// Mode*; a pin is a clock pin if it is one in any mode.
bool
Resizer::isClockPin(const Pin *pin)
{
  for (Mode *mode : sta_->modes())
    if (sta_->isClock(pin, mode))
      return true;
  return false;
}

////////////////////////////////////////////////////////////////

float
Resizer::routingAlpha() const
{
  return routing_alpha_;
}

double
Resizer::coreArea() const
{
  return dbuToMeters(core_.dx()) * dbuToMeters(core_.dy());
}

double
Resizer::utilization()
{
  ensureBlock();
  ensureDesignArea();
  double core_area = coreArea();
  if (core_area > 0.0)
    return design_area_ / core_area;
  else
    return 1.0;
}

double
Resizer::maxArea() const
{
  return max_area_;
}

////////////////////////////////////////////////////////////////

class VertexLevelLess
{
public:
  VertexLevelLess(const Network *network);
  bool operator()(const Vertex *vertex1,
                  const Vertex *vertex2) const;

protected:
  const Network *network_;
};

VertexLevelLess::VertexLevelLess(const Network *network) :
  network_(network)
{
}

bool
VertexLevelLess::operator()(const Vertex *vertex1,
                            const Vertex *vertex2) const
{
  Level level1 = vertex1->level();
  Level level2 = vertex2->level();
  return (level1 < level2)
    || (level1 == level2
        // Break ties for stable results.
        && network_->pathName(vertex1->pin())
           < network_->pathName(vertex2->pin()));
}


////////////////////////////////////////////////////////////////

// block_ indicates core_, design_area_, db_network_ etc valid.
void
Resizer::ensureBlock()
{
  // block_ indicates core_, design_area_
  if (block_ == nullptr) {
    block_ = db_->getChip()->getBlock();
    core_ = block_->getCoreArea();
    core_exists_ = !(core_.xMin() == 0
                     && core_.xMax() == 0
                     && core_.yMin() == 0
                     && core_.yMax() == 0);
  }
}

void
Resizer::init()
{
  // Abbreviated copyState
  db_network_ = sta_->getDbNetwork();
  sta_->ensureLevelized();
  graph_ = sta_->graph();
  ensureBlock();
  ensureDesignArea();
  ensureLevelDrvrVertices();
  for (Mode *mode : sta_->modes())
    sta_->ensureClkNetwork(mode);
}

void
Resizer::removeBuffers()
{
  ensureBlock();
  db_network_ = sta_->getDbNetwork();
  // Disable incremental timing.
  graph_delay_calc_->delaysInvalid();
  search_->arrivalsInvalid();

  int remove_count = 0;
  for (dbInst *inst : block_->getInsts()) {
    LibertyCell *lib_cell = db_network_->libertyCell(inst);
    if (lib_cell && lib_cell->isBuffer()) {
      Instance *buffer = db_network_->dbToSta(inst);
      // Do not remove buffers connected to input/output ports
      // because verilog netlists use the net name for the port.
      if (!bufferBetweenPorts(buffer)) {
        removeBuffer(const_cast<Instance*>(buffer));
        remove_count++;
      }
    }
  }
  level_drvr_vertices_valid_ = false;
  logger_->info(RSZ, 26, "Removed {} buffers.", remove_count);
}

bool
Resizer::bufferBetweenPorts(Instance *buffer)
{
  LibertyCell *lib_cell = network_->libertyCell(buffer);
  LibertyPort *in_port, *out_port;
  lib_cell->bufferPorts(in_port, out_port);
  Pin *in_pin = db_network_->findPin(buffer, in_port);
  Pin *out_pin = db_network_->findPin(buffer, out_port);
  Net *in_net = db_network_->net(in_pin);
  Net *out_net = db_network_->net(out_pin);
  bool in_net_ports = hasPort(in_net);
  bool out_net_ports = hasPort(out_net);
  return in_net_ports && out_net_ports;
}

void
Resizer::removeBuffer(Instance *buffer)
{
  LibertyCell *lib_cell = network_->libertyCell(buffer);
  LibertyPort *in_port, *out_port;
  lib_cell->bufferPorts(in_port, out_port);
  Pin *in_pin = db_network_->findPin(buffer, in_port);
  Pin *out_pin = db_network_->findPin(buffer, out_port);
  Net *in_net = db_network_->net(in_pin);
  Net *out_net = db_network_->net(out_pin);
  bool out_net_ports = hasPort(out_net);
  Net *survivor, *removed;
  if (out_net_ports) {
    survivor = out_net;
    removed = in_net;
  }
  else {
    // default or out_net_ports
    // Default to in_net surviving so drivers (cached in dbNetwork)
    // do not change.
    survivor = in_net;
    removed = out_net;
  }

  sta_->disconnectPin(in_pin);
  sta_->disconnectPin(out_pin);
  sta_->deleteInstance(buffer);

  NetPinIterator *pin_iter = db_network_->pinIterator(removed);
  while (pin_iter->hasNext()) {
    const Pin *pin = pin_iter->next();
    Instance *pin_inst = db_network_->instance(pin);
    if (pin_inst != buffer) {
      Port *pin_port = db_network_->port(pin);
      sta_->disconnectPin(const_cast<Pin*>(pin));
      sta_->connectPin(pin_inst, pin_port, survivor);
    }
  }
  delete pin_iter;
  sta_->deleteNet(removed);
}

void
Resizer::ensureLevelDrvrVertices()
{
  if (!level_drvr_vertices_valid_) {
    level_drvr_vertices_.clear();
    VertexIterator vertex_iter(graph_);
    while (vertex_iter.hasNext()) {
      Vertex *vertex = vertex_iter.next();
      if (vertex->isDriver(network_))
        level_drvr_vertices_.push_back(vertex);
    }
    sort(level_drvr_vertices_, VertexLevelLess(network_));
    level_drvr_vertices_valid_ = true;
  }
}

////////////////////////////////////////////////////////////////

void
Resizer::resizePreamble()
{
  init();
  makeEquivCells();
  findBuffers();
  findTargetLoads();
}

static float
bufferDrive(const LibertyCell *buffer)
{
  LibertyPort *input, *output;
  buffer->bufferPorts(input, output);
  return output->driveResistance();
}

void
Resizer::findBuffers()
{
  LibertyLibraryIterator *lib_iter = network_->libertyLibraryIterator();
  while (lib_iter->hasNext()) {
    LibertyLibrary *lib = lib_iter->next();
    for (LibertyCell *buffer : *lib->buffers()) {
      if (!dontUse(buffer)
          && isLinkCell(buffer)) {
        buffer_cells_.push_back(buffer);
      }
    }
  }
  delete lib_iter;

  if (buffer_cells_.empty())
    logger_->error(RSZ, 22, "no buffers found.");

  sort(buffer_cells_, [] (const LibertyCell *buffer1,
                          const LibertyCell *buffer2) {
                        return bufferDrive(buffer1) > bufferDrive(buffer2);
                      });
  buffer_lowest_drive_ = buffer_cells_[0];
  buffer_med_drive_ = buffer_cells_[buffer_cells_.size() / 2];
  buffer_highest_drive_ = buffer_cells_[buffer_cells_.size() - 1];
}

bool
Resizer::isLinkCell(LibertyCell *cell)
{
  return network_->findLibertyCell(cell->name()) == cell;
}

////////////////////////////////////////////////////////////////

void
Resizer::bufferInputs()
{
  init();
  inserted_buffer_count_ = 0;
  InstancePinIterator *port_iter = network_->pinIterator(network_->topInstance());
  while (port_iter->hasNext()) {
    Pin *pin = port_iter->next();
    Vertex *vertex = graph_->pinDrvrVertex(pin);
    Net *net = network_->net(network_->term(pin));
    if (network_->direction(pin)->isInput()
        && !isConstantPin(vertex->pin())
        && !isClockPin(pin)
        && hasPins(net))
      // repair_design will resize to target slew.
      bufferInput(pin, buffer_lowest_drive_);
  }
  delete port_iter;
  ensureWireParasitics();

  if (inserted_buffer_count_ > 0) {
    logger_->info(RSZ, 27, "Inserted {} input buffers.", inserted_buffer_count_);
    level_drvr_vertices_valid_ = false;
  }
}
   
bool
Resizer::hasPins(Net *net)
{
  NetPinIterator *pin_iter = db_network_->pinIterator(net);
  bool has_pins = pin_iter->hasNext();
  delete pin_iter;
  return has_pins;
}

Instance *
Resizer::bufferInput(Pin *top_pin,
                     LibertyCell *buffer_cell)
{
  Term *term = db_network_->term(top_pin);
  Net *input_net = db_network_->net(term);
  LibertyPort *input, *output;
  buffer_cell->bufferPorts(input, output);
  string buffer_name = makeUniqueInstName("input");
  Instance *parent = db_network_->topInstance();
  Net *buffer_out = makeUniqueNet();
  Instance *buffer = db_network_->makeInstance(buffer_cell,
                                               buffer_name.c_str(),
                                               parent);
  if (buffer) {
    journalMakeBuffer(buffer);
    Point pin_loc = db_network_->location(top_pin);
    Point buf_loc = core_exists_ ? core_.closestPtInside(pin_loc) : pin_loc;
    setLocation(buffer, buf_loc);
    designAreaIncr(area(db_network_->cell(buffer_cell)));
    inserted_buffer_count_++;

    NetPinIterator *pin_iter = db_network_->pinIterator(input_net);
    while (pin_iter->hasNext()) {
      const Pin *pin = pin_iter->next();
      // Leave input port pin connected to input_net.
      if (pin != top_pin) {
        sta_->disconnectPin(const_cast<Pin*>(pin));
        Port *pin_port = db_network_->port(pin);
        sta_->connectPin(db_network_->instance(pin), pin_port, buffer_out);
      }
    }
    delete pin_iter;
    sta_->connectPin(buffer, input, input_net);
    sta_->connectPin(buffer, output, buffer_out);

    parasiticsInvalid(input_net);
    parasiticsInvalid(buffer_out);
  }
  return buffer;
}

void
Resizer::setLocation(Instance *inst,
                     Point pt)
{
  int x = pt.getX();
  int y = pt.getY();
  // Stay inside the lines.
  if (core_exists_) {
    Point in_core = core_.closestPtInside(Point(x, y));
    x = in_core.getX();
    y = in_core.getY();
  }

  dbInst *dinst = db_network_->staToDb(inst);
  dinst->setPlacementStatus(dbPlacementStatus::PLACED);
  dinst->setLocation(x, y);
}

void
Resizer::bufferOutputs()
{
  init();
  inserted_buffer_count_ = 0;
  InstancePinIterator *port_iter = network_->pinIterator(network_->topInstance());
  while (port_iter->hasNext()) {
    Pin *pin = port_iter->next();
    Vertex *vertex = graph_->pinLoadVertex(pin);
    Net *net = network_->net(network_->term(pin));
    if (network_->direction(pin)->isOutput()
        && net
        && !isConstantPin(vertex->pin())
        && hasPins(net))
      bufferOutput(pin, buffer_lowest_drive_);
  }
  delete port_iter;
  ensureWireParasitics();

  if (inserted_buffer_count_ > 0) {
    logger_->info(RSZ, 28, "Inserted {} output buffers.", inserted_buffer_count_);
    level_drvr_vertices_valid_ = false;
  }
}

void
Resizer::bufferOutput(Pin *top_pin,
                      LibertyCell *buffer_cell)
{
  NetworkEdit *network = networkEdit();
  Term *term = network_->term(top_pin);
  Net *output_net = network_->net(term);
  LibertyPort *input, *output;
  buffer_cell->bufferPorts(input, output);
  string buffer_name = makeUniqueInstName("output");
  Instance *parent = network->topInstance();
  Net *buffer_in = makeUniqueNet();
  Instance *buffer = network->makeInstance(buffer_cell,
                                           buffer_name.c_str(),
                                           parent);
  if (buffer) {
    journalMakeBuffer(buffer);
    setLocation(buffer, db_network_->location(top_pin));
    designAreaIncr(area(db_network_->cell(buffer_cell)));
    inserted_buffer_count_++;

    NetPinIterator *pin_iter = network->pinIterator(output_net);
    while (pin_iter->hasNext()) {
      const Pin *pin = pin_iter->next();
      if (pin != top_pin) {
        // Leave output port pin connected to output_net.
        sta_->disconnectPin(const_cast<Pin*>(pin));
        Port *pin_port = network->port(pin);
        sta_->connectPin(network->instance(pin), pin_port, buffer_in);
      }
    }
    delete pin_iter;
    sta_->connectPin(buffer, input, buffer_in);
    sta_->connectPin(buffer, output, output_net);

    parasiticsInvalid(buffer_in);
    parasiticsInvalid(output_net);
  }
}

////////////////////////////////////////////////////////////////

// Repair long wires, max slew, max capacitance, max fanout violations
// The whole enchilada.
// max_wire_length zero for none (meters)
void
Resizer::repairDesign(double max_wire_length, // max_wire_length zero for none (meters)
                      double max_slew_margin,
                      double max_cap_margin)
{
  int repair_count, slew_violations, cap_violations;
  int fanout_violations, length_violations;
  repairDesign(max_wire_length, max_slew_margin, max_cap_margin,
               repair_count, slew_violations, cap_violations,
               fanout_violations, length_violations);

  if (slew_violations > 0)
    logger_->info(RSZ, 34, "Found {} slew violations.", slew_violations);
  if (fanout_violations > 0)
    logger_->info(RSZ, 35, "Found {} fanout violations.", fanout_violations);
  if (cap_violations > 0)
    logger_->info(RSZ, 36, "Found {} capacitance violations.", cap_violations);
  if (length_violations > 0)
    logger_->info(RSZ, 37, "Found {} long wires.", length_violations);
  if (inserted_buffer_count_ > 0)
    logger_->info(RSZ, 38, "Inserted {} buffers in {} nets.",
                  inserted_buffer_count_,
                  repair_count);
  if (resize_count_ > 0)
    logger_->info(RSZ, 39, "Resized {} instances.", resize_count_);
}

void
Resizer::repairDesign(double max_wire_length, // zero for none (meters)
                      double max_slew_margin,
                      double max_cap_margin,
                      int &repair_count,
                      int &slew_violations,
                      int &cap_violations,
                      int &fanout_violations,
                      int &length_violations)
{
  repair_count = 0;
  slew_violations = 0;
  cap_violations = 0;
  fanout_violations = 0;
  length_violations = 0;
  inserted_buffer_count_ = 0;
  resize_count_ = 0;

  sta_->checkSlewsPreamble();
  sta_->checkCapacitancesPreamble(sta_->scenes());
  sta_->checkFanoutPreamble();

  int max_length = metersToDbu(max_wire_length);
  for (int i = level_drvr_vertices_.size() - 1; i >= 0; i--) {
    Vertex *drvr = level_drvr_vertices_[i];
    Pin *drvr_pin = drvr->pin();
    Net *net = network_->isTopLevelPort(drvr_pin)
      ? network_->net(network_->term(drvr_pin))
      : network_->net(drvr_pin);
    if (net
        && !isClockPin(drvr_pin)
        // Exclude tie hi/low cells and supply nets.
        && !isConstantPin(drvr->pin()))
      repairNet(net, drvr, max_slew_margin, max_cap_margin,
                true, true, true, max_length, true,
                repair_count, slew_violations, cap_violations,
                fanout_violations, length_violations);
  }
  ensureWireParasitics();

  if (inserted_buffer_count_ > 0)
    level_drvr_vertices_valid_ = false;
}

// repairDesign but restricted to clock network and
// no max_fanout/max_cap checks.
void
Resizer::repairClkNets(double max_wire_length) // max_wire_length zero for none (meters)
{
  init();
  // Need slews to resize inserted buffers.
  sta_->findDelays();

  inserted_buffer_count_ = 0;
  resize_count_ = 0;

  int repair_count = 0;
  int slew_violations = 0;
  int cap_violations = 0;
  int fanout_violations = 0;
  int length_violations = 0;
  int max_length = metersToDbu(max_wire_length);
  for (Mode *mode : sta_->modes())
  for (Clock *clk : mode->sdc()->clocks()) {
    for (const Pin *clk_pin : *sta_->pins(clk, mode)) {
      if (network_->isDriver(clk_pin)) {
        Net *net = network_->isTopLevelPort(clk_pin)
          ? network_->net(network_->term(clk_pin))
          : network_->net(clk_pin);
        Vertex *drvr = graph_->pinDrvrVertex(clk_pin);
        // Do not resize clock tree gates.
        repairNet(net, drvr, 0.0, 0.0,
                  false, false, false, max_length, false,
                  repair_count, slew_violations, cap_violations,
                  fanout_violations, length_violations);
      }
    }
  }
  ensureWireParasitics();
  if (length_violations > 0)
    logger_->info(RSZ, 47, "Found {} long wires.", length_violations);
  if (inserted_buffer_count_ > 0) {
    logger_->info(RSZ, 48, "Inserted {} buffers in {} nets.",
                  inserted_buffer_count_,
                  repair_count);
    level_drvr_vertices_valid_ = false;
  }
}

// for debugging
void
Resizer::repairNet(Net *net,
                   double max_wire_length, // meters
                   double max_slew_margin,
                   double max_cap_margin)
{
  init();

  sta_->checkSlewsPreamble();
  sta_->checkCapacitancesPreamble(sta_->scenes());
  sta_->checkFanoutPreamble();

  inserted_buffer_count_ = 0;
  resize_count_ = 0;
  resized_multi_output_insts_.clear();
  int repair_count = 0;
  int slew_violations = 0;
  int cap_violations = 0;
  int fanout_violations = 0;
  int length_violations = 0;
  int max_length = metersToDbu(max_wire_length);
  PinSet *drivers = network_->drivers(net);
  if (drivers && !drivers->empty()) {
    const Pin *drvr_pin = nullptr;
    for (const Pin *drvr_pin1 : *drivers) {
      drvr_pin = drvr_pin1;
      break;
    }
    Vertex *drvr = graph_->pinDrvrVertex(drvr_pin);
    repairNet(net, drvr, max_slew_margin, max_cap_margin,
              true, true, true, max_length, true,
              repair_count, slew_violations, cap_violations,
              fanout_violations, length_violations);
  }

  if (slew_violations > 0)
    logger_->info(RSZ, 51, "Found {} slew violations.", slew_violations);
  if (fanout_violations > 0)
    logger_->info(RSZ, 52, "Found {} fanout violations.", fanout_violations);
  if (cap_violations > 0)
    logger_->info(RSZ, 53, "Found {} capacitance violations.", cap_violations);
  if (length_violations > 0)
    logger_->info(RSZ, 54, "Found {} long wires.", length_violations);
  if (inserted_buffer_count_ > 0) {
    logger_->info(RSZ, 55, "Inserted {} buffers in {} nets.",
                  inserted_buffer_count_,
                  repair_count);
    level_drvr_vertices_valid_ = false;
  }
  if (resize_count_ > 0)
    logger_->info(RSZ, 56, "Resized {} instances.", resize_count_);
  if (resize_count_ > 0)
    logger_->info(RSZ, 57, "Resized {} instances.", resize_count_);
}

void
Resizer::repairNet(Net *net,
                   Vertex *drvr,
                   double max_slew_margin,
                   double max_cap_margin,
                   bool check_slew,
                   bool check_cap,
                   bool check_fanout,
                   int max_length, // dbu
                   bool resize_drvr,
                   int &repair_count,
                   int &slew_violations,
                   int &cap_violations,
                   int &fanout_violations,
                   int &length_violations)
{
  Pin *drvr_pin = drvr->pin();
  SteinerTree *tree = makeSteinerTree(drvr_pin, routingAlpha(), true,
                                      db_network_, logger_);
  if (tree) {
    debugPrint(logger_, RSZ, "repair_net", 1, "repair net {}",
               sdc_network_->pathName(drvr_pin));
    // Resize the driver to normalize slews before repairing limit violations.
    if (resize_drvr)
      resizeToTargetSlew(drvr_pin, true);
    ensureWireParasitic(drvr_pin, net);
    graph_delay_calc_->findDelays(drvr);

    if (checkLimits(drvr_pin, max_slew_margin, max_cap_margin,
                    check_slew, check_cap, check_fanout)
        && network_->isTopLevelPort(drvr_pin)) {
      // Input port driving net with slew/cap/fanout violataions.
      // Buffer the input so we some leverage to fix it.
      LibertyCell *input_buffer_cell = buffer_lowest_drive_;
      Instance *buffer = bufferInput(drvr_pin, input_buffer_cell);
      if (buffer) {
        // Switcheroo to repairting the input buffer output.
        LibertyPort *buffer_input_port, *buffer_output_port;
        input_buffer_cell->bufferPorts(buffer_input_port, buffer_output_port);
        drvr_pin = network_->findPin(buffer, buffer_output_port);
        net = network_->net(drvr_pin);
        resizeToTargetSlew(drvr_pin, false);
        ensureWireParasitic(drvr_pin, net);
        graph_delay_calc_->findDelays(drvr);
      }
    }

    double max_cap = INF;
    float max_fanout = INF;
    bool repair_slew = false;
    bool repair_cap = false;
    bool repair_fanout = false;
    bool repair_wire = false;
    bool using_repeater_cap_limit = false;
    const Scene *scene = sta_->cmdScene();
    if (check_cap) {
      float cap1, max_cap1, cap_slack1;
      const Scene *scene1 = nullptr;
      const RiseFall *tr1;
      sta_->checkCapacitance(drvr_pin, sta_->scenes(), max_,
                             cap1, max_cap1, cap_slack1, tr1, scene1);
      if (scene1) {
        max_cap1 *= (1.0 - max_cap_margin / 100.0);
        if (cap1 > max_cap1) {
          max_cap = max_cap1;
          scene = scene1;
          cap_violations++;
          repair_cap = true;
        }
      }
    }
    if (check_fanout) {
      // 2026 checkFanout takes a Mode*; keep the worst slack across modes.
      float fanout = 0.0, fanout_slack = INF;
      for (Mode *mode : sta_->modes()) {
        float fanout1, limit1, slack1;
        sta_->checkFanout(drvr_pin, mode, max_, fanout1, limit1, slack1);
        if (slack1 < fanout_slack) {
          fanout = fanout1;
          max_fanout = limit1;
          fanout_slack = slack1;
        }
      }
      if (fanout_slack < 0.0) {
        fanout_violations++;
        repair_fanout = true;
      }
    }
    int wire_length = findMaxSteinerDist(tree);
    if (max_length
        && wire_length > max_length) {
      length_violations++;
      repair_wire = true;
    }
    if (check_slew) {
      Slew slew1;
      float slew_slack1, max_slew1;
      const Scene *scene1 = nullptr;
      checkSlew(drvr_pin, max_slew_margin, slew1, max_slew1, slew_slack1, scene1);
      if (slew_slack1 < 0.0) {
        slew_violations++;
        LibertyPort *drvr_port = network_->libertyPort(drvr_pin);
        if (drvr_port) {
          // Find max load cap that corresponds to max_slew.
          double max_cap1 = findSlewLoadCap(drvr_port, max_slew1, scene1);
          max_cap = min(max_cap, max_cap1);
          scene = scene1;
          debugPrint(logger_, RSZ, "repair_net", 2, "slew={} max_slew={} max_cap={} corner={}",
                     delayAsString(slew1, 3, this),
                     delayAsString(max_slew1, 3, this),
                     units_->capacitanceUnit()->asString(max_cap1, 3),
                     scene1->name());
          repair_slew = true;
        }
      }
    }

    if (repair_slew
        || repair_cap
        || repair_fanout
        || repair_wire) {
      Point drvr_loc = db_network_->location(drvr->pin());
      debugPrint(logger_, RSZ, "repair_net", 1, "driver {} ({} {}) l={}",
                 sdc_network_->pathName(drvr_pin),
                 units_->distanceUnit()->asString(dbuToMeters(drvr_loc.getX()), 1),
                 units_->distanceUnit()->asString(dbuToMeters(drvr_loc.getY()), 1),
                 units_->distanceUnit()->asString(dbuToMeters(wire_length), 1));
      SteinerPt drvr_pt = tree->drvrPt(network_);
      int wire_length;
      float pin_cap, fanout;
      PinSeq load_pins;
      repairNet(tree, drvr_pt, SteinerTree::null_pt, net,
                max_cap, max_fanout, max_length, scene, 0,
                wire_length, pin_cap, fanout, load_pins);
      repair_count++;

      if (resize_drvr)
        resizeToTargetSlew(drvr_pin, true);
    }
    delete tree;
  }
}

bool
Resizer::checkLimits(const Pin *drvr_pin,
                     double max_slew_margin,
                     double max_cap_margin,
                     bool check_slew,
                     bool check_cap,
                     bool check_fanout)
{
  if (check_cap) {
    float cap1, max_cap1, cap_slack1;
    const Scene *scene1 = nullptr;
    const RiseFall *tr1;
    sta_->checkCapacitance(drvr_pin, sta_->scenes(), max_,
                           cap1, max_cap1, cap_slack1, tr1, scene1);
    max_cap1 *= (1.0 - max_cap_margin / 100.0);
    if (cap1 < max_cap1)
      return true;
  }
  if (check_fanout) {
    // 2026 checkFanout takes a Mode*; any mode below its limit violates.
    for (Mode *mode : sta_->modes()) {
      float fanout, fanout_slack, max_fanout;
      sta_->checkFanout(drvr_pin, mode, max_,
                        fanout, max_fanout, fanout_slack);
      if (fanout_slack < 0.0)
        return true;
    }
  }
  if (check_slew) {
    Slew slew1;
    float slew_slack1, max_slew1;
    const Scene *scene1 = nullptr;
    checkSlew(drvr_pin, max_slew_margin, slew1, max_slew1, slew_slack1, scene1);
    if (slew_slack1 < 0.0)
      return true;
  }
  return false;
}

void
Resizer::checkSlew(const Pin *drvr_pin,
                   double max_slew_margin,
                   // Return values.
                   Slew &slew,
                   float &limit,
                   float &slack,
                   const Scene *&scene)
{
  slack = INF;
  PinConnectedPinIterator *pin_iter = network_->connectedPinIterator(drvr_pin);
  while (pin_iter->hasNext()) {
    const Pin *pin = pin_iter->next();
    const Scene *scene1 = nullptr;
    const RiseFall *tr1;
    Slew slew1;
    float limit1, slack1;
    sta_->checkSlew(pin, sta_->scenes(), max_, false,
                    slew1, limit1, slack1, tr1, scene1);
    if (scene1) {
      limit1 *= (1.0 - max_slew_margin / 100.0);
      slack1 = limit1 - slew1;
      if (slack1 < slack) {
        slew = slew1;
        limit = limit1;
        slack = slack1;
        scene = scene1;
      }
    }
  }
  delete pin_iter;
}

// Find the output port load capacitance that results in slew.
double
Resizer::findSlewLoadCap(LibertyPort *drvr_port,
                         double slew,
                         const Scene *scene)
{
  DcalcAPIndex dcalc_ap = makeDcalcAp(scene, max_);
  // cap1 lower bound
  // cap2 upper bound
  double cap1 = 0.0;
  double cap2 = slew / drvr_port->driveResistance() * 2;
  double tol = .01; // 1%
  double diff1 = gateSlewDiff(drvr_port, cap1, slew, dcalc_ap);
  double diff2 = gateSlewDiff(drvr_port, cap2, slew, dcalc_ap);
  // binary search for diff = 0.
  while (abs(cap1 - cap2) > max(cap1, cap2) * tol) {
    if (diff2 < 0.0) {
      cap1 = cap2;
      diff1 = diff2;
      cap2 *= 2;
      diff2 = gateSlewDiff(drvr_port, cap2, slew, dcalc_ap);
    }
    else {
      double cap3 = (cap1 + cap2) / 2.0;
      double diff3 = gateSlewDiff(drvr_port, cap3, slew, dcalc_ap);
      if (diff3 < 0.0) {
        cap1 = cap3;
        diff1 = diff3;
      }
      else {
        cap2 = cap3;
        diff2 = diff3;
      }
    }
  }
  return cap1;
}

// objective function
double
Resizer::gateSlewDiff(LibertyPort *drvr_port,
                      double load_cap,
                      double slew,
                      DcalcAPIndex dcalc_ap)
{
  const Scene *scene;
  const MinMax *min_max;
  dcalcApParts(dcalc_ap, scene, min_max);
  const Pvt *pvt = sta_->operatingConditions(min_max, scene->mode()->sdc());
  ArcDelay delays[RiseFall::index_count];
  Slew slews[RiseFall::index_count];
  gateDelays(drvr_port, load_cap, dcalc_ap, delays, slews);
  // 2026 Delay has no max/operator-; compare and subtract mean values.
  float gate_slew = std::max(float(slews[RiseFall::riseIndex()]),
                             float(slews[RiseFall::fallIndex()]));
  return gate_slew - slew;
}

void
Resizer::repairNet(SteinerTree *tree,
                   SteinerPt pt,
                   SteinerPt prev_pt,
                   Net *net,
                   float max_cap,
                   float max_fanout,
                   int max_length, // dbu
                   const Scene *scene,
                   int level,
                   // Return values.
                   // Remaining parasiics after repeater insertion.
                   int &wire_length, // dbu
                   float &pin_cap,
                   float &fanout,
                   PinSeq &load_pins)
{
  Point pt_loc = tree->location(pt);
  int pt_x = pt_loc.getX();
  int pt_y = pt_loc.getY();
  debugPrint(logger_, RSZ, "repair_net", 2, "{:{}s}pt ({} {})",
             "", level,
             units_->distanceUnit()->asString(dbuToMeters(pt_x), 1),
             units_->distanceUnit()->asString(dbuToMeters(pt_y), 1));
  double wire_cap = wireSignalCapacitance(scene);
  SteinerPt left = tree->left(pt);
  int wire_length_left = 0;
  float pin_cap_left = 0.0;
  float fanout_left = 0.0;
  PinSeq loads_left;
  if (left != SteinerTree::null_pt)
    repairNet(tree, left, pt, net, max_cap, max_fanout, max_length, scene, level+1,
              wire_length_left, pin_cap_left, fanout_left, loads_left);
  SteinerPt right = tree->right(pt);
  int wire_length_right = 0;
  float pin_cap_right = 0.0;
  float fanout_right = 0.0;
  PinSeq loads_right;
  if (right != SteinerTree::null_pt)
    repairNet(tree, right, pt, net, max_cap, max_fanout, max_length, scene, level+1,
              wire_length_right, pin_cap_right, fanout_right, loads_right);
  debugPrint(logger_, RSZ, "repair_net", 3, "{:{}s}left l={} pin_cap={} fanout={}, right l={} pin_cap={} fanout={}",
             "", level,
             units_->distanceUnit()->asString(dbuToMeters(wire_length_left), 1),
             units_->capacitanceUnit()->asString(pin_cap_left, 3),
             fanout_left,
             units_->distanceUnit()->asString(dbuToMeters(wire_length_right), 1),
             units_->capacitanceUnit()->asString(pin_cap_right, 3),
             fanout_right);
  // Add a buffer to left or right branch to stay under the max cap/length/fanout.
  bool repeater_left = false;
  bool repeater_right = false;
  double cap_left = pin_cap_left + dbuToMeters(wire_length_left) * wire_cap;
  double cap_right = pin_cap_right + dbuToMeters(wire_length_right) * wire_cap;
  debugPrint(logger_, RSZ, "repair_net", 3, "{:{}s}cap_left={}, right_cap={}",
             "", level,
             units_->capacitanceUnit()->asString(cap_left, 3),
             units_->capacitanceUnit()->asString(cap_right, 3));
  bool cap_violation = (cap_left + cap_right) > max_cap;
  if (cap_violation) {
    debugPrint(logger_, RSZ, "repair_net", 3, "{:{}s}cap violation", "", level);
    if (cap_left > cap_right)
      repeater_left = true;
    else
      repeater_right = true;
  }
  bool length_violation = max_length > 0
    && (wire_length_left + wire_length_right) > max_length;
  if (length_violation) {
    debugPrint(logger_, RSZ, "repair_net", 3, "{:{}s}length violation", "", level);
    if (wire_length_left > wire_length_right)
      repeater_left = true;
    else
      repeater_right = true;
  }
  bool fanout_violation = max_fanout > 0
    && (fanout_left + fanout_right) > max_fanout;
  if (fanout_violation) {
    debugPrint(logger_, RSZ, "repair_net", 3, "{:{}s}fanout violation", "", level);
    if (fanout_left > fanout_right)
      repeater_left = true;
    else
      repeater_right = true;
  }

  if (repeater_left)
    makeRepeater("left", tree, pt, net, buffer_lowest_drive_, level,
                 wire_length_left, pin_cap_left, fanout_left, loads_left);
  if (repeater_right)
    makeRepeater("right", tree, pt, net, buffer_lowest_drive_, level,
                 wire_length_right, pin_cap_right, fanout_right, loads_right);

  wire_length = wire_length_left + wire_length_right;
  pin_cap = pin_cap_left + pin_cap_right;
  fanout = fanout_left + fanout_right;

  // Union left/right load pins.
  for (const Pin *load_pin : loads_left)
    load_pins.push_back(load_pin);
  for (const Pin *load_pin : loads_right)
    load_pins.push_back(load_pin);

  Net *buffer_out = nullptr;
  // Steiner pt pin is the net driver if prev_pt is null.
  if (prev_pt != SteinerTree::null_pt) {
    const PinSeq *pt_pins = tree->pins(pt);
    if (pt_pins) {
      for (const Pin *load_pin : *pt_pins) {
        Point load_loc = db_network_->location(load_pin);
        int load_dist = Point::manhattanDistance(load_loc, pt_loc);
        debugPrint(logger_, RSZ, "repair_net", 2, "{:{}s}load {} ({} {}) dist={}",
                   "", level,
                   sdc_network_->pathName(load_pin),
                   units_->distanceUnit()->asString(dbuToMeters(load_loc.getX()), 1),
                   units_->distanceUnit()->asString(dbuToMeters(load_loc.getY()), 1),
                   units_->distanceUnit()->asString(dbuToMeters(load_dist), 1));
        LibertyPort *load_port = network_->libertyPort(load_pin);
        if (load_port) {
          pin_cap += load_port->capacitance();
          fanout += portFanoutLoad(load_port);
        }
        else
          fanout += 1;
        load_pins.push_back(load_pin);
      }
    }

    Point prev_loc = tree->location(prev_pt);
    int length = Point::manhattanDistance(prev_loc, pt_loc);
    wire_length += length;
    // Back up from pt to prev_pt adding repeaters every max_length.
    int prev_x = prev_loc.getX();
    int prev_y = prev_loc.getY();
    debugPrint(logger_, RSZ, "repair_net", 3, "{:{}s}wl={} l={}",
               "", level,
               units_->distanceUnit()->asString(dbuToMeters(wire_length), 1),
               units_->distanceUnit()->asString(dbuToMeters(length), 1));
    while ((max_length > 0 && wire_length > max_length)
           || (wire_cap > 0.0
               && pin_cap < max_cap
               && (pin_cap+dbuToMeters(wire_length) * wire_cap) > max_cap)) {
      // Make the wire a bit shorter than necessary to allow for
      // offset from instance origin to pin and detailed placement movement.
      double length_margin = .05;
      // Distance from pt to repeater backward toward prev_pt.
      double buf_dist;
      if (max_length > 0 && wire_length > max_length) {
        buf_dist = length - (wire_length - max_length * (1.0 - length_margin));
      }
      else if (wire_cap > 0.0
               && (pin_cap + dbuToMeters(wire_length) * wire_cap) > max_cap) {
        int cap_length = metersToDbu((max_cap - pin_cap) / wire_cap);
        buf_dist = length - (wire_length - cap_length * (1.0 - length_margin));
      }
      else
        logger_->critical(RSZ, 23, "repairNet failure.");
      double dx = prev_x - pt_x;
      double dy = prev_y - pt_y;
      double d = (length == 0) ? 0.0 : buf_dist / length;
      int buf_x = pt_x + d * dx;
      int buf_y = pt_y + d * dy;
      makeRepeater("wire", buf_x, buf_y, net, buffer_lowest_drive_, level,
                   wire_length, pin_cap, fanout, load_pins);
      // Update for the next round.
      length -= buf_dist;
      wire_length = length;
      pt_x = buf_x;
      pt_y = buf_y;
      debugPrint(logger_, RSZ, "repair_net", 3, "{:{}s}wl={} l={}",
                 "", level,
                 units_->distanceUnit()->asString(dbuToMeters(wire_length), 1),
                 units_->distanceUnit()->asString(dbuToMeters(length), 1));
    }
  }
}

void
Resizer::makeRepeater(const char *where,
                      SteinerTree *tree,
                      SteinerPt pt,
                      Net *net,
                      LibertyCell *buffer_cell,
                      int level,
                      int &wire_length,
                      float &pin_cap,
                      float &fanout,
                      PinSeq &load_pins)
{
  Point pt_loc = tree->location(pt);
  makeRepeater(where, pt_loc.getX(), pt_loc.getY(), net, buffer_cell, level,
               wire_length, pin_cap, fanout, load_pins);
}

void
Resizer::makeRepeater(const char *where,
                      int x,
                      int y,
                      Net *net,
                      LibertyCell *buffer_cell,
                      int level,
                      int &wire_length,
                      float &pin_cap,
                      float &fanout,
                      PinSeq &load_pins)
{
  LibertyPort *buffer_input_port, *buffer_output_port;
  buffer_cell->bufferPorts(buffer_input_port, buffer_output_port);

  string buffer_name = makeUniqueInstName("repeater");
  debugPrint(logger_, RSZ, "repair_net", 2, "{:{}s}{} {} ({} {})",
             "", level,
             where,
             buffer_name.c_str(),
             units_->distanceUnit()->asString(dbuToMeters(x), 1),
             units_->distanceUnit()->asString(dbuToMeters(y), 1));

  // Inserting a buffer is complicated by the fact that verilog netlists
  // use the net name for input and output ports. This means the ports
  // cannot be moved to a different net.

  bool have_output_port_load = false;
  for (const Pin *pin : load_pins) {
    if (network_->isTopLevelPort(pin)
        && network_->direction(pin)->isAnyOutput()) {
      have_output_port_load = true;
      break;
    }
  }
  Net *in_net, *out_net;
  Instance *parent = db_network_->topInstance();

  // If the net is driven by an input port,
  // use the net as the repeater input net so the port stays connected to it.
  if (hasInputPort(net)
      || !have_output_port_load) {
    in_net = net;
    out_net = makeUniqueNet();
    // Copy signal type to new net.
    dbNet *out_net_db = db_network_->staToDb(out_net);
    dbNet *in_net_db = db_network_->staToDb(in_net);
    out_net_db->setSigType(in_net_db->getSigType());

    // Move load pins to out_net.
    for (const Pin *pin : load_pins) {
      Port *port = network_->port(pin);
      Instance *inst = network_->instance(pin);
      sta_->disconnectPin(const_cast<Pin*>(pin));
      sta_->connectPin(inst, port, out_net);
    }
  }
  else {
    // One of the loads is an output port.
    // Use the net as the repeater output net so the port stays connected to it.
    in_net = makeUniqueNet();
    out_net = net;
    // Copy signal type to new net.
    dbNet *out_net_db = db_network_->staToDb(out_net);
    dbNet *in_net_db = db_network_->staToDb(in_net);
    in_net_db->setSigType(out_net_db->getSigType());

    // Move non-repeater load pins to in_net.
    // Port adapter (P-29): bind the network (see init).
    PinSet load_pins1(network_);
    for (const Pin *pin : load_pins)
      load_pins1.insert(pin);

    NetPinIterator *pin_iter = network_->pinIterator(out_net);
    while (pin_iter->hasNext()) {
      const Pin *pin = pin_iter->next();
      if (load_pins1.count(pin) == 0) {
        Port *port = network_->port(pin);
        Instance *inst = network_->instance(pin);
        sta_->disconnectPin(const_cast<Pin*>(pin));
        sta_->connectPin(inst, port, in_net);
      }
    }
  }

  Instance *buffer = db_network_->makeInstance(buffer_cell,
                                               buffer_name.c_str(),
                                               parent);
  journalMakeBuffer(buffer);
  Point buf_loc(x, y);
  setLocation(buffer, buf_loc);
  designAreaIncr(area(db_network_->cell(buffer_cell)));
  inserted_buffer_count_++;

  sta_->connectPin(buffer, buffer_input_port, in_net);
  sta_->connectPin(buffer, buffer_output_port, out_net);

  parasiticsInvalid(in_net);
  parasiticsInvalid(out_net);

  // Resize repeater as we back up by levels.
  Pin *drvr_pin = network_->findPin(buffer, buffer_output_port);
  resizeToTargetSlew(drvr_pin, false);
  buffer_cell = network_->libertyCell(buffer);
  buffer_cell->bufferPorts(buffer_input_port, buffer_output_port);

  Pin *buf_in_pin = network_->findPin(buffer, buffer_input_port);
  load_pins.clear();
  load_pins.push_back(buf_in_pin);
  wire_length = 0;
  pin_cap = buffer_input_port->capacitance();
  fanout = portFanoutLoad(buffer_input_port);
}

bool
Resizer::hasInputPort(const Net *net)
{
  bool has_top_level_port = false;
  NetConnectedPinIterator *pin_iter = network_->connectedPinIterator(net);
  while (pin_iter->hasNext()) {
    const Pin *pin = pin_iter->next();
    if (network_->isTopLevelPort(pin)
        && network_->direction(pin)->isAnyInput()) {
      has_top_level_port = true;
      break;
    }
  }
  delete pin_iter;
  return has_top_level_port;
}

bool
Resizer::hasOutputPort(const Net *net)
{
  bool has_top_level_port = false;
  NetConnectedPinIterator *pin_iter = network_->connectedPinIterator(net);
  while (pin_iter->hasNext()) {
    const Pin *pin = pin_iter->next();
    if (network_->isTopLevelPort(pin)
        && network_->direction(pin)->isAnyOutput()) {
      has_top_level_port = true;
      break;
    }
  }
  delete pin_iter;
  return has_top_level_port;
}

bool
Resizer::hasPort(const Net *net)
{
  bool has_top_level_port = false;
  NetConnectedPinIterator *pin_iter = network_->connectedPinIterator(net);
  while (pin_iter->hasNext()) {
    const Pin *pin = pin_iter->next();
    if (network_->isTopLevelPort(pin)) {
      has_top_level_port = true;
      break;
    }
  }
  delete pin_iter;
  return has_top_level_port;
}

////////////////////////////////////////////////////////////////

void
Resizer::resizeToTargetSlew()
{
  resize_count_ = 0;
  resized_multi_output_insts_.clear();
  // Resize in reverse level order.
  for (int i = level_drvr_vertices_.size() - 1; i >= 0; i--) {
    Vertex *drvr = level_drvr_vertices_[i];
    Pin *drvr_pin = drvr->pin();
    Net *net = network_->net(drvr_pin);
    Instance *inst = network_->instance(drvr_pin);
    if (net
        && !isConstantPin(drvr->pin())
        && hasFanout(drvr)
        // Hands off the clock nets.
        && !isClockPin(drvr_pin)) {
      resizeToTargetSlew(drvr_pin, true);
      if (overMaxArea()) {
        logger_->error(RSZ, 24, "Max utilization reached.");
        break;
      }
    }
  }
  ensureWireParasitics();
  if (resize_count_ > 0)
    logger_->info(RSZ, 29, "Resized {} instances.", resize_count_);
}

bool
Resizer::hasFanout(Vertex *drvr)
{
  VertexOutEdgeIterator edge_iter(drvr, graph_);
  return edge_iter.hasNext();
}

void
Resizer::makeEquivCells()
{
  LibertyLibrarySeq libs;
  LibertyLibraryIterator *lib_iter = network_->libertyLibraryIterator();
  while (lib_iter->hasNext()) {
    LibertyLibrary *lib = lib_iter->next();
    // massive kludge until makeEquivCells is fixed to only incldue link cells
    LibertyCellIterator cell_iter(lib);
    if (cell_iter.hasNext()) {
      LibertyCell *cell = cell_iter.next();
      if (isLinkCell(cell))
        libs.push_back(lib);
    }
  }
  delete lib_iter;
  sta_->makeEquivCells(&libs, nullptr);
}

static float
targetLoadDist(float load_cap,
               float target_load)
{
  return abs(load_cap - target_load);
}

bool
Resizer::resizeToTargetSlew(const Pin *drvr_pin,
                            bool update_count)
{
  NetworkEdit *network = networkEdit();
  Instance *inst = network_->instance(drvr_pin);
  LibertyCell *cell = network_->libertyCell(inst);
  if (cell) {
    LibertyCellSeq *equiv_cells = sta_->equivCells(cell);
    if (equiv_cells) {
      bool revisiting_inst = false;
      if (hasMultipleOutputs(inst)) {
        if (resized_multi_output_insts_.count(inst) != 0)
          revisiting_inst = true;
        debugPrint(logger_, RSZ, "resizer", 2, "multiple outputs{}",
                   revisiting_inst ? " - revisit" : "");
        resized_multi_output_insts_.insert(inst);
      }
      bool is_buf_inv = cell->isBuffer() || cell->isInverter();
      ensureWireParasitic(drvr_pin);
      // Includes net parasitic capacitance.
      const Scene *tgt_scene;
      const MinMax *tgt_min_max;
      dcalcApParts(tgt_slew_dcalc_ap_, tgt_scene, tgt_min_max);
      float load_cap = graph_delay_calc_->loadCap(drvr_pin, tgt_scene, tgt_min_max);
      if (load_cap > 0.0) {
        LibertyCell *best_cell = cell;
        float target_load = (*target_load_map_)[cell];
        float best_load = target_load;
        float best_dist = targetLoadDist(load_cap, target_load);
        float best_delay = is_buf_inv
          ? bufferDelay(cell, load_cap, tgt_slew_dcalc_ap_)
          : 0.0;
        debugPrint(logger_, RSZ, "resizer", 3, "{} load cap {} dist={:.2e} delay={}",
                   sdc_network_->pathName(drvr_pin),
                   units_->capacitanceUnit()->asString(load_cap),
                   best_dist,
                   delayAsString(best_delay, 3, sta_));
        for (LibertyCell *target_cell : *equiv_cells) {
          if (!dontUse(target_cell)
              && isLinkCell(target_cell)) {
            float target_load = (*target_load_map_)[target_cell];
            float delay = is_buf_inv
              ? bufferDelay(target_cell, load_cap, tgt_slew_dcalc_ap_)
              : 0.0;
            float dist = targetLoadDist(load_cap, target_load);
            debugPrint(logger_, RSZ, "resizer", 3, " {} dist={:.2e} delay={}",
                       target_cell->name(),
                       dist,
                       delayAsString(delay, 3, sta_));
            if (is_buf_inv
                // Library may have "delay" buffers/inverters that are
                // functionally buffers/inverters but have additional
                // intrinsic delay. Accept worse target load matching if
                // delay is reduced to avoid using them.
                ? ((delay < best_delay
                    && dist < best_dist * 1.1)
                   || (dist < best_dist
                       && delay < best_delay * 1.1))
                : dist < best_dist
                // If the instance has multiple outputs (generally a register Q/QN)
                // only allow upsizing after the first pin is visited.
                && (!revisiting_inst
                    || target_load > best_load)) {
              best_cell = target_cell;
              best_dist = dist;
              best_load = target_load;
              best_delay = delay;
            }
          }
        }
        if (best_cell != cell) {
          debugPrint(logger_, RSZ, "resizer", 2, "{} {} -> {}",
                     sdc_network_->pathName(drvr_pin),
                     cell->name(),
                     best_cell->name());
          if (replaceCell(inst, best_cell, true)
              && !revisiting_inst
              && update_count)
            resize_count_++;
        }
      }
    }
  }
  return false;
}

// Replace LEF with LEF so ports stay aligned in instance.
bool
Resizer::replaceCell(Instance *inst,
                     LibertyCell *replacement,
                     bool journal)
{
  std::string replacement_name = replacement->name();
  dbMaster *replacement_master = db_->findMaster(replacement_name.c_str());
  if (replacement_master) {
    dbInst *dinst = db_network_->staToDb(inst);
    dbMaster *master = dinst->getMaster();
    designAreaIncr(-area(master));
    Cell *replacement_cell1 = db_network_->dbToSta(replacement_master);
    if (journal)
      journalInstReplaceCellBefore(inst);
    sta_->replaceCell(inst, replacement_cell1);
    designAreaIncr(area(replacement_master));

    // Delete estimated parasitics on all instance pins.
    // Input nets change pin cap, outputs change location (slightly).
    if (have_estimated_parasitics_) {
      InstancePinIterator *pin_iter = network_->pinIterator(inst);
      while (pin_iter->hasNext()) {
        const Pin *pin = pin_iter->next();
        const Net *net = network_->net(pin);
        if (net)
          parasiticsInvalid(net);
      }
      delete pin_iter;
    }
    return true;
  }
  return false;
}

bool
Resizer::hasMultipleOutputs(const Instance *inst)
{
  int output_count = 0;
  InstancePinIterator *pin_iter = network_->pinIterator(inst);
  while (pin_iter->hasNext()) {
    const Pin *pin = pin_iter->next();
    if (network_->direction(pin)->isAnyOutput()
        && network_->net(pin)) {
      output_count++;
      if (output_count > 1)
        return true;
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////

void
Resizer::resizeSlackPreamble()
{
  resizePreamble();
  // Save max_wire_length for multiple repairDesign calls.
  max_wire_length_ = findMaxWireLength();
  net_slack_map_.clear();
}

// Run repair design to find the slacks but save/restore all changes to the netlist.
void
Resizer::findResizeSlacks()
{
  journalBegin();
  estimateWireParasitics();
  int repair_count, slew_violations, cap_violations;
  int fanout_violations, length_violations;
  repairDesign(max_wire_length_, 0.0, 0.0,
               repair_count, slew_violations, cap_violations,
               fanout_violations, length_violations);
  findResizeSlacks1();
  journalRestore();
}
  
void
Resizer::findResizeSlacks1()
{
  // Use driver pin slacks rather than Sta::netSlack to save visiting
  // the net pins and min'ing the slack.
  NetSeq nets;
  for (int i = level_drvr_vertices_.size() - 1; i >= 0; i--) {
    Vertex *drvr = level_drvr_vertices_[i];
    Pin *drvr_pin = drvr->pin();
    Net *net = network_->isTopLevelPort(drvr_pin)
      ? network_->net(network_->term(drvr_pin))
      : network_->net(drvr_pin);
    if (net
        && !isConstantPin(drvr->pin())
        && !isClockPin(drvr_pin)) {
      net_slack_map_[net] = sta_->slack(drvr, max_);
      nets.push_back(net);
    }
  }

  // Find the nets with the worst slack.
  double worst_percent = .1;
  //  sort(nets.begin(), nets.end(). [&](const Net *net1,
  sort(nets, [this](const Net *net1,
                 const Net *net2)
             { return resizeNetSlack(net1) < resizeNetSlack(net2); });
  worst_slack_nets_.clear();
  for (int i = 0; i < nets.size() * worst_percent; i++)
    worst_slack_nets_.push_back(nets[i]);
}

NetSeq &
Resizer::resizeWorstSlackNets()
{
  return worst_slack_nets_;
}

vector<dbNet*>
Resizer::resizeWorstSlackDbNets()
{
  vector<dbNet*> nets;
  for (const Net* net : worst_slack_nets_)
    nets.push_back(db_network_->staToDb(net));
  return nets;
}

Slack
Resizer::resizeNetSlack(const Net *net)
{
  return net_slack_map_[net];
}

Slack
Resizer::resizeNetSlack(const dbNet *db_net)
{
  const Net *net = db_network_->dbToSta(db_net);
  return net_slack_map_[net];
}

////////////////////////////////////////////////////////////////

double
Resizer::area(Cell *cell)
{
  return area(db_network_->staToDb(cell));
}

double
Resizer::area(dbMaster *master)
{
  if (!master->isCoreAutoPlaceable()) {
    return 0;
  }
  return dbuToMeters(master->getWidth()) * dbuToMeters(master->getHeight());
}

double
Resizer::dbuToMeters(int dist) const
{
  int dbu = db_->getTech()->getDbUnitsPerMicron();
  return dist / (dbu * 1e+6);
}

int
Resizer::metersToDbu(double dist) const
{
  int dbu = db_->getTech()->getDbUnitsPerMicron();
  return dist * dbu * 1e+6;
}

void
Resizer::setMaxUtilization(double max_utilization)
{
  max_area_ = coreArea() * max_utilization;
}

bool
Resizer::overMaxArea()
{
  return max_area_
    && fuzzyGreaterEqual(design_area_, max_area_);
}

void
Resizer::setDontUse(LibertyCellSeq *dont_use)
{
  if (dont_use) {
    for (LibertyCell *cell : *dont_use)
      dont_use_.insert(cell);
  }
}

bool
Resizer::dontUse(LibertyCell *cell)
{
  return cell->dontUse()
    || dont_use_.count(cell) != 0;
}

////////////////////////////////////////////////////////////////

// Find a target slew for the libraries and then
// a target load for each cell that gives the target slew.
void
Resizer::findTargetLoads()
{
  // Find target slew across all buffers in the libraries.
  findBufferTargetSlews();
  if (target_load_map_ == nullptr)
    target_load_map_ = new CellTargetLoadMap;
  target_load_map_->clear();

  // Find target loads at the tgt_slew_scene (2026 STA has one library;
  // the 2021 corner-library selection has no counterpart).
  LibertyLibraryIterator *lib_iter = network_->libertyLibraryIterator();
  while (lib_iter->hasNext()) {
    LibertyLibrary *lib = lib_iter->next();
    LibertyCellIterator cell_iter(lib);
    while (cell_iter.hasNext()) {
      LibertyCell *cell = cell_iter.next();
      if (isLinkCell(cell))
        findTargetLoad(cell);
    }
  }
  delete lib_iter;
}

float
Resizer::targetLoadCap(LibertyCell *cell)
{
  float load_cap = 0.0;
  bool exists;
  findKeyValue(target_load_map_, cell, load_cap, exists);
  return load_cap;
}

void
Resizer::findTargetLoad(LibertyCell *cell)
{
  float target_load_sum = 0.0;
  int arc_count = 0;
  for (TimingArcSet *arc_set : cell->timingArcSets()) {
    const TimingRole *role = arc_set->role();
    if (!role->isTimingCheck()
        && role != TimingRole::tristateDisable()
        && role != TimingRole::tristateEnable()) {
      for (TimingArc *arc : arc_set->arcs()) {
        int in_rf_index = arc->fromEdge()->asRiseFall()->index();
        int out_rf_index = arc->toEdge()->asRiseFall()->index();
        float arc_target_load = findTargetLoad(cell, arc, 
                                               tgt_slews_[in_rf_index],
                                               tgt_slews_[out_rf_index]);
        debugPrint(logger_, RSZ, "target_load", 3, "{} {} -> {} {} target_load = {:.2e}",
                   cell->name(),
                   arc->from()->name(),
                   arc->to()->name(),
                   arc->toEdge()->asRiseFall()->name(),
                   arc_target_load);
        target_load_sum += arc_target_load;
        arc_count++;
      }
    }
  }
  float target_load = arc_count ? target_load_sum / arc_count : 0.0;
  (*target_load_map_)[cell] = target_load;

  debugPrint(logger_, RSZ, "target_load", 2, "{} target_load = {:.2e}",
             cell->name(),
             target_load);
}

// Find the load capacitance that will cause the output slew
// to be equal to out_slew.
float
Resizer::findTargetLoad(LibertyCell *cell,
                        TimingArc *arc,
                        Slew in_slew,
                        Slew out_slew)
{
  GateTimingModel *model = dynamic_cast<GateTimingModel*>(arc->model());
  if (model) {
    // load_cap1 lower bound
    // load_cap2 upper bound
    double load_cap1 = 0.0;
    double load_cap2 = 1.0e-12;  // 1pF
    double tol = .01; // 1%
    double diff1 = gateSlewDiff(cell, arc, model, in_slew, load_cap1, out_slew);
    if (diff1 > 0.0)
      // Zero load cap out_slew is higher than the target.
      return 0.0;
    double diff2 = gateSlewDiff(cell, arc, model, in_slew, load_cap2, out_slew);
    // binary search for diff = 0.
    while (abs(load_cap1 - load_cap2) > max(load_cap1, load_cap2) * tol) {
      if (diff2 < 0.0) {
        load_cap1 = load_cap2;
        diff1 = diff2;
        load_cap2 *= 2;
        diff2 = gateSlewDiff(cell, arc, model, in_slew, load_cap2, out_slew);
      }
      else {
        double load_cap3 = (load_cap1 + load_cap2) / 2.0;
        double diff3 = gateSlewDiff(cell, arc, model, in_slew, load_cap3, out_slew);
        if (diff3 < 0.0) {
          load_cap1 = load_cap3;
          diff1 = diff3;
        }
        else {
          load_cap2 = load_cap3;
          diff2 = diff3;
        }
      }
    }
    return load_cap1;
  }
  return 0.0;
}

// objective function
Slew
Resizer::gateSlewDiff(LibertyCell *cell,
                      TimingArc *arc,
                      GateTimingModel *model,
                      Slew in_slew,
                      float load_cap,
                      Slew out_slew)

{
  const Scene *scene;
  const MinMax *min_max;
  dcalcApParts(tgt_slew_dcalc_ap_, scene, min_max);
  const Pvt *pvt = sta_->operatingConditions(min_max, scene->mode()->sdc());
  // 2026 GateTimingModel::gateDelay takes float in/out slews and delays.
  // The 2021 call's trailing (0.0, false) args were always literal defaults;
  // the per-arc model object now carries that context.
  float arc_delay, arc_slew;
  model->gateDelay(pvt, in_slew, load_cap,
                   arc_delay, arc_slew);
  return Slew(arc_slew) - out_slew;
}

////////////////////////////////////////////////////////////////

Slew
Resizer::targetSlew(const RiseFall *rf)
{
  return tgt_slews_[rf->index()];
}

// Find target slew across all buffers in the libraries.
void
Resizer::findBufferTargetSlews()
{
  tgt_slews_ = {0.0};
  tgt_slew_scene_ = nullptr;

  for (const Scene *scene : sta_->scenes()) {
    // 2021 selected the corner library via libertyIndex + cornerCell; 2026
    // STA has one library, so the cell itself is used and the Pvt comes
    // from the scene's SDC operating conditions.
    const Pvt *pvt = sta_->operatingConditions(max_, scene->mode()->sdc());
    // Average slews across buffers at scene.
    Slew slews[RiseFall::index_count]{0.0};
    int counts[RiseFall::index_count]{0};
    for (LibertyCell *buffer : buffer_cells_)
      findBufferTargetSlews(buffer, pvt, slews, counts);
    // 2026 Delay has no operator/; average the mean values.
    Slew slew_rise = float(slews[RiseFall::riseIndex()]) / counts[RiseFall::riseIndex()];
    Slew slew_fall = float(slews[RiseFall::fallIndex()]) / counts[RiseFall::fallIndex()];
    // Use the target slews from the slowest scene,
    // and resize using that scene.
    if (slew_rise > tgt_slews_[RiseFall::riseIndex()]) {
      tgt_slews_[RiseFall::riseIndex()] = slew_rise;
      tgt_slews_[RiseFall::fallIndex()] = slew_fall;
      tgt_slew_scene_ = scene;
      tgt_slew_dcalc_ap_ = makeDcalcAp(scene, max_);
    }
  }

  debugPrint(logger_, RSZ, "target_load", 1, "target slew scene {} = {}/{}",
             tgt_slew_scene_->name(),
             delayAsString(tgt_slews_[RiseFall::riseIndex()], 3, sta_),
             delayAsString(tgt_slews_[RiseFall::fallIndex()], 3, sta_));
}

void
Resizer::findBufferTargetSlews(LibertyCell *buffer,
                               const Pvt *pvt,
                               // Return values.
                               Slew slews[],
                               int counts[])
{
  LibertyPort *input, *output;
  buffer->bufferPorts(input, output);
  for (TimingArcSet *arc_set : buffer->timingArcSets(input, output)) {
    {
      for (TimingArc *arc : arc_set->arcs()) {
        GateTimingModel *model = dynamic_cast<GateTimingModel*>(arc->model());
        const RiseFall *in_rf = arc->fromEdge()->asRiseFall();
        const RiseFall *out_rf = arc->toEdge()->asRiseFall();
        float in_cap = input->capacitance(in_rf, max_);
        float load_cap = in_cap * tgt_slew_load_cap_factor;
        ArcDelay arc_delay;
        Slew arc_slew;
        float arc_delay_f, arc_slew_f;
        model->gateDelay(pvt, 0.0, load_cap,
                         arc_delay_f, arc_slew_f);
        model->gateDelay(pvt, arc_slew_f, load_cap,
                         arc_delay_f, arc_slew_f);
        arc_delay = arc_delay_f;
        arc_slew = arc_slew_f;
        // 2026 Delay has no operator+=; accumulate the mean value.
        slews[out_rf->index()] = float(slews[out_rf->index()]) + arc_slew_f;
        counts[out_rf->index()]++;
      }
    }
  }
}

////////////////////////////////////////////////////////////////

// Repair tie hi/low net driver fanout by duplicating the
// tie hi/low instances for every pin connected to tie hi/low instances.
void
Resizer::repairTieFanout(LibertyPort *tie_port,
                         double separation, // meters
                         bool verbose)
{
  ensureBlock();
  ensureDesignArea();
  Instance *top_inst = network_->topInstance();
  LibertyCell *tie_cell = tie_port->libertyCell();
  InstanceSeq insts;
  findCellInstances(tie_cell, insts);
  int tie_count = 0;
  Instance *parent = db_network_->topInstance();
  int separation_dbu = metersToDbu(separation);
  for (const Instance *inst : insts) {
    Pin *drvr_pin = network_->findPin(inst, tie_port);
    if (drvr_pin) {
      std::string inst_name = network_->name(inst);
      Net *net = network_->net(drvr_pin);
      if (net) {
        NetConnectedPinIterator *pin_iter = network_->connectedPinIterator(net);
        while (pin_iter->hasNext()) {
          const Pin *load = pin_iter->next();
          if (load != drvr_pin) {
            // Make tie inst.
            Point tie_loc = tieLocation(load, separation_dbu);
            Instance *load_inst = network_->instance(load);
            string tie_name = makeUniqueInstName(inst_name.c_str(), true);
            Instance *tie = sta_->makeInstance(tie_name.c_str(),
                                               tie_cell, top_inst);
            setLocation(tie, tie_loc);

            // Make tie output net.
            Net *load_net = makeUniqueNet();

            // Connect tie inst output.
            sta_->connectPin(tie, tie_port, load_net);

            // Connect load to tie output net.
            sta_->disconnectPin(const_cast<Pin*>(load));
            Port *load_port = network_->port(load);
            sta_->connectPin(load_inst, load_port, load_net);

            designAreaIncr(area(db_network_->cell(tie_cell)));
            tie_count++;
          }
        }
        delete pin_iter;

        // Delete inst output net.
        Pin *tie_pin = network_->findPin(inst, tie_port);
        Net *tie_net = network_->net(tie_pin);
        sta_->deleteNet(tie_net);
        // Delete the tie instance.
        sta_->deleteInstance(const_cast<Instance*>(inst));
      }
    }
  }

  if (tie_count > 0) {
    logger_->info(RSZ, 42, "Inserted {} tie {} instances.",
                  tie_count,
                  tie_cell->name());
    level_drvr_vertices_valid_ = false;
  }
}

void
Resizer::findCellInstances(LibertyCell *cell,
                           // Return value.
                           InstanceSeq &insts)
{
  LeafInstanceIterator *inst_iter = network_->leafInstanceIterator();
  while (inst_iter->hasNext()) {
    Instance *inst = inst_iter->next();
    if (network_->libertyCell(inst) == cell)
      insts.push_back(inst);
  }
  delete inst_iter;
}

// Place the tie instance on the side of the load pin.
Point
Resizer::tieLocation(const Pin *load,
                     int separation)
{
  Point load_loc = db_network_->location(load);
  int load_x = load_loc.getX();
  int load_y = load_loc.getY();
  int tie_x = load_x;
  int tie_y = load_y;
  if (!network_->isTopLevelPort(load)) {
    dbInst *db_inst = db_network_->staToDb(network_->instance(load));
    dbBox *bbox = db_inst->getBBox();
    int left_dist = abs(load_x - bbox->xMin());
    int right_dist = abs(load_x - bbox->xMax());
    int bot_dist = abs(load_y - bbox->yMin());
    int top_dist = abs(load_y - bbox->yMax());
    if (left_dist < right_dist
        && left_dist < bot_dist
        && left_dist < top_dist)
      // left
      tie_x -= separation;
    if (right_dist < left_dist
        && right_dist < bot_dist
        && right_dist < top_dist)
      // right
      tie_x += separation;
    if (bot_dist < left_dist
        && bot_dist < right_dist
        && bot_dist < top_dist)
      // bot
      tie_y -= separation;
    if (top_dist < left_dist
        && top_dist < right_dist
        && top_dist < bot_dist)
      // top
      tie_y += separation;
  }
  return Point(tie_x, tie_y);
}

////////////////////////////////////////////////////////////////

void
Resizer::repairSetup(float slack_margin)
{
  inserted_buffer_count_ = 0;
  resize_count_ = 0;
  Slack worst_slack;
  Vertex *worst_vertex;
  sta_->worstSlack(max_, worst_slack, worst_vertex);
  debugPrint(logger_, RSZ, "repair_setup", 1, "worst_slack = {}",
             delayAsString(worst_slack, 3, sta_));
  Slack prev_worst_slack = -INF;
  int pass = 1;
  int decreasing_slack_passes = 0;
  while (fuzzyLess(worst_slack, slack_margin)) {
    Path worst_path = sta_->vertexWorstSlackPath(worst_vertex, max_);
    repairSetup(worst_path, worst_slack);
    // Port adapter (P-28): buffer insertion adds instances/pins that the
    // cached graph does not know (fresh pins have vertex id 0, so the
    // next rebufferBottomUp segfaults in vertexWorstRequiredPath on a
    // null load vertex; observed: rebuffer3/A, first buffer of the run).
    // Rebuild the graph from the current network before re-timing. The
    // SDC-preserving form keeps clocks and delays (networkChanged would
    // wipe SDC); the cached graph pointer and the level-driver-vertex
    // cache are refreshed with it, since both dangle after the rebuild.
    // No equivalent refresh was found in upstream v2.0; this is a port
    // adaptation, recorded as such.
    sta_->networkChangedNonSdc();
    graph_ = sta_->ensureGraph();
    level_drvr_vertices_valid_ = false;
    ensureWireParasitics();
    sta_->findRequireds();
    sta_->worstSlack(max_, worst_slack, worst_vertex);
    bool decreasing_slack = fuzzyLessEqual(worst_slack, prev_worst_slack);
    debugPrint(logger_, RSZ, "repair_setup", 1, "pass {} worst_slack = {} {}",
               pass,
               delayAsString(worst_slack, 3, sta_),
               decreasing_slack ? "v" : "^");
    if (decreasing_slack) {
      // Allow slack to increase a few passes to get out of local minima.
      // Do not update prev_worst_slack so it saves the high water mark.
      decreasing_slack_passes++;
      if (decreasing_slack_passes > repair_setup_decreasing_slack_passes_allowed_) {
        // Undo changes that reduced slack.
        journalRestore();
        debugPrint(logger_, RSZ, "repair_setup", 1,
                   "decreasing slack for {} passes restoring worst_slack {}",
                   repair_setup_decreasing_slack_passes_allowed_,
                   delayAsString(prev_worst_slack, 3, sta_));
        break;
      }
    }
    else {
      prev_worst_slack = worst_slack;
      decreasing_slack_passes = 0;
      // Progress, start journal so we can back up to here.
      journalBegin();
    }
    if (overMaxArea())
      break;
    pass++;
  }
  // Leave the parasitices up to date.
  ensureWireParasitics();

  if (inserted_buffer_count_ > 0)
    logger_->info(RSZ, 40, "Inserted {} buffers.", inserted_buffer_count_);
  if (resize_count_ > 0)
    logger_->info(RSZ, 41, "Resized {} instances.", resize_count_);
  if (fuzzyLess(worst_slack, slack_margin))
    logger_->warn(RSZ, 62, "Unable to repair all setup violations.");
  if (overMaxArea())
    logger_->error(RSZ, 25, "max utilization reached.");
}

// For testing.
void
Resizer::repairSetup(Pin *end_pin)
{
  inserted_buffer_count_ = 0;
  resize_count_ = 0;
  Vertex *vertex = graph_->pinLoadVertex(end_pin);
  Slack slack = sta_->slack(vertex, max_);
  Path path = sta_->vertexWorstSlackPath(vertex, max_);
  repairSetup(path, slack);
  // Leave the parasitices up to date.
  ensureWireParasitics();

  if (inserted_buffer_count_ > 0)
    logger_->info(RSZ, 30, "Inserted {} buffers.", inserted_buffer_count_);
  if (resize_count_ > 0)
    logger_->info(RSZ, 31, "Resized {} instances.", resize_count_);
}

void
Resizer::repairSetup(Path &path,
                     Slack path_slack)
{
  PathExpanded expanded(&path, sta_);
  if (expanded.size() > 1) {
    int path_length = expanded.size();
    vector<pair<int, Delay>> load_delays;
    int end_index = path_length - 1;
    int start_index = expanded.startIndex();
    const Path *prev_path = expanded.path(start_index - 1);
    DcalcAPIndex dcalc_ap = path.dcalcAnalysisPtIndex(sta_);
    for (int i = start_index; i < path_length; i++) {
      const Path *path = expanded.path(i);
      Vertex *path_vertex = path->vertex(sta_);
      const Pin *path_pin = path->pin(sta_);
      if (network_->isDriver(path_pin)
          && !network_->isTopLevelPort(path_pin)) {
        TimingArc *prev_arc = path->prevArc(sta_);
        Edge *prev_edge = path->prevEdge(sta_);
        // 2021 took the arc from the corner library via cornerArc; 2026 STA
        // has one library, so the arc itself is used. Graph::arcDelay takes
        // the DcalcAPIndex directly (2026), matching dcalc_ap->index().
        Delay load_delay = graph_->arcDelay(prev_edge, prev_arc, dcalc_ap)
          // Remove intrinsic delay to find load dependent delay.
          - prev_arc->intrinsicDelay();
        load_delays.push_back(pair(i, load_delay));
        debugPrint(logger_, RSZ, "repair_setup", 3, "{} load_delay = {}",
                   path_vertex->name(network_),
                   delayAsString(load_delay, 3, sta_));
      }
      prev_path = path;
    }

    sort(load_delays.begin(), load_delays.end(),
         [](pair<int, Delay> pair1,
            pair<int, Delay> pair2) {
           return pair1.second > pair2.second;
         });
    for (auto index_delay : load_delays) {
      int drvr_index = index_delay.first;
      Delay load_delay = index_delay.second;
      const Path *drvr_path = expanded.path(drvr_index);
      Vertex *drvr_vertex = drvr_path->vertex(sta_);
      Pin *drvr_pin = drvr_vertex->pin();
      const Path *load_path = expanded.path(drvr_index + 1);
      Vertex *load_vertex = load_path->vertex(sta_);
      Pin *load_pin = load_vertex->pin();
      int fanout = this->fanout(drvr_vertex);
      debugPrint(logger_, RSZ, "repair_setup", 2, "{} fanout = {}",
                 network_->pathName(drvr_pin),
                 fanout);
      if (fanout > 1
          // Rebuffer blows up on large fanout nets.
          && fanout < rebuffer_max_fanout_) {
        int count_before = inserted_buffer_count_;
        rebuffer(drvr_pin);
        int insert_count = inserted_buffer_count_ - count_before;
        debugPrint(logger_, RSZ, "repair_setup", 2, "rebuffer {} inserted {}",
                   network_->pathName(drvr_pin),
                   insert_count);
        if (insert_count > 0)
          break;
      }
      // Don't split loads on low fanout nets.
      if (fanout > split_load_min_fanout_) {
        // Divide and conquer.
        debugPrint(logger_, RSZ, "repair_setup", 2, "split loads {} -> {}",
                   network_->pathName(drvr_pin),
                   network_->pathName(load_pin));
        splitLoads(drvr_path, path_slack);
        break;
      }
      LibertyPort *drvr_port = network_->libertyPort(drvr_pin);
      const Scene *dcalc_scene;
      const MinMax *dcalc_min_max;
      dcalcApParts(dcalc_ap, dcalc_scene, dcalc_min_max);
      float load_cap = graph_delay_calc_->loadCap(drvr_pin, dcalc_scene, dcalc_min_max);
      int in_index = drvr_index - 1;
      const Path *in_path = expanded.path(in_index);
      Pin *in_pin = in_path->pin(sta_);
      LibertyPort *in_port = network_->libertyPort(in_pin);

      float prev_drive;
      if (drvr_index >= 2) {
        int prev_drvr_index = drvr_index - 2;
        const Path *prev_drvr_path = expanded.path(prev_drvr_index);
        Pin *prev_drvr_pin = prev_drvr_path->pin(sta_);
        prev_drive = 0.0;
        LibertyPort *prev_drvr_port = network_->libertyPort(prev_drvr_pin);
        if (prev_drvr_port) {
          prev_drive = prev_drvr_port->driveResistance();
        }
      }
      else
        prev_drive = 0.0;
      debugPrint(logger_, RSZ, "repair_setup", 2, "resize {}",
                 network_->pathName(drvr_pin));
      LibertyCell *upsize = upsizeCell(in_port, drvr_port, load_cap,
                                       prev_drive, dcalc_ap);
      if (upsize) {
        Instance *drvr = network_->instance(drvr_pin);
        debugPrint(logger_, RSZ, "repair_setup", 2, "resize {} {} -> {}",
                   network_->pathName(drvr_pin),
                   drvr_port->libertyCell()->name(),
                   upsize->name());
        if (replaceCell(drvr, upsize, true))
          resize_count_++;
        break;
      }
    }
  }
}

bool
Resizer::isDisabledConstraint(Vertex *vertex)
{
  // 2026: disabled-constraint state lives in per-mode SDC (Sdc::
  // isDisabledConstraint). Conservative: disabled in any mode counts as
  // disabled (the 2021 Vertex flag was mode-independent).
  for (const Scene *scene : sta_->scenes()) {
    const Mode *mode = scene->mode();
    if (mode && mode->sdc()->isDisabledConstraint(vertex->pin()))
      return true;
  }
  return false;
}

void
Resizer::splitLoads(const Path *drvr_path,
                    Slack drvr_slack)
{
  Vertex *drvr_vertex = drvr_path->vertex(sta_);
  const RiseFall *rf = drvr_path->transition(sta_);
  // Sort fanouts of the drvr on the critical path by slack margin
  // wrt the critical path slack.
  vector<pair<Vertex*, Slack>> fanout_slacks;
  VertexOutEdgeIterator edge_iter(drvr_vertex, graph_);
  while (edge_iter.hasNext()) {
    Edge *edge = edge_iter.next();
    Vertex *fanout_vertex = edge->to(graph_);
    Slack fanout_slack = sta_->slack(fanout_vertex, rf, max_);
    Slack slack_margin = fanout_slack - drvr_slack;
    debugPrint(logger_, RSZ, "repair_setup", 3, " fanin {} slack_margin = {}",
               network_->pathName(fanout_vertex->pin()),
               delayAsString(slack_margin, 3, sta_));
    fanout_slacks.push_back(pair<Vertex*, Slack>(fanout_vertex, slack_margin));
  }

  sort(fanout_slacks.begin(), fanout_slacks.end(),
       [](pair<Vertex*, Slack> pair1,
          pair<Vertex*, Slack> pair2) {
         return pair1.second > pair2.second;
       });

  Pin *drvr_pin = drvr_vertex->pin();
  Net *net = network_->net(drvr_pin);
  Instance *drvr = network_->instance(drvr_pin);
  LibertyPort *drvr_port = network_->libertyPort(drvr_pin);
  LibertyCell *drvr_cell = network_->libertyCell(drvr);

  string buffer_name = makeUniqueInstName("split");
  Instance *parent = db_network_->topInstance();
  LibertyCell *buffer_cell = buffer_lowest_drive_;
  Instance *buffer = db_network_->makeInstance(buffer_cell,
                                               buffer_name.c_str(),
                                               parent);
  journalMakeBuffer(buffer);
  inserted_buffer_count_++;
  designAreaIncr(area(db_network_->cell(buffer_cell)));

  Net *in_net = makeUniqueNet();
  Net *out_net = makeUniqueNet();
  LibertyPort *input, *output;
  buffer_cell->bufferPorts(input, output);
  Point drvr_loc = db_network_->location(drvr_pin);
  setLocation(buffer, drvr_loc);

  // Split the loads with extra slack to an inserted buffer.
  // before
  // drvr_pin -> net -> load_pins
  // after
  // drvr_pin -> net -> load_pins with low slack
  //                 -> buffer_in -> net -> rest of loads
  sta_->connectPin(buffer, input, net);
  parasiticsInvalid(net);
  sta_->connectPin(buffer, output, out_net);
  int split_index = fanout_slacks.size() / 2;
  for (int i = 0; i < split_index; i++) {
    pair<Vertex*, Slack> fanout_slack = fanout_slacks[i];
    Vertex *load_vertex = fanout_slack.first;
    Pin *load_pin = load_vertex->pin();
    // Leave ports connected to original net so verilog port names are preserved.
    if (!network_->isTopLevelPort(load_pin)) {
      LibertyPort *load_port = network_->libertyPort(load_pin);
      Instance *load = network_->instance(load_pin);

      sta_->disconnectPin(const_cast<Pin*>(load_pin));
      sta_->connectPin(load, load_port, out_net);
    }
  }
  Pin *buffer_out_pin = network_->findPin(buffer, output);
  resizeToTargetSlew(buffer_out_pin, false);
}

LibertyCell *
Resizer::upsizeCell(LibertyPort *in_port,
                    LibertyPort *drvr_port,
                    float load_cap,
                    float prev_drive,
                    DcalcAPIndex dcalc_ap)
{
  // 2021 selected the corner library's ports/cells via libertyIndex +
  // cornerPort/cornerCell. 2026 STA has one library; ports and cells are
  // used directly, with pin capacitance taken at the dcalc ap's min_max
  // (same mapping as pinCapacitance).
  const Scene *dcalc_scene;
  const MinMax *dcalc_min_max;
  dcalcApParts(dcalc_ap, dcalc_scene, dcalc_min_max);
  LibertyCell *cell = drvr_port->libertyCell();
  LibertyCellSeq *equiv_cells = sta_->equivCells(cell);
  if (equiv_cells) {
    std::string in_port_name = in_port->name();
    std::string drvr_port_name = drvr_port->name();
    std::sort(equiv_cells->begin(), equiv_cells->end(),
         [=] (const LibertyCell *cell1,
              const LibertyCell *cell2) {
           LibertyPort *port1 = cell1->findLibertyPort(drvr_port_name);
           LibertyPort *port2 = cell2->findLibertyPort(drvr_port_name);
           return port1->driveResistance() > port2->driveResistance();
         });
    float drive = drvr_port->driveResistance();
    float delay = gateDelay(drvr_port, load_cap, tgt_slew_dcalc_ap_)
      + prev_drive * in_port->capacitance(dcalc_min_max);
    for (LibertyCell *equiv : *equiv_cells) {
      LibertyPort *equiv_drvr = equiv->findLibertyPort(drvr_port_name);
      LibertyPort *equiv_input = equiv->findLibertyPort(in_port_name);
      float equiv_drive = equiv_drvr->driveResistance();
      // Include delay of previous driver into equiv gate.
      float equiv_delay = gateDelay(equiv_drvr, load_cap, dcalc_ap)
        + prev_drive * equiv_input->capacitance(dcalc_min_max);
      if (!dontUse(equiv)
          && equiv_drive < drive
          && equiv_delay < delay)
        return equiv;
    }
  }
  return nullptr;
}

////////////////////////////////////////////////////////////////

void
Resizer::repairHold(float slack_margin,
                    bool allow_setup_violations,
                    // Max buffer count as percent of design instance count.
                    float max_buffer_percent)
{
  init();
  LibertyCell *buffer_cell = findHoldBuffer();
  Unit *time_unit = units_->timeUnit();
  sta_->findRequireds();
  VertexSet ends = sta_->search()->endpoints();
  int max_buffer_count = max_buffer_percent * network_->instanceCount();
  repairHold(&ends, buffer_cell, slack_margin,
             allow_setup_violations, max_buffer_count);

  // Leave the parasitices up to date.
  ensureWireParasitics();
}

// For testing/debug.
void
Resizer::repairHold(Pin *end_pin,
                    LibertyCell *buffer_cell,
                    float slack_margin,
                    bool allow_setup_violations,
                    float max_buffer_percent)
{
  Vertex *end = graph_->pinLoadVertex(end_pin);
  VertexSet ends(graph_);
  ends.insert(end);

  init();
  sta_->findRequireds();
  int max_buffer_count = max_buffer_percent * network_->instanceCount();
  repairHold(&ends, buffer_cell, slack_margin, allow_setup_violations,
             max_buffer_count);
  // Leave the parasitices up to date.
  ensureWireParasitics();
}

// Find the buffer with the most delay in the fastest corner.
LibertyCell *
Resizer::findHoldBuffer()
{
  LibertyCell *max_buffer = nullptr;
  float max_delay = 0.0;
  LibertyCell *max_delay_cell = nullptr;
  for (LibertyCell *buffer : buffer_cells_) {
    float buffer_min_delay = bufferHoldDelay(buffer);
    if (max_buffer == nullptr
        || buffer_min_delay > max_delay) {
      max_buffer = buffer;
      max_delay = buffer_min_delay;
    }
  }
  return max_buffer;
}

float
Resizer::bufferHoldDelay(LibertyCell *buffer)
{
  Delay delays[RiseFall::index_count];
  bufferHoldDelays(buffer, delays);
  // 2026 Delay has no operator<; compare mean values.
  return std::min(float(delays[RiseFall::riseIndex()]),
                  float(delays[RiseFall::fallIndex()]));
}

// Min self delay across corners; buffer -> buffer
void
Resizer::bufferHoldDelays(LibertyCell *buffer,
                          // Return values.
                          Delay delays[RiseFall::index_count])
{
  LibertyPort *input, *output;
  buffer->bufferPorts(input, output);

  for (int rf_index : RiseFall::rangeIndex())
    delays[rf_index] = MinMax::min()->initValue();
  for (const Scene *scene : sta_->scenes()) {
    // 2021 took the corner library's input port cap; 2026 takes the port's
    // cap at the scene's max min_max (same mapping as pinCapacitance).
    float load_cap = input->capacitance(max_);
    DcalcAPIndex dcalc_ap = makeDcalcAp(scene, max_);
    ArcDelay gate_delays[RiseFall::index_count];
    Slew slews[RiseFall::index_count];
    gateDelays(output, load_cap, dcalc_ap, gate_delays, slews);
    for (int rf_index : RiseFall::rangeIndex())
      delays[rf_index] = std::min(float(delays[rf_index]),
                                  float(gate_delays[rf_index]));
  }
}

void
Resizer::repairHold(VertexSet *ends,
                    LibertyCell *buffer_cell,
                    float slack_margin,
                    bool allow_setup_violations,
                    int max_buffer_count)
{
  // Find endpoints with hold violation.
  VertexSet hold_failures(graph_);
  Slack worst_slack;
  findHoldViolations(ends, slack_margin, worst_slack, hold_failures);
  if (!hold_failures.empty()) {
    logger_->info(RSZ, 46, "Found {} endpoints with hold violations.",
                  hold_failures.size());
    inserted_buffer_count_ = 0;
    int repair_count = 1;
    int pass = 1;
    while (!hold_failures.empty()
           // Make sure we are making progress.
           && repair_count > 0
           && !overMaxArea()
           && inserted_buffer_count_ <= max_buffer_count) {
      repair_count = repairHoldPass(hold_failures, buffer_cell, slack_margin,
                                    allow_setup_violations, max_buffer_count);
      debugPrint(logger_, RSZ, "repair_hold", 1,
                 "pass {} worst slack {} failures {} inserted {}",
                 pass,
                 delayAsString(worst_slack, 3, sta_),
                 hold_failures .size(),
                 repair_count);
      sta_->findRequireds();
      findHoldViolations(ends, slack_margin, worst_slack, hold_failures);
      pass++;
    }
    if (slack_margin == 0.0 && fuzzyLess(worst_slack, 0.0))
      logger_->warn(RSZ, 66, "Unable to repair all hold violations.");
    else if (fuzzyLess(worst_slack, slack_margin))
      logger_->warn(RSZ, 64, "Unable to repair all hold checks within margin.");

    if (inserted_buffer_count_ > 0) {
      logger_->info(RSZ, 32, "Inserted {} hold buffers.", inserted_buffer_count_);
      level_drvr_vertices_valid_ = false;
    }
    if (inserted_buffer_count_ > max_buffer_count)
      logger_->error(RSZ, 60, "Max buffer count reached.");
    if (overMaxArea())
      logger_->error(RSZ, 50, "Max utilization reached.");
  }
  else
    logger_->info(RSZ, 33, "No hold violations found.");
}

void
Resizer::findHoldViolations(VertexSet *ends,
                            float slack_margin,
                            // Return values.
                            Slack &worst_slack,
                            VertexSet &hold_violations)
{
  Search *search = sta_->search();
  worst_slack = INF;
  hold_violations.clear();
  debugPrint(logger_, RSZ, "repair_hold", 3, "Hold violations");
  for (Vertex *end : *ends) {
    Slack slack = sta_->slack(end, MinMax::min());
    if (!isClockPin(end->pin())
        && fuzzyLess(slack, slack_margin)) {
      debugPrint(logger_, RSZ, "repair_hold", 3, " {}",
                 end->name(sdc_network_));
      if (slack < worst_slack)
        worst_slack = slack;
      hold_violations.insert(end);
    }
  }
}

int
Resizer::repairHoldPass(VertexSet &hold_failures,
                        LibertyCell *buffer_cell,
                        float slack_margin,
                        bool allow_setup_violations,
                        int max_buffer_count)
{
  VertexSet fanins = findHoldFanins(hold_failures);
  VertexSeq sorted_fanins = sortHoldFanins(fanins);
  
  int repair_count = 0;
  int max_repair_count = max(static_cast<int>(hold_failures.size() * .2), 10);
  for(int i = 0; i < sorted_fanins.size() && repair_count < max_repair_count ; i++) {
    Vertex *vertex = sorted_fanins[i];
    Pin *drvr_pin = vertex->pin();
    Net *net = network_->isTopLevelPort(drvr_pin)
      ? network_->net(network_->term(drvr_pin))
      : network_->net(drvr_pin);
    ensureWireParasitics();
    Slack drvr_slacks[RiseFall::index_count][MinMax::index_count];
    sta_->slacks(vertex, drvr_slacks);
    int min_index = MinMax::minIndex();
    int max_index = MinMax::maxIndex();
    const RiseFall *drvr_rf = (drvr_slacks[RiseFall::riseIndex()][min_index] <
                               drvr_slacks[RiseFall::fallIndex()][min_index])
      ? RiseFall::rise()
      : RiseFall::fall();
    Slack drvr_hold_slack = drvr_slacks[drvr_rf->index()][min_index] - slack_margin;
    Slack drvr_setup_slack = drvr_slacks[drvr_rf->index()][max_index];
    if (!isConstantPin(vertex->pin())
        && fuzzyLess(drvr_hold_slack, 0.0)
        // Have to have enough setup slack to add delay to repair the hold violation.
        && (allow_setup_violations
            || fuzzyLess(drvr_hold_slack, drvr_setup_slack))) {
      debugPrint(logger_, RSZ, "repair_hold", 2, "driver {}",
                 vertex->name(sdc_network_));
      // Only add delay to loads with hold violations.
      PinSeq load_pins;
      float load_cap = 0.0;
      bool loads_have_out_port = false;
      VertexOutEdgeIterator edge_iter(vertex, graph_);
      while (edge_iter.hasNext()) {
        Edge *edge = edge_iter.next();
        Vertex *fanout = edge->to(graph_);
        Slack fanout_slack = sta_->slack(fanout, MinMax::min());
        if (fanout_slack < slack_margin) {
          Pin *fanout_pin = fanout->pin();
          load_pins.push_back(fanout_pin);
          if (network_->direction(fanout_pin)->isAnyOutput()
              && network_->isTopLevelPort(fanout_pin))
            loads_have_out_port = true;
          else {
            LibertyPort *fanout_port = network_->libertyPort(fanout_pin);
            if (fanout_port)
              load_cap += fanout_port->capacitance();
          }
        }
      }
      if (!load_pins.empty()) {
        // multi-corner support MIA
        DcalcAPIndex dcalc_ap = makeDcalcAp(sta_->cmdScene(), max_);
        Delay buffer_delay1 = bufferDelay(buffer_cell, drvr_rf, load_cap, dcalc_ap);
        // Need enough slack to at least insert the last buffer with loads.
        if (allow_setup_violations
            || (buffer_delay1 < drvr_slacks[RiseFall::riseIndex()][max_index]
                && buffer_delay1 < drvr_slacks[RiseFall::fallIndex()][max_index])) {
          Delay buffer_delay = bufferHoldDelay(buffer_cell);
          Delay max_insert = min(drvr_slacks[RiseFall::riseIndex()][max_index],
                                 drvr_slacks[RiseFall::fallIndex()][max_index]);
          int max_insert_count = (max_insert - buffer_delay1) / buffer_delay + 1;
          int hold_buffer_count = (-drvr_hold_slack > buffer_delay1)
            ? std::ceil((-drvr_hold_slack-buffer_delay1)/buffer_delay)+1
            : 1;
          int buffer_count = allow_setup_violations
            ? hold_buffer_count
            : min(max_insert_count, hold_buffer_count);
          debugPrint(logger_, RSZ, "repair_hold", 2,
                     " {} hold={} inserted {} for {}/{} loads",
                     vertex->name(sdc_network_),
                     delayAsString(drvr_hold_slack, 3, this),
                     buffer_count,
                     load_pins.size(),
                     fanout(vertex));
          makeHoldDelay(vertex, buffer_count, load_pins,
                        loads_have_out_port, buffer_cell);
          repair_count += buffer_count;
          if (logger_->debugCheck(RSZ, "repair_hold", 4)) {
            // Check that no setup violations are introduced.
            ensureWireParasitics();
            Slack drvr_setup_slack1 = sta_->slack(vertex, max_);
            if (drvr_setup_slack1 < 0
                && drvr_setup_slack1 < drvr_setup_slack)
              printf("%s %s -> %s\n", vertex->name(network_).c_str(),
                     delayAsString(drvr_setup_slack, 3, sta_).c_str(),
                     delayAsString(drvr_setup_slack1, 3, sta_).c_str());
          }
          if (inserted_buffer_count_ > max_buffer_count
              || overMaxArea())
            return repair_count;
        }
      }
    }
  }
  return repair_count;
}

// 2021 SearchPredNonReg2 is gone in 2026. Local replacement: SearchPred0
// that additionally refuses to traverse internal arcs of sequential cells,
// so backward BFS fanin cones stop at register outputs instead of walking
// through registers to their inputs. (The 2026 base no-mode searchThru
// dispatches to the (edge, mode) override, so one override covers both.)
class NonRegSearchPred : public SearchPred0
{
public:
  NonRegSearchPred(const StaState *sta) : SearchPred0(sta) {}
  bool searchThru(Edge *edge,
                  const Mode *mode) const override
  {
    if (!SearchPred0::searchThru(edge, mode))
      return false;
    Graph *graph = sta_->graph();
    Vertex *from_vertex = graph->vertex(edge->from());
    Vertex *to_vertex = graph->vertex(edge->to());
    const Pin *from_pin = from_vertex ? from_vertex->pin() : nullptr;
    const Pin *to_pin = to_vertex ? to_vertex->pin() : nullptr;
    if (from_pin && to_pin) {
      const Network *network = sta_->network();
      Instance *from_inst = network->instance(from_pin);
      Instance *to_inst = network->instance(to_pin);
      if (from_inst && from_inst == to_inst) {
        Cell *cell = network->cell(from_inst);
        LibertyCell *lib_cell = cell ? network->libertyCell(cell) : nullptr;
        if (lib_cell && lib_cell->isSequential())
          return false;
      }
    }
    return true;
  }
};

// 2021 SearchPredNonLatch2 reconstruction: the candidate tree has no sta/
// sources, so the exact 2021 rule is not recoverable. findFanouts collects
// the combinational fanout cone of register outputs; stopping at latch
// boundaries (cells with a latch D->Q or enable->Q arc) matches that use.
class NonLatchSearchPred : public SearchPred0
{
public:
  NonLatchSearchPred(const StaState *sta) : SearchPred0(sta) {}
  bool searchThru(Edge *edge,
                  const Mode *mode) const override
  {
    if (!SearchPred0::searchThru(edge, mode))
      return false;
    Graph *graph = sta_->graph();
    Vertex *from_vertex = graph->vertex(edge->from());
    Vertex *to_vertex = graph->vertex(edge->to());
    const Pin *from_pin = from_vertex ? from_vertex->pin() : nullptr;
    const Pin *to_pin = to_vertex ? to_vertex->pin() : nullptr;
    if (from_pin && to_pin) {
      const Network *network = sta_->network();
      Instance *from_inst = network->instance(from_pin);
      Instance *to_inst = network->instance(to_pin);
      if (from_inst && from_inst == to_inst) {
        Cell *cell = network->cell(from_inst);
        LibertyCell *lib_cell = cell ? network->libertyCell(cell) : nullptr;
        if (lib_cell) {
          for (TimingArcSet *arc_set : lib_cell->timingArcSets()) {
            const TimingRole *role = arc_set->role();
            if (role->isLatchDtoQ() || role->isLatchEnToQ())
              return false;
          }
        }
      }
    }
    return true;
  }
};

VertexSet
Resizer::findHoldFanins(VertexSet &ends)
{
  Search *search = sta_->search();
  NonRegSearchPred pred(sta_);
  BfsBkwdIterator iter(BfsIndex::other, &pred, this);
  for (Vertex *vertex : ends)
    iter.enqueueAdjacentVertices(vertex);

  VertexSet fanins(graph_);
  while (iter.hasNext()) {
    Vertex *fanin = iter.next();
    if (!isClockPin(fanin->pin())) {
      if (fanin->isDriver(network_))
        fanins.insert(fanin);
      iter.enqueueAdjacentVertices(fanin);
    }
  }
  return fanins;
}

VertexSeq
Resizer::sortHoldFanins(VertexSet &fanins)
{
  VertexSeq sorted_fanins;
  for(Vertex *vertex : fanins)
    sorted_fanins.push_back(vertex);

  sort(sorted_fanins, [&](Vertex *v1, Vertex *v2)
                      { float s1 = sta_->slack(v1, MinMax::min());
                        float s2 = sta_->slack(v2, MinMax::min());
                        if (fuzzyEqual(s1, s2)) {
                          float gap1 = slackGap(v1);
                          float gap2 = slackGap(v2);
                          // Break ties based on the hold/setup gap.
                          if (fuzzyEqual(gap1, gap2))
                            return v1->level() > v2->level();
                          else
                            return gap1 > gap2;
                        }
                        else
                          return s1 < s2;});
  if (logger_->debugCheck(RSZ, "repair_hold", 4)) {
    printf("Sorted fanins");
    printf("     hold_slack  slack_gap  level");
    for(Vertex *vertex : sorted_fanins)
      printf("%s %s %s %d",
             vertex->name(network_).c_str(),
             delayAsString(sta_->slack(vertex, MinMax::min()), 3, sta_).c_str(),
             delayAsString(slackGap(vertex), 3, sta_).c_str(),
             vertex->level());
  }
  return sorted_fanins;
}

void
Resizer::makeHoldDelay(Vertex *drvr,
                       int buffer_count,
                       PinSeq &load_pins,
                       bool loads_have_out_port,
                       LibertyCell *buffer_cell)
{
  Pin *drvr_pin = drvr->pin();
  Instance *parent = db_network_->topInstance();
  Net *drvr_net = network_->isTopLevelPort(drvr_pin)
    ? db_network_->net(db_network_->term(drvr_pin))
    : db_network_->net(drvr_pin);
  Net *in_net, *out_net;
  if (loads_have_out_port) {
    // Verilog uses nets as ports, so the net connected to an output port has
    // to be preserved.
    // Move the driver pin over to gensym'd net.
    in_net = makeUniqueNet();
    Port *drvr_port = network_->port(drvr_pin);
    Instance *drvr_inst = network_->instance(drvr_pin);
    sta_->disconnectPin(drvr_pin);
    sta_->connectPin(drvr_inst, drvr_port, in_net);
    out_net = drvr_net;
  }
  else {
    in_net = drvr_net;
    out_net = makeUniqueNet();
  }

  parasiticsInvalid(in_net);
  // Spread buffers between driver and load center.
  Point drvr_loc = db_network_->location(drvr_pin);
  Point load_center = findCenter(load_pins);
  int dx = (load_center.x() - drvr_loc.x()) / (buffer_count + 1);
  int dy = (load_center.y() - drvr_loc.y()) / (buffer_count + 1);

  Net *buf_in_net = in_net;
  Instance *buffer = nullptr;
  LibertyPort *input, *output;
  buffer_cell->bufferPorts(input, output);
  // drvr_pin->in_net->hold_buffer1->net2->hold_buffer2->out_net...->load_pins
  for (int i = 0; i < buffer_count; i++) {
    Net *buf_out_net = (i == buffer_count - 1) ? out_net : makeUniqueNet();
    // drvr_pin->drvr_net->hold_buffer->net2->load_pins
    string buffer_name = makeUniqueInstName("hold");
    buffer = db_network_->makeInstance(buffer_cell, buffer_name.c_str(),
                                       parent);
    journalMakeBuffer(buffer);
    inserted_buffer_count_++;
    designAreaIncr(area(db_network_->cell(buffer_cell)));

    sta_->connectPin(buffer, input, buf_in_net);
    sta_->connectPin(buffer, output, buf_out_net);
    Point buffer_loc(drvr_loc.x() + dx * i,
                     drvr_loc.y() + dy * i);
    setLocation(buffer, buffer_loc);
    buf_in_net = buf_out_net;
    parasiticsInvalid(buf_out_net);
  }

  for (const Pin *load_pin : load_pins) {
    Instance *load = db_network_->instance(load_pin);
    Port *load_port = db_network_->port(load_pin);
    sta_->disconnectPin(const_cast<Pin*>(load_pin));
    sta_->connectPin(load, load_port, out_net);
  }
  Pin *buffer_out_pin = network_->findPin(buffer, output);
  resizeToTargetSlew(buffer_out_pin, false);
}

Point
Resizer::findCenter(PinSeq &pins)
{
  long sum_x = 0;
  long sum_y = 0;
  for (const Pin *pin : pins) {
    Point loc = db_network_->location(pin);
    sum_x += loc.x();
    sum_y += loc.y();
  }
  return Point(sum_x / pins.size(), sum_y / pins.size());
}

// Gap between min setup and hold slacks.
// This says how much head room there is for adding delay to fix a
// hold violation before violating a setup check.
Slack
Resizer::slackGap(Slacks &slacks)
{
  return min(slacks[RiseFall::riseIndex()][MinMax::maxIndex()]
             - slacks[RiseFall::riseIndex()][MinMax::minIndex()],
             slacks[RiseFall::fallIndex()][MinMax::maxIndex()]
             - slacks[RiseFall::fallIndex()][MinMax::minIndex()]);
}

Slack
Resizer::slackGap(Vertex *vertex)
{
  Slacks slacks;
  sta_->slacks(vertex, slacks);
  return slackGap(slacks);
}

int
Resizer::fanout(Vertex *vertex)
{
  int fanout = 0;
  VertexOutEdgeIterator edge_iter(vertex, graph_);
  while (edge_iter.hasNext()) {
    edge_iter.next();
    fanout++;
  }
  return fanout;
}

////////////////////////////////////////////////////////////////

void
Resizer::reportLongWires(int count,
                         int digits)
{
  graph_ = sta_->ensureGraph();
  for (Mode *mode : sta_->modes())
    sta_->ensureClkNetwork(mode);
  VertexSeq drvrs;
  findLongWires(drvrs);
  report_->reportLine("Driver    length delay");
  const Scene *scene = sta_->cmdScene();
  double wire_res = wireSignalResistance(scene);
  double wire_cap = wireSignalCapacitance(scene);
  int i = 0;
  for (Vertex *drvr : drvrs) {
    Pin *drvr_pin = drvr->pin();
    double wire_length = dbuToMeters(maxLoadManhattenDistance(drvr));
    double steiner_length = dbuToMeters(findMaxSteinerDist(drvr));
    double delay = (wire_length * wire_res) * (wire_length * wire_cap) * 0.5;
    report_->reportLine(std::format("{} manhtn {} steiner {} {}",
                                     sdc_network_->pathName(drvr_pin),
                                     units_->distanceUnit()->asString(wire_length, 1),
                                     units_->distanceUnit()->asString(steiner_length, 1),
                                     delayAsString(delay, digits, sta_)));
    if (i == count)
      break;
    i++;
  }
}

typedef std::pair<Vertex*, int> DrvrDist;

void
Resizer::findLongWires(VertexSeq &drvrs)
{
  std::vector<DrvrDist> drvr_dists;
  VertexIterator vertex_iter(graph_);
  while (vertex_iter.hasNext()) {
    Vertex *vertex = vertex_iter.next();
    if (vertex->isDriver(network_)) {
      Pin *pin = vertex->pin();
      Net *net = network_->net(pin);
      // Hands off the clock nets.
      if (!isClockPin(pin)
          && !isConstantPin(vertex->pin())
          && !isDisabledConstraint(vertex))
        drvr_dists.push_back(DrvrDist(vertex, maxLoadManhattenDistance(vertex)));
    }
  }
  sort(drvr_dists, [this](const DrvrDist &drvr_dist1,
                          const DrvrDist &drvr_dist2) {
                    return drvr_dist1.second > drvr_dist2.second;
                  });
  drvrs.reserve(drvr_dists.size());
  for (DrvrDist &drvr_dist : drvr_dists)
    drvrs.push_back(drvr_dist.first);
}

void
Resizer::findLongWiresSteiner(VertexSeq &drvrs)
{
  std::vector<DrvrDist> drvr_dists;
  VertexIterator vertex_iter(graph_);
  while (vertex_iter.hasNext()) {
    Vertex *vertex = vertex_iter.next();
    if (vertex->isDriver(network_)) {
      Pin *pin = vertex->pin();
      Net *net = network_->net(pin);
      // Hands off the clock nets.
      if (!isClockPin(pin)
          && !isConstantPin(vertex->pin()))
        drvr_dists.push_back(DrvrDist(vertex, findMaxSteinerDist(vertex)));
    }
  }
  sort(drvr_dists, [this](const DrvrDist &drvr_dist1,
                          const DrvrDist &drvr_dist2) {
                     return drvr_dist1.second > drvr_dist2.second;
                   });
  drvrs.reserve(drvr_dists.size());
  for (DrvrDist &drvr_dist : drvr_dists)
    drvrs.push_back(drvr_dist.first);
}

// Find the maximum distance along steiner tree branches from
// the driver to loads (in dbu).
int
Resizer::findMaxSteinerDist(Vertex *drvr)
{
  Pin *drvr_pin = drvr->pin();
  SteinerTree *tree = makeSteinerTree(drvr_pin, routingAlpha(), true,
                                      db_network_, logger_);
  if (tree) {
    int dist = findMaxSteinerDist(tree);
    delete tree;
    return dist;
  }
  return 0;
}

int
Resizer::findMaxSteinerDist(SteinerTree *tree)
{
  SteinerPt drvr_pt = tree->drvrPt(network_);
  return findMaxSteinerDist(tree, drvr_pt, 0);
}

// DFS of steiner tree.
int
Resizer::findMaxSteinerDist(SteinerTree *tree,
                            SteinerPt pt,
                            int dist_from_drvr)
{
  const PinSeq *pins = tree->pins(pt);
  if (pins) {
    for (const Pin *pin : *pins) {
      if (db_network_->isLoad(pin))
        return dist_from_drvr;
    }
  }
  Point loc = tree->location(pt);
  SteinerPt left = tree->left(pt);
  int left_max = 0;
  if (left != SteinerTree::null_pt) {
    int left_dist = Point::manhattanDistance(loc, tree->location(left));
    left_max = findMaxSteinerDist(tree, left, dist_from_drvr + left_dist);
  }
  SteinerPt right = tree->right(pt);
  int right_max = 0;
  if (right != SteinerTree::null_pt) {
    int right_dist = Point::manhattanDistance(loc, tree->location(right));
    right_max = findMaxSteinerDist(tree, right, dist_from_drvr + right_dist);
  }
  return max(left_max, right_max);
}

double
Resizer::maxLoadManhattenDistance(const Net *net)
{
  NetPinIterator *pin_iter = network_->pinIterator(net);
  int max_dist = 0;
  while (pin_iter->hasNext()) {
    const Pin *pin = pin_iter->next();
    if (network_->isDriver(pin)) {
      Vertex *drvr = graph_->pinDrvrVertex(pin);
      if (drvr) {
        int dist = maxLoadManhattenDistance(drvr);
        max_dist = max(max_dist, dist);
      }
    }
  }
  delete pin_iter;
  return dbuToMeters(max_dist);
}

int
Resizer::maxLoadManhattenDistance(Vertex *drvr)
{
  int max_dist = 0;
  Point drvr_loc = db_network_->location(drvr->pin());
  VertexOutEdgeIterator edge_iter(drvr, graph_);
  while (edge_iter.hasNext()) {
    Edge *edge = edge_iter.next();
    Vertex *load = edge->to(graph_);
    Point load_loc = db_network_->location(load->pin());
    int dist = Point::manhattanDistance(drvr_loc, load_loc);
    max_dist = max(max_dist, dist);
  }
  return max_dist;
}

////////////////////////////////////////////////////////////////

NetSeq *
Resizer::findFloatingNets()
{
  NetSeq *floating_nets = new NetSeq;
  NetIterator *net_iter = network_->netIterator(network_->topInstance());
  while (net_iter->hasNext()) {
    Net *net = net_iter->next();
    PinSeq loads;
    PinSeq drvrs;
    // Port adapter (P-29): bind the network (see init).
    PinSet visited_drvrs(network_);
    FindNetDrvrLoads visitor(nullptr, visited_drvrs, loads, drvrs, network_);
    network_->visitConnectedPins(net, visitor);
    if (drvrs.size() == 0 && loads.size() > 0)
      floating_nets->push_back(net);
  }
  delete net_iter;
  sort(floating_nets, sta::NetPathNameLess(network_));
  return floating_nets;
}

////////////////////////////////////////////////////////////////

string
Resizer::makeUniqueNetName()
{
  string node_name;
  Instance *top_inst = network_->topInstance();
  do 
    node_name = std::format("net{}", unique_net_index_++);
  while (network_->findNet(top_inst, node_name.c_str()));
  return node_name;
}

Net *
Resizer::makeUniqueNet()
{
  string net_name = makeUniqueNetName();
  Instance *parent = db_network_->topInstance();
  return db_network_->makeNet(net_name.c_str(), parent);
}

string
Resizer::makeUniqueInstName(const char *base_name)
{
  return makeUniqueInstName(base_name, false);
}

string
Resizer::makeUniqueInstName(const char *base_name,
                            bool underscore)
{
  string inst_name;
  do 
    inst_name = underscore
      ? std::format("{}_{}", base_name, unique_inst_index_++)
      : std::format("{}{}", base_name, unique_inst_index_++);
  while (network_->findInstance(inst_name.c_str()));
  return inst_name;
}

float
Resizer::portFanoutLoad(LibertyPort *port)
{
  float fanout_load;
  bool exists;
  port->fanoutLoad(fanout_load, exists);
  if (!exists) {
    LibertyLibrary *lib = port->libertyLibrary();
    lib->defaultFanoutLoad(fanout_load, exists);
  }
  if (exists)
    return fanout_load;
  else
    return 0.0;
}

float
Resizer::bufferDelay(LibertyCell *buffer_cell,
                     const RiseFall *rf,
                     float load_cap,
                     DcalcAPIndex dcalc_ap)
{
  LibertyPort *input, *output;
  buffer_cell->bufferPorts(input, output);
  ArcDelay gate_delays[RiseFall::index_count];
  Slew slews[RiseFall::index_count];
  gateDelays(output, load_cap, dcalc_ap, gate_delays, slews);
  return gate_delays[rf->index()];
}

float
Resizer::bufferDelay(LibertyCell *buffer_cell,
                     float load_cap,
                     DcalcAPIndex dcalc_ap)
{
  LibertyPort *input, *output;
  buffer_cell->bufferPorts(input, output);
  ArcDelay gate_delays[RiseFall::index_count];
  Slew slews[RiseFall::index_count];
  gateDelays(output, load_cap, dcalc_ap, gate_delays, slews);
  return max(gate_delays[RiseFall::riseIndex()],
             gate_delays[RiseFall::fallIndex()]);
}

// Self delay; buffer -> buffer
float
Resizer::bufferSelfDelay(LibertyCell *buffer_cell)
{
  DcalcAPIndex dcalc_ap = makeDcalcAp(sta_->cmdScene(), max_);
  LibertyPort *input, *output;
  buffer_cell->bufferPorts(input, output);
  ArcDelay gate_delays[RiseFall::index_count];
  Slew slews[RiseFall::index_count];
  float load_cap = input->capacitance();
  gateDelays(output, load_cap, dcalc_ap, gate_delays, slews);
  return max(gate_delays[RiseFall::riseIndex()],
             gate_delays[RiseFall::fallIndex()]);
}

float
Resizer::bufferSelfDelay(LibertyCell *buffer_cell,
                         const RiseFall *rf)
{
  DcalcAPIndex dcalc_ap = makeDcalcAp(sta_->cmdScene(), max_);
  LibertyPort *input, *output;
  buffer_cell->bufferPorts(input, output);
  ArcDelay gate_delays[RiseFall::index_count];
  Slew slews[RiseFall::index_count];
  float load_cap = input->capacitance();
  gateDelays(output, load_cap, dcalc_ap, gate_delays, slews);
  return gate_delays[rf->index()];
}

// Rise/fall delays across all timing arcs into drvr_port.
// Uses target slew for input slew.
void
Resizer::gateDelays(LibertyPort *drvr_port,
                    float load_cap,
                    DcalcAPIndex dcalc_ap,
                    // Return values.
                    ArcDelay delays[RiseFall::index_count],
                    Slew slews[RiseFall::index_count])
{
  for (int rf_index : RiseFall::rangeIndex()) {
    delays[rf_index] = -INF;
    slews[rf_index] = -INF;
  }
  const Scene *scene;
  const MinMax *min_max;
  dcalcApParts(dcalc_ap, scene, min_max);
  const Pvt *pvt = sta_->operatingConditions(min_max, scene->mode()->sdc());
  LibertyCell *cell = drvr_port->libertyCell();
  for (TimingArcSet *arc_set : cell->timingArcSets()) {
    if (arc_set->to() == drvr_port
        && !arc_set->role()->isTimingCheck()) {
      for (TimingArc *arc : arc_set->arcs()) {
        const RiseFall *in_rf = arc->fromEdge()->asRiseFall();
        int out_rf_index = arc->toEdge()->asRiseFall()->index();
        float in_slew = tgt_slews_[in_rf->index()];
        ArcDelay gate_delay;
        Slew drvr_slew;
        // 2026 per-arc gateDelay overload is marked deprecated; it is the
        // direct mapping of the 2021 call. R1 should move to the batch
        // gateDelays(ArcDcalcArgSeq) API.
        arc_delay_calc_->gateDelay(arc, in_slew, load_cap,
                                   nullptr, 0.0, pvt, scene, min_max,
                                   gate_delay,
                                   drvr_slew);
        delays[out_rf_index] = std::max(float(delays[out_rf_index]), float(gate_delay));
        slews[out_rf_index] = std::max(float(slews[out_rf_index]), float(drvr_slew));
      }
    }
  }
}

ArcDelay
Resizer::gateDelay(LibertyPort *drvr_port,
                   const RiseFall *rf,
                   float load_cap,
                   DcalcAPIndex dcalc_ap)
{
  ArcDelay delays[RiseFall::index_count];
  Slew slews[RiseFall::index_count];
  gateDelays(drvr_port, load_cap, dcalc_ap, delays, slews);
  return delays[rf->index()];
}

ArcDelay
Resizer::gateDelay(LibertyPort *drvr_port,
                   float load_cap,
                   DcalcAPIndex dcalc_ap)
{
  ArcDelay delays[RiseFall::index_count];
  Slew slews[RiseFall::index_count];
  gateDelays(drvr_port, load_cap, dcalc_ap, delays, slews);
  return std::max(float(delays[RiseFall::riseIndex()]), float(delays[RiseFall::fallIndex()]));
}

////////////////////////////////////////////////////////////////

double
Resizer::findMaxWireLength()
{
  double max_length = INF;
  for (const Scene *scene : sta_->scenes()) {
    for (LibertyCell *buffer_cell : buffer_cells_) {
      double buffer_length = findMaxWireLength(buffer_cell, scene);
      max_length = min(max_length, buffer_length);
    }
  }
  return max_length;
}

// Find the max wire length before it is faster to split the wire
// in half with a buffer (in meters).
double
Resizer::findMaxWireLength(LibertyCell *buffer_cell,
                           const Scene *scene)
{
  ensureBlock();
  LibertyPort *load_port, *drvr_port;
  buffer_cell->bufferPorts(load_port, drvr_port);
  return findMaxWireLength(drvr_port, scene);
}

double
Resizer::findMaxWireLength(LibertyPort *drvr_port,
                           const Scene *scene)
{
  LibertyCell *cell = drvr_port->libertyCell();
  double drvr_r = drvr_port->driveResistance();
  // wire_length1 lower bound
  // wire_length2 upper bound
  double wire_length1 = 0.0;
  // Initial guess with wire resistance same as driver resistance.
  double wire_length2 = drvr_r / wireSignalResistance(scene);
  double tol = .01; // 1%
  double diff1 = splitWireDelayDiff(wire_length1, cell);
  double diff2 = splitWireDelayDiff(wire_length2, cell);
  // binary search for diff = 0.
  while (abs(wire_length1 - wire_length2) > max(wire_length1, wire_length2) * tol) {
    if (diff2 < 0.0) {
      wire_length1 = wire_length2;
      diff1 = diff2;
      wire_length2 *= 2;
      diff2 = splitWireDelayDiff(wire_length2, cell);
    }
    else {
      double wire_length3 = (wire_length1 + wire_length2) / 2.0;
      double diff3 = splitWireDelayDiff(wire_length3, cell);
      if (diff3 < 0.0) {
        wire_length1 = wire_length3;
        diff1 = diff3;
      }
      else {
        wire_length2 = wire_length3;
        diff2 = diff3;
      }
    }
  }
  return wire_length1;
}

// objective function
double
Resizer::splitWireDelayDiff(double wire_length,
                            LibertyCell *buffer_cell)
{
  Delay delay1, delay2;
  Slew slew1, slew2;
  bufferWireDelay(buffer_cell, wire_length, delay1, slew1);
  bufferWireDelay(buffer_cell, wire_length / 2, delay2, slew2);
  return float(delay1) - float(delay2) * 2;
}

void
Resizer::bufferWireDelay(LibertyCell *buffer_cell,
                         double wire_length, // meters
                         // Return values.
                         Delay &delay,
                         Slew &slew)
{
  LibertyPort *load_port, *drvr_port;
  buffer_cell->bufferPorts(load_port, drvr_port);
  return cellWireDelay(drvr_port, load_port, wire_length, delay, slew);
}

// Cell delay plus wire delay.
// Use target slew for input slew.
// drvr_port and load_port do not have to be the same liberty cell.
void
Resizer::cellWireDelay(LibertyPort *drvr_port,
                       LibertyPort *load_port,
                       double wire_length, // meters
                       // Return values.
                       Delay &delay,
                       Slew &slew)
{
  // Make a (hierarchical) block to use as a scratchpad.
  // Port adapter (P-31): upstream destroys the scratch block at the end
  // of cellWireDelay, so the fixed name "wire_delay" never collides.
  // ead3236 odb has no dbBlock::destroy, so the port leaks one empty
  // block per call (see teardown note below) and must use a unique name
  // per call: the second call's create otherwise returns null (child
  // "wire_delay" already exists) and setBlock segfaults on it (observed
  // in findMaxWireLength). ensureBlock first: block_ is null until the
  // first ensureBlock, and create dereferences its parent.
  ensureBlock();
  static int wire_delay_block_index = 0;
  std::string scratch_name =
      std::format("wire_delay{}", wire_delay_block_index++);
  dbBlock *block = dbBlock::create(block_, scratch_name.c_str(), nullptr, '/');
  // Mirror the 2021 makeBlockSta recipe (dbSta.cc) minus OpenRoad app
  // wiring: fresh dbSta, components, scratch block, logger, units.
  // Port adapter (P-30): 2026 ReportTcl::~ReportTcl unconditionally calls
  // Tcl_UnstackChannel on its interp, so a scratch sta that never got
  // setTclInterp segfaults at delete (observed in findMaxWireLength).
  // The scratch sta gets its own interp, created and destroyed here; the
  // main interp is not shared, so teardown unstacks a real channel stack.
  Tcl_Interp *scratch_interp = Tcl_CreateInterp();
  Tcl_Init(scratch_interp);
  dbSta *sta = new dbSta;
  sta->makeComponents();
  sta->setTclInterp(scratch_interp);
  // Port adapter (P-32): the scratch sta must be able to report errors
  // without segfaulting: dbStaReport routes through a utl::Logger, and
  // leaving it null crashes inside errorMsg (observed in
  // findMaxWireLength when the delay calc reports). Wire the main logger;
  // the scratch sta never reports on success, so this only surfaces real
  // failures. Full dbSta::init is NOT used here: it would hijack the
  // global Sta singleton via Sta::setSta.
  sta->setReportLogger(logger_);
  sta->getDbNetwork()->setBlock(block);
  // NOTE: the scratch sta never reports, so its dbStaReport logger is
  // left null (dbStaReport is a dbSta.cc-local class in v2.0, not visible
  // here; the 2021 makeBlockSta setLogger call has no 2026 target).
  sta->copyUnits(sta_->units());
  // The scratch sta's default scene (created by makeComponents) holds the
  // parasitics for the scratch network. Operating conditions and wire RC
  // come from the main sta's scenes, which carry SDC; the scratch scene
  // has no SDC.
  const Scene *scratch_scene = sta->cmdScene();
  Parasitics *scratch_parasitics = scratch_scene->parasitics(max_);
  Network *network = sta->network();
  ArcDelayCalc *arc_delay_calc = sta->arcDelayCalc();

  Instance *top_inst = network->topInstance();
  // Tmp net for parasitics to live on.
  Net *net = sta->makeNet("wire", top_inst);
  LibertyCell *drvr_cell = drvr_port->libertyCell();
  LibertyCell *load_cell = load_port->libertyCell();
  Instance *drvr = sta->makeInstance("drvr", drvr_cell, top_inst);
  Instance *load = sta->makeInstance("load", load_cell, top_inst);
  sta->connectPin(drvr, drvr_port, net);
  sta->connectPin(load, load_port, net);
  Pin *drvr_pin = network->findPin(drvr, drvr_port);
  Pin *load_pin = network->findPin(load, load_port);

  // Max rise/fall delays.
  delay = -INF;
  slew = -INF;

  for (const Scene *scene : sta_->scenes()) {
    const Pvt *pvt = sta_->operatingConditions(max_, scene->mode()->sdc());

    makeWireParasitic(net, drvr_pin, load_pin, wire_length, scene,
                      scratch_parasitics, network);
    // Let delay calc reduce parasitic network as it sees fit.
    Parasitic *drvr_parasitic = arc_delay_calc->findParasitic(drvr_pin,
                                                              RiseFall::rise(),
                                                              scratch_scene,
                                                              max_);

    for (TimingArcSet *arc_set : drvr_cell->timingArcSets()) {
      if (arc_set->to() == drvr_port) {
        for (TimingArc *arc : arc_set->arcs()) {
          const RiseFall *in_rf = arc->fromEdge()->asRiseFall();
          Slew in_slew = tgt_slews_[in_rf->index()];
          ArcDelay gate_delay;
          Slew drvr_slew;
          arc_delay_calc->gateDelay(arc, in_slew, 0.0,
                                    drvr_parasitic, 0.0, pvt, scratch_scene,
                                    max_, gate_delay, drvr_slew);
          // 2021 ArcDelayCalc::loadDelay is gone in 2026. Wire delay is the
          // Elmore delay driver -> load through the pi network (exact for
          // this single segment). No 2026 primitive returns the load slew
          // through the network, so load_slew falls back to drvr_slew (wire
          // slew degradation dropped); findMaxSlewWireLength is currently
          // uncalled in the repair flow, so this is dormant for the run.
          ArcDelay wire_delay = 0.0;
          Slew load_slew = drvr_slew;
          float elmore = 0.0;
          bool elmore_exists = false;
          scratch_parasitics->findElmore(drvr_parasitic, load_pin,
                                         elmore, elmore_exists);
          if (elmore_exists)
            wire_delay = elmore;
          delay = std::max(float(delay),
                           float(gate_delay) + float(wire_delay));
          slew = std::max(float(slew), float(load_slew));
        }
      }
    }
    arc_delay_calc->finishDrvrPin();
    scratch_parasitics->deleteParasiticNetwork(net);
  }

  // Cleanup the turds.
  sta->deleteInstance(drvr);
  sta->deleteInstance(load);
  sta->deleteNet(net);
  delete sta;
  Tcl_DeleteInterp(scratch_interp);
  // NOTE: ead3236 odb has no dbBlock::destroy; the empty scratch block
  // leaks (one per call, bounded by repair iterations). R1 teardown item.
}

Parasitic *
Resizer::makeWireParasitic(Net *net,
                           Pin *drvr_pin,
                           Pin *load_pin,
                           double wire_length, // meters
                           const Scene *scene,
                           Parasitics *parasitics,
                           Network *network)
{
  Parasitic *parasitic = parasitics->makeParasiticNetwork(net, false);
  ParasiticNode *n1 = parasitics->ensureParasiticNode(parasitic, drvr_pin,
                                                      network);
  ParasiticNode *n2 = parasitics->ensureParasiticNode(parasitic, load_pin,
                                                      network);
  double wire_cap = wire_length * wireSignalCapacitance(scene);
  double wire_res = wire_length * wireSignalResistance(scene);
  parasitics->incrCap(n1, wire_cap / 2.0);
  // Single resistor for the segment; id 0 (same choice as the
  // EstimateWireParasitics pad parasitic).
  parasitics->makeResistor(parasitic, 0, wire_res, n1, n2);
  parasitics->incrCap(n2, wire_cap / 2.0);
  return parasitic;
}

////////////////////////////////////////////////////////////////

double
Resizer::findMaxSlewWireLength(LibertyPort *drvr_port,
                               LibertyPort *load_port,
                               double max_slew,
                               const Scene *scene)
{
  ensureBlock();
  // wire_length1 lower bound
  // wire_length2 upper bound
  double wire_length1 = 0.0;
  double wire_length2 = std::sqrt(max_slew /(wireSignalResistance(scene)
                                             * wireSignalCapacitance(scene)));
  double tol = .01; // 1%
  double diff1 = maxSlewWireDiff(drvr_port, load_port, wire_length1, max_slew);
  double diff2 = maxSlewWireDiff(drvr_port, load_port, wire_length2, max_slew);
  // binary search for diff = 0.
  while (abs(wire_length1 - wire_length2) > max(wire_length1, wire_length2) * tol) {
    if (diff2 < 0.0) {
      wire_length1 = wire_length2;
      diff1 = diff2;
      wire_length2 *= 2;
      diff2 = maxSlewWireDiff(drvr_port, load_port, wire_length2, max_slew);
    }
    else {
      double wire_length3 = (wire_length1 + wire_length2) / 2.0;
      double diff3 = maxSlewWireDiff(drvr_port, load_port, wire_length3, max_slew);
      if (diff3 < 0.0) {
        wire_length1 = wire_length3;
        diff1 = diff3;
      }
      else {
        wire_length2 = wire_length3;
        diff2 = diff3;
      }
    }
  }
  return wire_length1;
}

// objective function
double
Resizer::maxSlewWireDiff(LibertyPort *drvr_port,
                         LibertyPort *load_port,
                         double wire_length,
                         double max_slew)
{
  Delay delay;
  Slew slew;
  cellWireDelay(drvr_port, load_port, wire_length, delay, slew);
  return float(slew) - max_slew;
}

////////////////////////////////////////////////////////////////

double
Resizer::designArea()
{
  ensureBlock();
  ensureDesignArea();
  return design_area_;
}

void
Resizer::designAreaIncr(float delta)
{
  design_area_ += delta;
}

void
Resizer::ensureDesignArea()
{
  if (core_exists_) {
    design_area_ = 0.0;
    for (dbInst *inst : block_->getInsts()) {
      dbMaster *master = inst->getMaster();
      design_area_ += area(master);
    }
  }
}

int
Resizer::fanout(Pin *drvr_pin)
{
  int fanout = 0;
  NetConnectedPinIterator *pin_iter = network_->connectedPinIterator(drvr_pin);
  while (pin_iter->hasNext()) {
    const Pin *pin = pin_iter->next();
    if (pin != drvr_pin)
      fanout++;
  }
  delete pin_iter;
  return fanout;
}

bool
Resizer::isFuncOneZero(const Pin *drvr_pin)
{
  LibertyPort *port = network_->libertyPort(drvr_pin);
  if (port) {
    FuncExpr *func = port->function();
    return func && (func->op() == FuncExpr::Op::zero
                    || func->op() == FuncExpr::Op::one);
  }
  return false;
}

////////////////////////////////////////////////////////////////

void
Resizer::repairClkInverters()
{
  // Abbreviated copyState
  db_network_ = sta_->getDbNetwork();
  sta_->ensureLevelized();
  graph_ = sta_->graph();
  ensureBlock();
  ensureDesignArea();
  for (const Instance *inv : findClkInverters())
    cloneClkInverter(const_cast<Instance*>(inv));
}

InstanceSeq
Resizer::findClkInverters()
{
  InstanceSeq clk_inverters;
  ClkTreeSearchPred srch_pred(this);
  BfsFwdIterator bfs(BfsIndex::other, &srch_pred, this);
  for (Mode *mode : sta_->modes())
  for (Clock *clk : mode->sdc()->clocks()) {
    for (const Pin *pin : clk->leafPins()) {
      Vertex *vertex = graph_->pinDrvrVertex(pin);
      bfs.enqueue(vertex);
    }
  }
  while (bfs.hasNext()) {
    Vertex *vertex = bfs.next();
    const Pin *pin = vertex->pin();
    Instance *inst = network_->instance(pin);
    LibertyCell *lib_cell = network_->libertyCell(inst);
    if (vertex->isDriver(network_)
        && lib_cell
        && lib_cell->isInverter()) {
      clk_inverters.push_back(inst);
      debugPrint(logger_, RSZ, "repair_clk_inverters", 2, "inverter {}",
                 network_->pathName(inst));
    }
    if (!vertex->isRegClk())
      bfs.enqueueAdjacentVertices(vertex);
  }
  return clk_inverters;
}

void
Resizer::cloneClkInverter(Instance *inv)
{
  LibertyCell *inv_cell = network_->libertyCell(inv);
  LibertyPort *in_port, *out_port;
  inv_cell->bufferPorts(in_port, out_port);
  Pin *in_pin = network_->findPin(inv, in_port);
  Pin *out_pin = network_->findPin(inv, out_port);
  Net *in_net = network_->net(in_pin);
  dbNet *in_net_db = db_network_->staToDb(in_net);
  Net *out_net = network_->isTopLevelPort(out_pin)
    ? network_->net(network_->term(out_pin))
    : network_->net(out_pin);
  if (out_net) {
    string inv_name = network_->name(inv);
    Instance *top_inst = network_->topInstance();
    NetConnectedPinIterator *load_iter = network_->pinIterator(out_net);
    while (load_iter->hasNext()) {
      const Pin *load_pin = load_iter->next();
      if (load_pin != out_pin) {
        string clone_name = makeUniqueInstName(inv_name.c_str(), true);
        Instance *clone = sta_->makeInstance(clone_name.c_str(),
                                             inv_cell, top_inst);
        Point clone_loc = db_network_->location(load_pin);
        journalMakeBuffer(clone);
        setLocation(clone, clone_loc);

        Net *clone_out_net = makeUniqueNet();
        dbNet *clone_out_net_db = db_network_->staToDb(clone_out_net);
        clone_out_net_db->setSigType(in_net_db->getSigType());

        Instance *load = network_->instance(load_pin);
        sta_->connectPin(clone, in_port, in_net);
        sta_->connectPin(clone, out_port, clone_out_net);

        // Connect load to clone
        sta_->disconnectPin(const_cast<Pin*>(load_pin));
        Port *load_port = network_->port(load_pin);
        sta_->connectPin(load, load_port, clone_out_net);
      }
    }
    delete load_iter;

    // Delete inv
    sta_->disconnectPin(in_pin);
    sta_->disconnectPin(out_pin);
    sta_->deleteNet(out_net);
    sta_->deleteInstance(inv);
  }
}

////////////////////////////////////////////////////////////////

// Journal to roll back changes (OpenDB not up to the task).
void
Resizer::journalBegin()
{
  debugPrint(logger_, RSZ, "journal", 1, "journal begin");
  resized_inst_map_.clear();
  inserted_buffers_.clear();
}

void
Resizer::journalInstReplaceCellBefore(Instance *inst)
{
  LibertyCell *lib_cell = network_->libertyCell(inst);
  debugPrint(logger_, RSZ, "journal", 1, "journal replace {} ({})",
             network_->pathName(inst),
             lib_cell->name());
  // Do not clobber an existing checkpoint cell.
  if (resized_inst_map_.count(inst) == 0)
    resized_inst_map_[inst] = lib_cell;
}

void
Resizer::journalMakeBuffer(Instance *buffer)
{
  debugPrint(logger_, RSZ, "journal", 1, "journal make_buffer {}",
             network_->pathName(buffer));
  inserted_buffers_.insert(buffer);
}

void
Resizer::journalRestore()
{
  for (auto inst_cell : resized_inst_map_) {
    Instance *inst = inst_cell.first;
    if (inserted_buffers_.count(inst) == 0) {
      LibertyCell *lib_cell = inst_cell.second;
      debugPrint(logger_, RSZ, "journal", 1, "journal restore {} ({})",
                 network_->pathName(inst),
                 lib_cell->name());
      replaceCell(inst, lib_cell, false);
      resize_count_--;
    }
  }
  for (const Instance *buffer : inserted_buffers_) {
    debugPrint(logger_, RSZ, "journal", 1, "journal remove {}",
               network_->pathName(buffer));
    removeBuffer(const_cast<Instance*>(buffer));
    inserted_buffer_count_--;
  }
}

////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////

PinSet
Resizer::findFaninFanouts(PinSet *end_pins)
{
  // Abbreviated copyState
  db_network_ = sta_->getDbNetwork();
  sta_->ensureLevelized();
  graph_ = sta_->graph();

  VertexSet ends(graph_);
  for (const Pin *pin : *end_pins) {
    Vertex *end = graph_->pinLoadVertex(pin);
    ends.insert(end);
  }
  // Port adapter (P-29): bind the network (see init).
  PinSet fanin_fanout_pins(network_);
  VertexSet fanin_fanouts = findFaninFanouts(ends);
  for (Vertex *vertex : fanin_fanouts)
    fanin_fanout_pins.insert(vertex->pin());
  return fanin_fanout_pins;
}

VertexSet
Resizer::findFaninFanouts(VertexSet &ends)
{
  // Search backwards from ends to fanin register outputs and input ports.
  VertexSet fanin_roots = findFaninRoots(ends);
  // Search forward from register outputs.
  VertexSet fanouts = findFanouts(fanin_roots);
  return fanouts;
}

// Find roots for logic fanin of ends.
VertexSet
Resizer::findFaninRoots(VertexSet &ends)
{
  Search *search = sta_->search();
  NonRegSearchPred pred(sta_);
  BfsBkwdIterator iter(BfsIndex::other, &pred, this);
  for (Vertex *vertex : ends)
    iter.enqueueAdjacentVertices(vertex);

  VertexSet roots(graph_);
  while (iter.hasNext()) {
    Vertex *vertex = iter.next();
    if (isRegOutput(vertex)
        || network_->isTopLevelPort(vertex->pin()))
      roots.insert(vertex);
    else
      iter.enqueueAdjacentVertices(vertex);
  }
  return roots;
}

bool
Resizer::isRegOutput(Vertex *vertex)
{
  LibertyPort *port = network_->libertyPort(vertex->pin());
  if (port) {
    LibertyCell *cell = port->libertyCell();
    for (TimingArcSet *arc_set : cell->timingArcSets()) {
      if (arc_set->to() == port
          && arc_set->role()->genericRole() == TimingRole::regClkToQ())
        return true;
    }
  }
  return false;
}

VertexSet
Resizer::findFanouts(VertexSet &reg_outs)
{
  VertexSet fanouts(graph_);
  Search *search = sta_->search();
  NonLatchSearchPred pred(sta_);
  BfsFwdIterator iter(BfsIndex::other, &pred, this);
  for (Vertex *reg_out : reg_outs)
    iter.enqueueAdjacentVertices(reg_out);

  while (iter.hasNext()) {
    Vertex *vertex = iter.next();
    if (!isRegister(vertex)) {
      fanouts.insert(vertex);
      iter.enqueueAdjacentVertices(vertex);
    }
  }
  return fanouts;
}

bool
Resizer::isRegister(Vertex *vertex)
{
  LibertyPort *port = network_->libertyPort(vertex->pin());
  if (port) {
    LibertyCell *cell = port->libertyCell();
    return cell && cell->isSequential();
  }
  return false;
}

} // namespace
