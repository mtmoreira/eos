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

// dbSta, OpenSTA on OpenDB

// Network edit function flow
// tcl edit cmd -> dbNetwork -> db -> dbStaCbk -> Sta
// Edits from tcl, Sta api and db edits are all supported.

#include "db_sta/dbSta.hh"

#include <algorithm>            // min
// No <tcl.h> here: Sta.hh forward-declares Tcl_Interp, and this
// file calls no Tcl C function after the M-3 adaptation, so the
// module builds with or without Tcl headers present.

#include "sta/StaMain.hh"
#include "sta/Graph.hh"
#include "sta/Clock.hh"
#include "sta/Sdc.hh"
#include "sta/Search.hh"
#include "sta/Path.hh"
#include "sta/PathExpanded.hh"
#include "sta/Bfs.hh"
#include "sta/EquivCells.hh"
#include "sta/ReportTcl.hh"

#include "odb/db.h"

#include "utl/Logger.h"


#include "db_sta/dbNetwork.hh"

#include "dbSdcNetwork.hh"


////////////////////////////////////////////////////////////////

namespace sta {

using std::min;

using utl::Logger;
using utl::STA;

class dbStaReport : public sta::ReportTcl
{
public:
  void setLogger(Logger *logger);
  // PORT (2026 STA): the base formats via std::format in template methods
  // and delivers through these virtual hooks. The 2021 printf-style
  // virtuals (warn/fileWarn/error/fileError/critical, vfile variants)
  // no longer exist on the base, and the base's printToBuffer/buffer_
  // members are gone, so the port overrides the hooks and routes to
  // utl::Logger. printString/printLine survive unchanged (still virtual
  // on the 2026 base).
  void warnMsg(int id,
               const std::string &msg) override;
  void fileWarnMsg(int id,
                   std::string_view filename,
                   int line,
                   const std::string &msg) override;
  void errorMsg(int id,
                const std::string &msg) override;
  void fileErrorMsg(int id,
                    std::string_view filename,
                    int line,
                    const std::string &msg) override;
  void criticalMsg(int id,
                   const std::string &msg) override;
  virtual size_t printString(const char *buffer,
                             size_t length);

protected:
  virtual void printLine(const char *line,
                         size_t length);

  Logger *logger_;
};

class dbStaCbk : public dbBlockCallBackObj
{
public:
  dbStaCbk(dbSta *sta,
           Logger *logger);
  void setNetwork(dbNetwork *network);
  virtual void inDbInstCreate(dbInst *inst) override;
  virtual void inDbInstDestroy(dbInst *inst) override;
  virtual void inDbInstSwapMasterBefore(dbInst *inst,
                                        dbMaster *master) override;
  virtual void inDbInstSwapMasterAfter(dbInst *inst) override;
  virtual void inDbNetDestroy(dbNet *net) override;
  void inDbITermPostConnect(dbITerm *iterm) override;
  void inDbITermPreDisconnect(dbITerm *iterm) override;
  void inDbITermDestroy(dbITerm *iterm) override;
  void inDbBTermPostConnect(dbBTerm *bterm) override;
  void inDbBTermPreDisconnect(dbBTerm *bterm) override;
  void inDbBTermDestroy(dbBTerm *bterm) override;

private:
  dbSta *sta_;
  dbNetwork *network_;
  Logger *logger_;
};



dbSta::dbSta() :
  Sta(),
  db_(nullptr),
  logger_(nullptr),
  db_network_(nullptr),
  db_report_(nullptr),
  db_cbk_(nullptr)
{
}

dbSta::~dbSta()
{
}

void
dbSta::init(Tcl_Interp *tcl_interp,
	    dbDatabase *db,
            Logger *logger)
{
  initSta();
  Sta::setSta(this);
  db_ = db;
  logger_ = logger;
  makeComponents();
  setTclInterp(tcl_interp);
  db_report_->setLogger(logger);
  db_cbk_ = new dbStaCbk(this, logger);
  // EOS adapter (M-3): no SWIG init and no encoded dbSta Tcl here.
  // dbSta.tcl is not imported (EOS drives via the C++ API plus
  // its own registry); the EOS session performs in-tree STA Tcl
  // init itself where a Tcl session is needed.
}

// Port adapter (P-32): wire the report logger for scratch stations
// (cellWireDelay) without full init, which would hijack the global Sta
// singleton via Sta::setSta.
void
dbSta::setReportLogger(Logger *logger)
{
  db_report_->setLogger(logger);
}

// Wrapper to init network db.
void
dbSta::makeComponents()
{
  Sta::makeComponents();
  // EOS adapter (M-3): bind the network through dbNetwork::init, the
  // merged module's single db+logger binding. On the main path init()
  // already set db_ and logger_. On the scratch path (cellWireDelay)
  // both are null and the network binds later via setBlock, which
  // takes db_ from the block.
  if (db_ && logger_) {
    db_network_->init(db_, logger_);
  }
}

////////////////////////////////////////////////////////////////

void
dbSta::makeReport()
{
  db_report_ = new dbStaReport();
  report_ = db_report_;
}

void
dbSta::makeNetwork()
{
  db_network_ = new class dbNetwork();
  network_ = db_network_;
}

void
dbSta::makeSdcNetwork()
{
  sdc_network_ = new dbSdcNetwork(network_);
}

void
dbSta::postReadLef(dbTech* tech,
                   dbLib* library)
{
  if (library) {
    db_network_->readLefAfter(library);
  }
}

void
dbSta::postReadDef(dbBlock* block)
{
  db_network_->readDefAfter(block);
  db_cbk_->addOwner(block);
  db_cbk_->setNetwork(db_network_);
}

void
dbSta::postReadDb(dbDatabase* db)
{
  db_network_->readDbAfter(db);
  odb::dbChip *chip = db_->getChip();
  if (chip) {
    odb::dbBlock *block = chip->getBlock();
    if (block) {
      db_cbk_->addOwner(block);
      db_cbk_->setNetwork(db_network_);
    }
  }
}

Slack
dbSta::netSlack(const dbNet *db_net,
		const MinMax *min_max)
{
  const Net *net = db_network_->dbToSta(db_net);
  return slack(net, min_max);
}

std::set<dbNet*>
dbSta::findClkNets()
{
  std::set<dbNet*> clk_nets;
  for (Mode *mode : modes()) {
    ensureClkNetwork(mode);
    for (Clock *clk : mode->sdc()->clocks()) {
      for (const Pin *pin : *pins(clk, mode)) {
        Net *net = network_->net(pin);
        if (net)
          clk_nets.insert(db_network_->staToDb(net));
      }
    }
  }
  return clk_nets;
}

std::set<dbNet*>
dbSta::findClkNets(const Clock *clk)
{
  std::set<dbNet*> clk_nets;
  for (Mode *mode : modes()) {
    ensureClkNetwork(mode);
    for (const Pin *pin : *pins(clk, mode)) {
    Net *net = network_->net(pin);      
    if (net)
      clk_nets.insert(db_network_->staToDb(net));
    }
  }
  return clk_nets;
}

////////////////////////////////////////////////////////////////

// Network edit functions.
// These override the default sta functions that call sta before/after
// functions because the db calls them via callbacks.

void
dbSta::deleteInstance(Instance *inst)
{
  NetworkEdit *network = networkCmdEdit();
  network->deleteInstance(inst);
}

void
dbSta::replaceCell(Instance *inst,
                   Cell *to_cell,
                   LibertyCell *to_lib_cell)
{
  NetworkEdit *network = networkCmdEdit();
  LibertyCell *from_lib_cell = network->libertyCell(inst);
  if (sta::equivCells(from_lib_cell, to_lib_cell)) {
    replaceEquivCellBefore(inst, to_lib_cell);
    network->replaceCell(inst, to_cell);
    replaceEquivCellAfter(inst);
  }
  else {
    replaceCellBefore(inst, to_lib_cell);
    network->replaceCell(inst, to_cell);
    replaceCellAfter(inst);
  }
}

void
dbSta::deleteNet(Net *net)
{
  NetworkEdit *network = networkCmdEdit();
  network->deleteNet(net);
}

void
dbSta::connectPin(Instance *inst,
                  Port *port,
                  Net *net)
{
  NetworkEdit *network = networkCmdEdit();
  network->connect(inst, port, net);
}

void
dbSta::connectPin(Instance *inst,
                  LibertyPort *port,
                  Net *net)
{
  NetworkEdit *network = networkCmdEdit();
  network->connect(inst, port, net);
}

void
dbSta::disconnectPin(Pin *pin)
{
  NetworkEdit *network = networkCmdEdit();
  network->disconnectPin(pin);
}

////////////////////////////////////////////////////////////////

void
dbStaReport::setLogger(Logger *logger)
{
  logger_ = logger;
}

// Line return \n is implicit.
void
dbStaReport::printLine(const char *buffer,
                       size_t length)
{
  logger_->report(buffer);
}

// Only used by encapsulated Tcl channels, ie puts and command prompt.
size_t
dbStaReport::printString(const char *buffer,
                         size_t length)
{
  if (buffer[length - 1] == '\n') {
    std::string buf(buffer);
    // Trim trailing \r\n.
    buf.erase(buf.find_last_not_of("\r\n") + 1);
    logger_->report(buf.c_str());
  }
  else
    // puts without a trailing \n in the string.
    // Tcl command prompts get here.
    // puts "xyz" makes a separate call for the '\n '.
    // This seems to be the only way to get the output.
    // It will not be logged.
    printConsole(buffer, length);
  return length;
}

void
dbStaReport::warnMsg(int id,
                     const std::string &msg)
{
  // Don't give std::format a chance to interpret the message.
  logger_->warn(STA, id, "{}", msg);
}

void
dbStaReport::fileWarnMsg(int id,
                         std::string_view filename,
                         int line,
                         const std::string &msg)
{
  logger_->warn(STA, id, "{} line {}, {}", filename, line, msg);
}

void
dbStaReport::errorMsg(int id,
                      const std::string &msg)
{
  logger_->error(STA, id, "{}", msg);
}

void
dbStaReport::fileErrorMsg(int id,
                          std::string_view filename,
                          int line,
                          const std::string &msg)
{
  logger_->error(STA, id, "{} line {}, {}", filename, line, msg);
}

void
dbStaReport::criticalMsg(int id,
                         const std::string &msg)
{
  logger_->critical(STA, id, "{}", msg);
}

////////////////////////////////////////////////////////////////
//
// OpenDB callbacks to notify OpenSTA of network edits
//
////////////////////////////////////////////////////////////////

dbStaCbk::dbStaCbk(dbSta *sta,
                   Logger *logger) :
  sta_(sta),
  network_(nullptr),  // not built yet
  logger_(logger)
{
}

void
dbStaCbk::setNetwork(dbNetwork *network)
{
  network_ = network;
}

void
dbStaCbk::inDbInstCreate(dbInst* inst)
{
  sta_->makeInstanceAfter(network_->dbToSta(inst));
}

void
dbStaCbk::inDbInstDestroy(dbInst *inst)
{
  // This is called after the iterms have been destroyed
  // so it side-steps Sta::deleteInstanceAfter.
  sta_->deleteLeafInstanceBefore(network_->dbToSta(inst));
}

void
dbStaCbk::inDbInstSwapMasterBefore(dbInst *inst,
                                   dbMaster *master)
{
  LibertyCell *to_lib_cell = network_->libertyCell(network_->dbToSta(master));
  LibertyCell *from_lib_cell = network_->libertyCell(inst);
  Instance *sta_inst = network_->dbToSta(inst);
  if (sta::equivCells(from_lib_cell, to_lib_cell))
    sta_->replaceEquivCellBefore(sta_inst, to_lib_cell);
  else
    logger_->error(STA, 1000, "instance {} swap master {} is not equivalent",
                   inst->getConstName(),
                   master->getConstName());
}

void
dbStaCbk::inDbInstSwapMasterAfter(dbInst *inst)
{
  sta_->replaceEquivCellAfter(network_->dbToSta(inst));
}

void
dbStaCbk::inDbNetDestroy(dbNet *db_net)
{
  Net *net = network_->dbToSta(db_net);
  sta_->deleteNetBefore(net);
  network_->deleteNetBefore(net);
}

void
dbStaCbk::inDbITermPostConnect(dbITerm *iterm)
{
  Pin *pin = network_->dbToSta(iterm);
  network_->connectPinAfter(pin);
  sta_->connectPinAfter(pin);
}

void
dbStaCbk::inDbITermPreDisconnect(dbITerm *iterm)
{
  Pin *pin = network_->dbToSta(iterm);
  sta_->disconnectPinBefore(pin);
  network_->disconnectPinBefore(pin);
}

void
dbStaCbk::inDbITermDestroy(dbITerm *iterm)
{
  sta_->deletePinBefore(network_->dbToSta(iterm));
}

void
dbStaCbk::inDbBTermPostConnect(dbBTerm *bterm)
{
  Pin *pin = network_->dbToSta(bterm);
  network_->connectPinAfter(pin);
  sta_->connectPinAfter(pin);
}

void
dbStaCbk::inDbBTermPreDisconnect(dbBTerm *bterm)
{
  Pin *pin = network_->dbToSta(bterm);
  sta_->disconnectPinBefore(pin);
  network_->disconnectPinBefore(pin);
}

void
dbStaCbk::inDbBTermDestroy(dbBTerm *bterm)
{
  sta_->deletePinBefore(network_->dbToSta(bterm));
}

////////////////////////////////////////////////////////////////

} // namespace sta
