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
#include "rsz/SteinerTree.hh"

#include "utl/Logger.h"

#include "sta/Report.hh"
#include "sta/Units.hh"
#include "sta/Sdc.hh"
#include "sta/Parasitics.hh"
#include "sta/ArcDelayCalc.hh"

namespace rsz {

using utl::RSZ;

using sta::PinSet;
using sta::Mode;
using sta::NetIterator;
using sta::NetConnectedPinIterator;
using sta::OperatingConditions;

using odb::dbInst;
using odb::dbMasterType;

////////////////////////////////////////////////////////////////

void
Resizer::setLayerRC(dbTechLayer *layer,
                    const Scene *scene,
                    double res,
                    double cap)
{
  if (layer_res_.empty()) {
    int layer_count = db_->getTech()->getLayerCount();
    int corner_count = sta_->scenes().size();
    layer_res_.resize(layer_count);
    layer_cap_.resize(layer_count);
    for (int i = 0; i < layer_count; i++) {
      layer_res_[i].resize(corner_count);
      layer_cap_[i].resize(corner_count);
    }
  }

  layer_res_[layer->getNumber()][scene->index()] = res;
  layer_cap_[layer->getNumber()][scene->index()] = cap;
}

void
Resizer::layerRC(dbTechLayer *layer,
                 const Scene *scene,
                 // Return values.
                 double &res,
                 double &cap)
{
  if (layer_res_.empty()) {
    res = 0.0;
    cap = 0.0;
  }
  else {
    res = layer_res_[layer->getNumber()][scene->index()];
    cap = layer_cap_[layer->getNumber()][scene->index()];
  }
}

////////////////////////////////////////////////////////////////

void
Resizer::setWireSignalRC(const Scene *scene,
                         double res,
                         double cap)
{
  wire_signal_res_.resize(sta_->scenes().size());
  wire_signal_cap_.resize(sta_->scenes().size());
  wire_signal_res_[scene->index()] = res;
  wire_signal_cap_[scene->index()] = cap;
}

double
Resizer::wireSignalResistance(const Scene *scene)
{
  if (wire_signal_res_.empty())
    return 0.0;
  else
    return wire_signal_res_[scene->index()];
}

double
Resizer::wireSignalCapacitance(const Scene *scene)
{
  if (wire_signal_cap_.empty())
    return 0.0;
  else
    return wire_signal_cap_[scene->index()];
}

void
Resizer::setWireClkRC(const Scene *scene,
                      double res,
                      double cap)
{
  wire_clk_res_.resize(sta_->scenes().size());
  wire_clk_cap_.resize(sta_->scenes().size());
  wire_clk_res_[scene->index()] = res;
  wire_clk_cap_[scene->index()] = cap;
}

double
Resizer::wireClkResistance(const Scene *scene)
{
  if (wire_clk_res_.empty())
    return 0.0;
  else
    return wire_clk_res_[scene->index()];
}

double
Resizer::wireClkCapacitance(const Scene *scene)
{
  if (wire_clk_cap_.empty())
    return 0.0;
  else
    return wire_clk_cap_[scene->index()];
}

////////////////////////////////////////////////////////////////

void
Resizer::ensureWireParasitics()
{
  if (have_estimated_parasitics_) {
    for (const Net *net : parasitics_invalid_)
      estimateWireParasitic(net);
    parasitics_invalid_.clear();
  }
  else
    estimateWireParasitics();
}

void
Resizer::ensureWireParasitic(const Pin *drvr_pin)
{
  const Net *net = network_->net(drvr_pin);
  if (net)
    ensureWireParasitic(drvr_pin, net);
}

void
Resizer::ensureWireParasitic(const Pin *drvr_pin,
                             const Net *net)
{
  // Sufficient to check for parasitic for one scene because
  // they are all made at the same time.
  const Scene *scene = sta_->scenes()[0];
  if (have_estimated_parasitics_
      && net
      && (parasitics_invalid_.count(net) != 0
          || scene->parasitics(max_)->findPiElmore(drvr_pin, RiseFall::rise(),
                                                   max_) == nullptr)) {
    estimateWireParasitic(drvr_pin, net);
    parasitics_invalid_.erase(net);
  }
}

void
Resizer::estimateWireParasitics()
{
  if (!wire_signal_cap_.empty()) {
    for (Mode *mode : sta_->modes())
      sta_->ensureClkNetwork(mode);
    // 2021 made separate parasitics per corner via setParasiticAnalysisPts.
    // 2026 STA holds parasitics per scene structurally, so no flag is needed.

    NetIterator *net_iter = network_->netIterator(network_->topInstance());
    while (net_iter->hasNext()) {
      Net *net = net_iter->next();
      estimateWireParasitic(net);
    }
    delete net_iter;
    have_estimated_parasitics_ = true;
    parasitics_invalid_.clear();
  }
}

void
Resizer::estimateWireParasitic(const Net *net)
{
  PinSet *drivers = network_->drivers(net);
  if (drivers && !drivers->empty()) {
    for (const Pin *drvr_pin : *drivers) {
      estimateWireParasitic(drvr_pin, net);
      break;
    }
  }
}

void
Resizer::estimateWireParasitic(const Pin *drvr_pin,
                               const Net *net)
{
  if (!network_->isPower(net)
      && !network_->isGround(net)) {
    if (isPadNet(net))
      // When an input port drives a pad instance with huge input
      // cap the elmore delay is gigantic. Annotate with zero
      // wire capacitance to prevent wireload model parasitics from being used.
      makePadParasitic(net);
    else
      estimateWireParasiticSteiner(drvr_pin, net);
  }
}

bool
Resizer::isPadNet(const Net *net) const
{
  const Pin *pin1, *pin2;
  net2Pins(net, pin1, pin2);
  return pin1 && pin2
    && ((network_->isTopLevelPort(pin1)
         && isPadPin(pin2))
        || (network_->isTopLevelPort(pin2)
            && isPadPin(pin1)));
}

void
Resizer::makePadParasitic(const Net *net)
{
  const Pin *pin1, *pin2;
  net2Pins(net, pin1, pin2);
  for (const Scene *scene : sta_->scenes()) {
    Parasitic *parasitic = sta_->makeParasiticNetwork(net, false, scene, max_);
    Parasitics *parasitics = scene->parasitics(max_);
    ParasiticNode *n1 = parasitics->ensureParasiticNode(parasitic, pin1, network_);
    ParasiticNode *n2 = parasitics->ensureParasiticNode(parasitic, pin2, network_);

    // Use a small resistor to keep the connectivity intact.
    uint32_t next_resistor_id = 0;
    parasitics->makeResistor(parasitic, next_resistor_id++, .001, n1, n2);
    // 2021 reduced eagerly to the delay calc's reduced type and then deleted
    // the per-ap network. 2026 delay calcs reduce lazily during delay
    // calculation, so the network is left in place; deleting it here would
    // remove the estimated parasitics.
  }
}

void
Resizer::estimateWireParasiticSteiner(const Pin *drvr_pin,
                                      const Net *net)
{
  SteinerTree *tree = makeSteinerTree(drvr_pin, routingAlpha(), false,
                                      db_network_, logger_);
  if (tree) {
    debugPrint(logger_, RSZ, "resizer_parasitics", 1, "estimate wire {}",
               sdc_network_->pathName(net));
    for (const Scene *scene : sta_->scenes()) {
      Parasitic *parasitic = sta_->makeParasiticNetwork(net, false, scene, max_);
      Parasitics *parasitics = scene->parasitics(max_);
      uint32_t next_resistor_id = 0;
      bool is_clk = sta_->isClock(net, scene->mode());
      double wire_cap=is_clk ? wireClkCapacitance(scene) : wireSignalCapacitance(scene);
      double wire_res=is_clk ? wireClkResistance(scene) : wireSignalResistance(scene);
      int branch_count = tree->branchCount();
      for (int i = 0; i < branch_count; i++) {
        Point pt1, pt2;
        SteinerPt steiner_pt1, steiner_pt2;
        int wire_length_dbu;
        tree->branch(i,
                     pt1, steiner_pt1,
                     pt2, steiner_pt2,
                     wire_length_dbu);
        ParasiticNode *n1 = parasitics->ensureParasiticNode(parasitic, net, steiner_pt1, network_);
        ParasiticNode *n2 = parasitics->ensureParasiticNode(parasitic, net, steiner_pt2, network_);
        if (wire_length_dbu == 0)
          // Use a small resistor to keep the connectivity intact.
          parasitics->makeResistor(parasitic, next_resistor_id++, 1.0e-3, n1, n2);
        else {
          double length = dbuToMeters(wire_length_dbu);
          double cap = length * wire_cap;
          double res = length * wire_res;
          // Make pi model for the wire.
          debugPrint(logger_, RSZ, "resizer_parasitics", 2,
                     " pi {} l={} c2={} rpi={} c1={} {}",
                     parasitics->name(n1),
                     units_->distanceUnit()->asString(length),
                     units_->capacitanceUnit()->asString(cap / 2.0),
                     units_->resistanceUnit()->asString(res),
                     units_->capacitanceUnit()->asString(cap / 2.0),
                     parasitics->name(n2));
          parasitics->incrCap(n1, cap / 2.0);
          parasitics->makeResistor(parasitic, next_resistor_id++, res, n1, n2);
          parasitics->incrCap(n2, cap / 2.0);
        }
        parasiticNodeConnectPins(parasitic, n1, tree, steiner_pt1, parasitics, next_resistor_id);
        parasiticNodeConnectPins(parasitic, n2, tree, steiner_pt2, parasitics, next_resistor_id);
      }
      // 2021 reduced eagerly to pi_elmore and then deleted the per-ap
      // network. 2026 delay calcs reduce lazily, so the network stays.
      (void) parasitic;
    }
    delete tree;
  }
}

void
Resizer::parasiticNodeConnectPins(Parasitic *parasitic,
                                  ParasiticNode *node,
                                  SteinerTree *tree,
                                  SteinerPt pt,
                                  Parasitics *parasitics,
                                  uint32_t &next_resistor_id)
{
  const PinSeq *pins = tree->pins(pt);
  if (pins) {
    for (const Pin *pin : *pins) {
      ParasiticNode *pin_node = parasitics->ensureParasiticNode(parasitic, pin, network_);
      // Use a small resistor to keep the connectivity intact.
      parasitics->makeResistor(parasitic, next_resistor_id++, 1.0e-3, node, pin_node);
    }
  }
}

void
Resizer::net2Pins(const Net *net,
                  const Pin *&pin1,
                  const Pin *&pin2)  const
{
  pin1 = nullptr;
  pin2 = nullptr;
  NetConnectedPinIterator *pin_iter = network_->connectedPinIterator(net);
  if (pin_iter->hasNext())
    pin1 = pin_iter->next();
  if (pin_iter->hasNext())
    pin2 = pin_iter->next();
  delete pin_iter;
}

bool
Resizer::isPadPin(const Pin *pin) const
{
  Instance *inst = network_->instance(pin);
  return inst
    && !network_->isTopInstance(inst)
    && isPad(inst);
}

bool
Resizer::isPad(const Instance *inst) const
{
  dbInst *db_inst = db_network_->staToDb(inst);
  dbMasterType type = db_inst->getMaster()->getType();
  // Use switch so if new types are added we get a compiler warning.
  switch (type) {
  case dbMasterType::CORE:
  case dbMasterType::CORE_ANTENNACELL:
  case dbMasterType::CORE_FEEDTHRU:
  case dbMasterType::CORE_TIEHIGH:
  case dbMasterType::CORE_TIELOW:
  case dbMasterType::CORE_WELLTAP:
  case dbMasterType::ENDCAP:
  case dbMasterType::ENDCAP_PRE:
  case dbMasterType::ENDCAP_POST:
  case dbMasterType::CORE_SPACER:
  case dbMasterType::BLOCK:
  case dbMasterType::BLOCK_BLACKBOX:
  case dbMasterType::BLOCK_SOFT:
  case dbMasterType::ENDCAP_TOPLEFT:
  case dbMasterType::ENDCAP_TOPRIGHT:
  case dbMasterType::ENDCAP_BOTTOMLEFT:
  case dbMasterType::ENDCAP_BOTTOMRIGHT:
  case dbMasterType::COVER:
  case dbMasterType::RING:
    return false;
  case dbMasterType::COVER_BUMP:
  case dbMasterType::PAD:
  case dbMasterType::PAD_AREAIO:
  case dbMasterType::PAD_INPUT:
  case dbMasterType::PAD_OUTPUT:
  case dbMasterType::PAD_INOUT:
  case dbMasterType::PAD_POWER:
  case dbMasterType::PAD_SPACER:
  case dbMasterType::NONE:
    return true;
  }
  // gcc warniing
  return false;
}

void
Resizer::parasiticsInvalid(const Net *net)
{
  if (have_estimated_parasitics_) {
    debugPrint(logger_, RSZ, "resizer_parasitics", 2, "parasitics invalid {}",
               network_->pathName(net));
    parasitics_invalid_.insert(net);
  }
}

void
Resizer::parasiticsInvalid(const dbNet *net)
{
  parasiticsInvalid(db_network_->dbToSta(net));
}

} // namespace
