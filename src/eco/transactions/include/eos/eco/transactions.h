// transactions: the ECO transaction engine (L4 engines).
//
// The Revised Plan, Section 9, names the only way an ECO may be
// applied: checkpoint; propose edit; update connectivity and STA;
// legalize; reroute affected nets; re-extract; check all required
// scenes; commit, or roll back to the checkpoint. This module is
// that protocol as code, for the v1 scope the Phase 2 queue sets:
//
// - The design checkpoint is a binary snapshot of the ODB database
//   (OdbDatabase::WriteDb) taken before the edit; rollback restores
//   it by replacing the caller's database with a fresh one read
//   back from the snapshot, so a rejected edit leaves the design
//   exactly as it was, instance for instance and net for net.
// - Acceptance is a full re-analysis of every required scene
//   through the L3 timing service, before and after the edit.
//   Until incremental extraction is qualified, that full
//   re-analysis is the acceptance path (Plan, Section 9). Scenes
//   at extracted parasitics are refused here: their SPEF describes
//   the pre-edit netlist, so an extracted-fidelity verdict after an
//   edit would be a claim about parasitics that no longer exist.
//   The closure loop (unit 2.5) keeps the same estimate-only rule:
//   extracted acceptance after edits stays deferred until
//   incremental extraction is qualified, because EOS has no
//   extraction engine to re-derive parasitics for an edited
//   netlist. Scenes run at estimate fidelity, and
//   EcoOutcome::parasitics_note says so in words, so a caller
//   registering a report binding for the outcome cannot mislabel
//   the fidelity it describes.
// - Legalize and reroute are not separate steps in v1: the v1
//   adapters (sizing, buffering) preserve connectivity by
//   construction, place inserted cells at their driver's location
//   (see timing-eco), and the designs in scope carry no routing to
//   redo. The steps join the protocol when routing exists in EOS.
//
// The verdict is the Plan, Section 8, rule stated for the four
// timing metrics the timing service reports per scene (setup and
// hold worst slack and total negative slack, all higher-is-better
// with the gate at zero): a candidate regresses when a metric that
// was clean crosses below zero, or a metric that was already
// violating gets worse. Consuming slack that stays at or above
// zero is not a regression; a setup gain that creates a hold
// failure is. A candidate is accepted only when no scene regresses
// and at least one scene's timing improves. Power and area are not
// in the v1 metric set, so the lexicographic order below timing
// has nothing to judge yet; that is stated here rather than
// implied.
//
// Electrical judging (unit F5, the bounded part of P2-4) sits
// immediately below timing in that lexicographic order. The
// timing service now also reports, per scene, the max
// transition, max capacitance and max fanout checks (worst value
// and violating-pin count). The exact rule: a candidate
// regresses when, in any scene and for any of the three checks,
// its violating-pin count is higher than the before side's
// count, or its count is the same nonzero count and its worst
// value is worse than the before side's worst value by more
// than the check's float-noise epsilon (1e-9 in the check's own
// unit). A check absent from either side is not judged (the
// service emits the metrics on every scene it produces, so
// real analyses always carry them on both sides). An
// electrical improvement never by itself accepts a candidate:
// acceptance still requires a timing improvement, and an
// electrical regression rejects a candidate whose timing
// improved. Power and area judging remain out of scope.
//
// The ledger wrap: a transaction in the L5 ledger spans engines,
// so the ledger is the caller's to drive (the structure document:
// L5 owns the ledger, L4 never decides validity). The caller
// begins a ledger transaction, calls RunEcoTransaction inside it,
// then commits the ledger transaction when the outcome is
// committed, or aborts it when the outcome is rejected: an aborted
// ledger transaction leaves no record, so a rejected ECO is as if
// never attempted in design and ledger alike. The engine records
// its before and after metrics in the outcome for exactly that
// ledger record.
//
// This header is a stable surface.
#ifndef EOS_ECO_TRANSACTIONS_H_
#define EOS_ECO_TRANSACTIONS_H_

#include <functional>
#include <string>
#include <vector>

#include "eos/analysis/timing.h"
#include "eos/db/odb.h"
#include "eos/foundation/errors.h"

namespace eos::eco {

using foundation::Result;
using foundation::Status;

// The all-scene verdict over one candidate, as judged by
// EvaluateAllScenes. scene_name and metric_name name the deciding
// scene and metric (the first regression, or the first
// improvement); reason is the same statement in words, with the
// measured values, suitable for a ledger record or a report.
struct EcoVerdict {
  bool accepted = false;
  bool regressed = false;
  bool improved = false;
  std::string scene_name;
  std::string metric_name;
  std::string reason;
};

// Judges before against after, scene by scene, under the rules in
// this header's banner. Scenes are matched by scenario name; the
// two lists must cover the same scenes and carry the same four
// timing metrics with the same units, or the call fails with
// kInvalidArgument: a verdict over mismatched inputs would be a
// guess, and the protocol does not guess.
Result<EcoVerdict> EvaluateAllScenes(
    const std::vector<analysis::SceneResult>& before,
    const std::vector<analysis::SceneResult>& after);

// One ECO to try: a label for the record, the SDC every scene is
// analyzed under, and the required scenes (all at estimate
// parasitics in v1, per the banner).
//
// known_before is an optional caller-supplied analysis of the
// design exactly as it stands at the call (unit 2.9): a caller
// that already holds a full TimingService analysis of the
// unchanged design (the closure loop holds one per iteration)
// may hand it in, and the transaction then skips its own
// before-analysis and judges against the supplied numbers. The
// analysis must cover exactly the request's scenes, by scenario
// name, and carry the four judged metrics per scene, or the
// call fails with kInvalidArgument before any checkpoint or
// edit. The contract is the caller's word that the design has
// not changed since the analysis ran: the engine cannot check
// that cheaply (checking is the analysis itself), so a caller
// that hands in numbers for a different design state gets a
// verdict about that state, and the header says so rather than
// implying a check ran. An empty known_before means the engine
// runs the before-analysis itself, as it always did.
struct EcoRequest {
  std::string label;
  std::string sdc_path;
  std::vector<analysis::SceneAnalysis> scenes;
  std::vector<analysis::SceneResult> known_before;
};

// The outcome of one transaction. committed is the protocol's
// decision; reason is the verdict's reason (or why the edit or the
// re-analysis failed, when the call returns an outcome for a
// failure the design could be rolled back from). before and after
// hold the full per-scene metrics the verdict was judged on, for
// the caller's ledger record. parasitics_note states the fidelity
// the after metrics were computed at, in words, for report
// bindings.
struct EcoOutcome {
  bool committed = false;
  std::string label;
  std::string reason;
  std::string parasitics_note;
  std::vector<analysis::SceneResult> before;
  std::vector<analysis::SceneResult> after;
};

// Runs one ECO transaction over database: checkpoint, analyze,
// apply the edit, re-analyze, judge, commit or roll back.
//
// The edit is a callback that mutates the database through the ECO
// adapters (or any facade mutation) and reports its own failure as
// a Status. An edit failure, a re-analysis failure, and a rejected
// verdict all roll the design back to the checkpoint; the first
// two are returned as the call's Status (with the design already
// restored), a rejected verdict is a normal protocol outcome
// returned as a value with committed false. A failure to take or
// restore the checkpoint is returned as a Status and the design is
// left in whatever state the failed restore reached, which the
// message says plainly.
Result<EcoOutcome> RunEcoTransaction(
    db::OdbDatabase& database, const EcoRequest& request,
    const std::function<Status(db::OdbDatabase&)>& edit);

}  // namespace eos::eco

#endif  // EOS_ECO_TRANSACTIONS_H_
