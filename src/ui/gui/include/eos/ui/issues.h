// issues: the GUI's elaboration issues model (L6 interface,
// Qt-free).
//
// The model the issues panel reads: the structured diagnostics
// from a synth_issues payload, severity ordered. Parsing is
// all-or-nothing like the other models: a malformed payload
// leaves the previous issues untouched and reports false.
#ifndef EOS_UI_ISSUES_H_
#define EOS_UI_ISSUES_H_

#include <string>
#include <vector>

namespace eos::ui {

// One elaboration diagnostic: severity "error", "warning", or
// "info"; code names the check that produced it; message is
// the human text; module is the owning module; path is the
// hierarchy path of that module's instance (the top module's
// path for a top-level issue); target is the cell or wire
// inside the module, empty for a module-level issue; file and
// line are the source location when the design records one,
// empty and 0 otherwise.
struct GuiIssue {
  std::string severity;
  std::string code;
  std::string message;
  std::string module;
  std::string path;
  std::string target;
  std::string file;
  int line = 0;
};

class IssuesModel {
 public:
  // Parses a synth_issues payload: a "checked=<human list>"
  // line, a "netlist=elaborated|generic" line, then one
  // "issue severity=<s> code=<c> module=<m> path=<p>
  // target=<t> file=<f> line=<n> message=<free text>" line
  // per issue. The message runs to end of line and may
  // contain spaces; every other field is a single token.
  // Returns false (issues unchanged) on any malformed line,
  // unknown key, missing checked or netlist line, unknown
  // severity or netlist value, empty checked line, empty
  // code, module, path, or message, or a negative or
  // non-numeric line number. An empty target or file is
  // legal and renders as an empty value. A successful load
  // replaces the previous issues and sorts them by severity
  // (error, warning, info), then display path, then code.
  bool LoadPayload(const std::string& payload);
  void Clear();

  bool Loaded() const { return loaded_; }
  const std::string& Checked() const { return checked_; }
  const std::string& Netlist() const { return netlist_; }
  const std::vector<GuiIssue>& Issues() const { return issues_; }
  int ErrorCount() const;
  int WarningCount() const;
  int InfoCount() const;
  // The display path of an issue: its hierarchy path, plus
  // "/target" when the issue names a cell or wire.
  static std::string DisplayPath(const GuiIssue& issue);

 private:
  bool loaded_ = false;
  std::string checked_;
  std::string netlist_;
  std::vector<GuiIssue> issues_;
};

}  // namespace eos::ui

#endif  // EOS_UI_ISSUES_H_
