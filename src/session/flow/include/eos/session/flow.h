// flow: flow orchestration (L5 session). This module's first
// feature is the timing closure loop of the Revised Plan,
// Sections 8 and 17 (Phase 2 unit 2.5).
//
// The loop is the bounded search of Section 8, orchestrated from
// L5 because it sequences L4 engines under L5 state: diagnose the
// violating scenes, generate sizing and buffering candidates for
// the design, screen every candidate through the ECO transaction
// protocol (src/eco/transactions: checkpoint, edit, full
// re-analysis of every required scene, all-scene verdict, commit
// or roll back), keep only all-scene improvements, and stop at
// the timing gate or at the iteration bound. L5 sequences and
// judges here; the edits themselves are the L4 adapters in
// src/eco/timing-eco, exactly as the code structure document
// prescribes.
//
// Diagnosis, honestly scoped: the timing service reports the four
// per-scene metrics (setup and hold worst slack and total
// negative slack) and the STA facade exposes no per-endpoint path
// detail, so the loop cannot name the critical endpoint of a
// violating scene. Its diagnosis is therefore the violating scene
// and metric set, and its candidate generation is exhaustive over
// the editable design in a deterministic order (instances and
// nets sorted by name), screened one candidate at a time until
// the first all-scene improvement is found (first improvement,
// not best improvement: the search is bounded by candidate count
// times full re-analysis cost, and the record says which
// candidate was taken). A facade that reports worst endpoints
// would let a later version aim candidates at the violating
// cones first; the screening protocol would not change.
//
// Candidate knowledge: which drive variant is stronger is the
// caller's Liberty knowledge, as it is for the sizing adapter
// itself. The request therefore carries the sizing families
// (groups of masters with identical signal pin sets, ordered
// weakest to strongest) and the buffer masters to try, in the
// caller's order. A sizing candidate steps one instance one
// family step up; repeated iterations climb further. A buffering
// candidate splits one net whose fanout is at least two instance
// loads, whose driver is exactly one instance output pin and
// whose loads are all instance pins (the adapter's own scope);
// the first buffer master in the request is used for every net.
//
// The timing gate is the Section 8 gate over the metrics the
// timing service reports: in every required scene, worst setup
// slack at or above zero, setup total negative slack zero, worst
// hold slack at or above zero and hold total negative slack
// zero. The plan's gate also names max transition, capacitance
// and fanout violations; the facade exposes no such metrics in
// this version, so the gate covers the four reported metrics and
// this header says so rather than implying electrical checks ran.
//
// Ledger and reports: every accepted iteration is one ledger
// transaction, begun and committed by this service around the
// committed ECO outcome, recording the flattened per-scene before
// and after metrics (keys "<scene>/<metric>") and the edit as its
// label. A screened candidate the protocol rejects leaves no
// ledger record, by the ledger's own design (an aborted
// transaction is as if never attempted); rejections are counted
// in the outcome instead. When the loop ends, however it ends,
// it registers its own report binding, "timing_closure", at the
// final ledger revision: proof pass exactly when the gate is met,
// unknown otherwise, so a loop that did not close timing can
// never certify an export. If that name is already registered the
// registration failure is returned as the call's Status (the
// accepted edits and their ledger transactions stand; the design
// is never rolled back over a bookkeeping name).
//
// Fidelity: scenes at extracted parasitics are refused before
// any analysis or edit, with the transaction engine's reason:
// after an edit an extracted scene's SPEF describes a netlist
// that no longer exists, and EOS has no extraction engine to
// re-derive it. Closure runs at estimate fidelity, and the
// outcome's parasitics_note states that in words.
//
// This header is a stable surface.
#ifndef EOS_SESSION_FLOW_H_
#define EOS_SESSION_FLOW_H_

#include <cstddef>
#include <string>
#include <vector>

#include "eos/analysis/timing.h"
#include "eos/db/odb.h"
#include "eos/foundation/errors.h"
#include "eos/session/project.h"

namespace eos::session {

using foundation::Result;
using foundation::Status;

// Everything one closure run needs beyond the project and the
// design database.
struct CloseTimingRequest {
  // The SDC every scene is analyzed under.
  std::string sdc_path;
  // The required scenes, all at estimate parasitics (see the
  // banner on fidelity). At least one.
  std::vector<analysis::SceneAnalysis> scenes;
  // The iteration bound: at most this many accepted ECO steps.
  // Must be at least 1.
  std::size_t max_iterations = 1;
  // Sizing families: each inner list names masters that carry
  // identical signal pin sets, ordered weakest to strongest.
  // Instances already at a family's strongest member generate no
  // sizing candidate. May be empty (no sizing candidates).
  std::vector<std::vector<std::string>> sizing_families;
  // Buffer masters to insert, in preference order; the first is
  // used for every buffering candidate. May be empty (no
  // buffering candidates).
  std::vector<std::string> buffer_masters;
};

// One accepted iteration, as recorded in the ledger: the edit's
// label and the per-scene metrics before and after it.
struct ClosureStep {
  std::string label;
  std::vector<analysis::SceneResult> before;
  std::vector<analysis::SceneResult> after;
};

// The outcome of one closure run. closed is the timing gate over
// final (see the banner). initial holds the metrics the loop
// started from and final the metrics it ended at; steps holds one
// entry per accepted iteration, in order. candidates_tried counts
// every candidate screened through the ECO protocol (accepted
// and rejected); candidates_rejected counts the rejected ones.
// stop_reason states in words why the loop stopped, naming the
// deciding scene and metric where one decided it.
struct ClosureOutcome {
  bool closed = false;
  std::string stop_reason;
  std::string parasitics_note;
  std::vector<analysis::SceneResult> initial;
  std::vector<analysis::SceneResult> final;
  std::vector<ClosureStep> steps;
  std::size_t candidates_tried = 0;
  std::size_t candidates_rejected = 0;
};

// True when every scene meets the timing gate over the four
// reported metrics (see the banner). An empty scene list is not
// closed: there is nothing the gate was checked over.
bool TimingGateMet(const std::vector<analysis::SceneResult>& scenes);

// Runs the closure loop over database, inside project: the
// project's ledger records one transaction per accepted
// iteration and the project's report registry gains the loop's
// "timing_closure" binding at the end. Fails with
// kInvalidArgument on an empty scene list, an extracted scene, a
// zero iteration bound or a malformed sizing family (an empty
// family, a duplicate master, or masters whose signal pin sets
// differ), with kInvalidState when the database has no top
// block, and with the underlying Status when an analysis or a
// screening fails in a way the protocol cannot roll back from
// (a rejected verdict is an outcome, not a failure; see
// src/eco/transactions). On a Status failure after some
// iterations committed, those iterations stand: each was its own
// committed transaction, in design and ledger alike.
Result<ClosureOutcome> RunTimingClosure(Project& project,
                                        db::OdbDatabase& database,
                                        const CloseTimingRequest& request);

}  // namespace eos::session

#endif  // EOS_SESSION_FLOW_H_
