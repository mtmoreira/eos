// Copyright 2026 The EOS Authors
// SPDX-License-Identifier: Apache-2.0
//
// The scoped SDC clock parser. See the header for the subset
// contract; the shape is a tokenizer that splits the text into
// statements of tokens, then one small option walk per known
// command. Anything the subset cannot express is recorded in
// the unwired list, never dropped silently, and content that
// is not a clock statement at all is ignored by design.

#include "eos/io/sdc.h"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <utility>

namespace eos::io {
namespace {

struct Token {
  // Bare word: the word itself. Braced group: the raw content
  // between the braces. Bracket group: the whole "[...]" text,
  // wrappers kept. Quoted group: the content between quotes.
  std::string text;
  bool braced = false;
  bool bracket = false;
  // True when the token would need Tcl substitution ('$'
  // outside braces): the subset performs none.
  bool substituted = false;
};

struct Statement {
  int line = 0;
  std::vector<Token> tokens;
  bool substituted = false;
};

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

// Splits the whole text into statements. Never fails: an
// unterminated group simply runs to the end of the text.
std::vector<Statement> Tokenize(const std::string& text) {
  std::vector<Statement> out;
  Statement cur;
  bool cur_active = false;
  Token bare;
  bool bare_active = false;
  int line = 1;

  auto end_bare = [&]() {
    if (!bare_active) return;
    if (!cur_active) {
      cur_active = true;
      cur.line = line;
    }
    if (bare.substituted) cur.substituted = true;
    cur.tokens.push_back(std::move(bare));
    bare = Token();
    bare_active = false;
  };
  auto add_token = [&](Token token) {
    end_bare();
    if (!cur_active) {
      cur_active = true;
      cur.line = line;
    }
    if (token.substituted) cur.substituted = true;
    cur.tokens.push_back(std::move(token));
  };
  auto end_statement = [&]() {
    end_bare();
    if (cur_active) out.push_back(std::move(cur));
    cur = Statement();
    cur_active = false;
  };

  std::size_t i = 0;
  const std::size_t n = text.size();
  while (i < n) {
    const char c = text[i];
    // Backslash-newline is a line continuation everywhere in
    // the subset: it joins the statement and counts the line.
    if (c == '\\' && i + 1 < n && text[i + 1] == '\n') {
      end_bare();
      i += 2;
      ++line;
      continue;
    }
    if (c == '\n') {
      end_statement();
      ++line;
      ++i;
      continue;
    }
    if (c == ';') {
      end_statement();
      ++i;
      continue;
    }
    if (IsSpace(c)) {
      end_bare();
      ++i;
      continue;
    }
    // '#' at the start of a token comments out the rest of the
    // line; the newline itself still ends the statement.
    if (c == '#' && !bare_active) {
      while (i < n && text[i] != '\n') ++i;
      continue;
    }
    if (!bare_active && c == '{') {
      // Brace group: braces nest; a backslash shields the next
      // character from the brace count.
      std::size_t j = i + 1;
      int depth = 1;
      while (j < n && depth > 0) {
        const char d = text[j];
        if (d == '\\' && j + 1 < n) {
          if (text[j + 1] == '\n') ++line;
          j += 2;
          continue;
        }
        if (d == '\n') ++line;
        if (d == '{') ++depth;
        if (d == '}' && --depth == 0) break;
        ++j;
      }
      Token token;
      token.braced = true;
      token.text = text.substr(i + 1, (j < n ? j : n) - (i + 1));
      add_token(std::move(token));
      i = (j < n) ? j + 1 : n;
      continue;
    }
    if (!bare_active && c == '[') {
      // Bracket group: kept whole as one token. Brackets nest;
      // braces inside protect brackets from the count.
      std::size_t j = i + 1;
      int depth = 1;
      int braces = 0;
      while (j < n && depth > 0) {
        const char d = text[j];
        if (d == '\n') ++line;
        if (d == '{') ++braces;
        if (d == '}' && braces > 0) --braces;
        if (braces == 0) {
          if (d == '[') ++depth;
          if (d == ']' && --depth == 0) break;
        }
        ++j;
      }
      Token token;
      token.bracket = true;
      const std::size_t end = (j < n) ? j + 1 : n;
      token.text = text.substr(i, end - i);
      token.substituted = token.text.find('$') != std::string::npos;
      add_token(std::move(token));
      i = end;
      continue;
    }
    if (!bare_active && c == '"') {
      std::size_t j = i + 1;
      while (j < n && text[j] != '"' && text[j] != '\n') ++j;
      Token token;
      token.text = text.substr(i + 1, j - (i + 1));
      token.substituted = token.text.find('$') != std::string::npos;
      add_token(std::move(token));
      i = (j < n && text[j] == '"') ? j + 1 : j;
      continue;
    }
    // Bare word character.
    if (!bare_active) {
      bare = Token();
      bare_active = true;
    }
    if (c == '$') bare.substituted = true;
    bare.text += c;
    ++i;
  }
  end_statement();
  return out;
}

// Splits on whitespace runs.
std::vector<std::string> SplitWords(const std::string& s) {
  std::vector<std::string> out;
  std::string word;
  for (const char c : s) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!word.empty()) {
        out.push_back(word);
        word.clear();
      }
    } else {
      word += c;
    }
  }
  if (!word.empty()) out.push_back(word);
  return out;
}

// The words a token contributes where a list is expected: a
// braced group splits into its words; anything else is itself.
std::vector<std::string> WordsOf(const Token& token) {
  if (token.braced) return SplitWords(token.text);
  return {token.text};
}

// The single value an option token carries: a braced group's
// first word, or the token text itself.
std::string ValueOf(const Token& token) {
  const std::vector<std::string> words = WordsOf(token);
  return words.empty() ? std::string() : words.front();
}

bool ParseNumber(const std::string& text, double& out) {
  if (text.empty()) return false;
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0') return false;
  out = value;
  return true;
}

bool ParseInteger(const std::string& text, int& out) {
  if (text.empty()) return false;
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0') return false;
  out = static_cast<int>(value);
  return true;
}

bool IsOptionToken(const Token& token) {
  return !token.braced && !token.bracket && !token.text.empty() &&
         token.text.front() == '-';
}

// Walks one statement's tokens past the command word. Values
// are consumed through TakeValue; a missing value is reported
// as the caller's unwired reason.
class OptionWalk {
 public:
  OptionWalk(const Statement& statement, std::string command)
      : tokens_(statement.tokens), command_(std::move(command)) {}

  // The next token, or nullptr at the end of the statement.
  const Token* Next() {
    if (index_ >= tokens_.size()) return nullptr;
    return &tokens_[index_++];
  }

  // The value token for the option just read.
  const Token* TakeValue(const std::string& option, std::string& reason) {
    const Token* token = Next();
    if (token == nullptr) {
      reason = command_ + " option " + option + " needs a value";
    }
    return token;
  }

 private:
  const std::vector<Token>& tokens_;
  std::string command_;
  std::size_t index_ = 1;  // Token 0 is the command word.
};

std::optional<SdcUnwired> Unwired(int line, const std::string& statement,
                                  const std::string& reason) {
  return SdcUnwired{line, statement, reason};
}

std::optional<SdcUnwired> ParseCreateClock(const Statement& statement,
                                           SdcClockFile& out) {
  const std::string cmd = "create_clock";
  if (statement.substituted) {
    return Unwired(statement.line, cmd,
                   "uses Tcl substitution, outside the supported subset");
  }
  SdcClockDef def;
  bool has_name = false;
  bool has_period = false;
  OptionWalk walk(statement, cmd);
  while (const Token* token = walk.Next()) {
    if (IsOptionToken(*token)) {
      const std::string& opt = token->text;
      std::string reason;
      if (opt == "-name") {
        const Token* value = walk.TakeValue(opt, reason);
        if (value == nullptr) return Unwired(statement.line, cmd, reason);
        def.name = ValueOf(*value);
        has_name = true;
      } else if (opt == "-period") {
        const Token* value = walk.TakeValue(opt, reason);
        if (value == nullptr) return Unwired(statement.line, cmd, reason);
        if (!ParseNumber(ValueOf(*value), def.period_ns)) {
          return Unwired(statement.line, cmd,
                         "period is not a number: " + ValueOf(*value));
        }
        has_period = true;
      } else if (opt == "-waveform") {
        const Token* value = walk.TakeValue(opt, reason);
        if (value == nullptr) return Unwired(statement.line, cmd, reason);
        std::vector<std::string> edges;
        if (value->braced) {
          edges = SplitWords(value->text);
        } else {
          edges.push_back(value->text);
          const Token* second = walk.TakeValue(opt, reason);
          if (second == nullptr) {
            return Unwired(statement.line, cmd, reason);
          }
          edges.push_back(ValueOf(*second));
        }
        if (edges.size() != 2 ||
            !ParseNumber(edges[0], def.waveform_rise_ns) ||
            !ParseNumber(edges[1], def.waveform_fall_ns)) {
          return Unwired(statement.line, cmd,
                         "waveform is not a rise/fall pair of numbers");
        }
        def.has_waveform = true;
      } else if (opt == "-add") {
        def.add = true;
      } else {
        return Unwired(statement.line, cmd,
                       "unknown option " + opt + " on create_clock");
      }
      continue;
    }
    for (const std::string& word : WordsOf(*token)) {
      def.targets.push_back(word);
    }
  }
  if (!has_period) {
    return Unwired(statement.line, cmd, "create_clock has no -period");
  }
  if (!has_name) {
    if (def.targets.size() == 1) {
      def.name = def.targets.front();
    } else if (def.targets.empty()) {
      return Unwired(statement.line, cmd,
                     "create_clock has no target and no -name");
    } else {
      return Unwired(statement.line, cmd,
                     "create_clock with several targets needs -name");
    }
  }
  out.clocks.push_back(std::move(def));
  return std::nullopt;
}

std::optional<SdcUnwired> ParseCreateGeneratedClock(
    const Statement& statement, SdcClockFile& out) {
  const std::string cmd = "create_generated_clock";
  if (statement.substituted) {
    return Unwired(statement.line, cmd,
                   "uses Tcl substitution, outside the supported subset");
  }
  SdcGeneratedClockDef def;
  bool has_name = false;
  bool has_source = false;
  OptionWalk walk(statement, cmd);
  while (const Token* token = walk.Next()) {
    if (IsOptionToken(*token)) {
      const std::string& opt = token->text;
      std::string reason;
      if (opt == "-edges") {
        return Unwired(statement.line, cmd,
                       "-edges has no divide/multiply form in this subset");
      }
      if (opt == "-name") {
        const Token* value = walk.TakeValue(opt, reason);
        if (value == nullptr) return Unwired(statement.line, cmd, reason);
        def.name = ValueOf(*value);
        has_name = true;
      } else if (opt == "-source") {
        const Token* value = walk.TakeValue(opt, reason);
        if (value == nullptr) return Unwired(statement.line, cmd, reason);
        def.source = ValueOf(*value);
        has_source = true;
      } else if (opt == "-master_clock") {
        const Token* value = walk.TakeValue(opt, reason);
        if (value == nullptr) return Unwired(statement.line, cmd, reason);
        def.master_clock = ValueOf(*value);
        def.has_master_clock = true;
      } else if (opt == "-divide_by") {
        const Token* value = walk.TakeValue(opt, reason);
        if (value == nullptr) return Unwired(statement.line, cmd, reason);
        if (!ParseInteger(ValueOf(*value), def.divide_by)) {
          return Unwired(statement.line, cmd,
                         "-divide_by is not an integer: " + ValueOf(*value));
        }
      } else if (opt == "-multiply_by") {
        const Token* value = walk.TakeValue(opt, reason);
        if (value == nullptr) return Unwired(statement.line, cmd, reason);
        if (!ParseInteger(ValueOf(*value), def.multiply_by)) {
          return Unwired(statement.line, cmd,
                         "-multiply_by is not an integer: " +
                             ValueOf(*value));
        }
      } else if (opt == "-invert") {
        def.invert = true;
      } else if (opt == "-combinational") {
        def.combinational = true;
      } else if (opt == "-add") {
        def.add = true;
      } else {
        return Unwired(statement.line, cmd,
                       "unknown option " + opt +
                           " on create_generated_clock");
      }
      continue;
    }
    for (const std::string& word : WordsOf(*token)) {
      def.targets.push_back(word);
    }
  }
  if (!has_source && !def.combinational) {
    return Unwired(statement.line, cmd,
                   "create_generated_clock has no -source");
  }
  if (!has_name) {
    if (def.targets.size() == 1) {
      def.name = def.targets.front();
    } else if (def.targets.empty()) {
      return Unwired(statement.line, cmd,
                     "create_generated_clock has no target and no -name");
    } else {
      return Unwired(statement.line, cmd,
                     "create_generated_clock with several targets needs "
                     "-name");
    }
  }
  out.generated.push_back(std::move(def));
  return std::nullopt;
}

// True when a target token addresses objects other than
// clocks (a get_pins/get_ports/get_cells/get_nets wrapper).
bool TargetsNonClockObjects(const Token& token) {
  if (!token.bracket) return false;
  const std::vector<std::string> words = SplitWords(token.text);
  if (words.size() < 2) return false;
  return words[0] == "[get_pins" || words[0] == "[get_ports" ||
         words[0] == "[get_cells" || words[0] == "[get_nets";
}

std::optional<SdcUnwired> ParseSetClockUncertainty(const Statement& statement,
                                                   SdcClockFile& out) {
  const std::string cmd = "set_clock_uncertainty";
  if (statement.substituted) {
    return Unwired(statement.line, cmd,
                   "uses Tcl substitution, outside the supported subset");
  }
  SdcUncertaintyDef def;
  OptionWalk walk(statement, cmd);
  while (const Token* token = walk.Next()) {
    if (IsOptionToken(*token)) {
      const std::string& opt = token->text;
      std::string reason;
      if (opt == "-rise" || opt == "-fall" || opt == "-min" ||
          opt == "-max") {
        return Unwired(statement.line, cmd,
                       opt + " refinement is not a per-clock value");
      }
      if (opt == "-from" || opt == "-to") {
        return Unwired(statement.line, cmd,
                       "inter-clock uncertainty is not a per-clock value");
      }
      if (opt == "-setup") {
        const Token* value = walk.TakeValue(opt, reason);
        if (value == nullptr) return Unwired(statement.line, cmd, reason);
        if (!ParseNumber(ValueOf(*value), def.setup_ns)) {
          return Unwired(statement.line, cmd,
                         "-setup is not a number: " + ValueOf(*value));
        }
        def.has_setup = true;
      } else if (opt == "-hold") {
        const Token* value = walk.TakeValue(opt, reason);
        if (value == nullptr) return Unwired(statement.line, cmd, reason);
        if (!ParseNumber(ValueOf(*value), def.hold_ns)) {
          return Unwired(statement.line, cmd,
                         "-hold is not a number: " + ValueOf(*value));
        }
        def.has_hold = true;
      } else {
        return Unwired(statement.line, cmd,
                       "unknown option " + opt +
                           " on set_clock_uncertainty");
      }
      continue;
    }
    if (TargetsNonClockObjects(*token)) {
      return Unwired(statement.line, cmd,
                     "uncertainty targeted at pins or ports rather than "
                     "clocks");
    }
    for (const std::string& word : WordsOf(*token)) {
      def.targets.push_back(word);
    }
  }
  if (!def.has_setup && !def.has_hold) {
    return Unwired(statement.line, cmd,
                   "set_clock_uncertainty has no -setup or -hold value");
  }
  out.uncertainties.push_back(std::move(def));
  return std::nullopt;
}

}  // namespace

foundation::Result<SdcClockFile> ParseSdcClocks(const std::string& text) {
  SdcClockFile out;
  for (const Statement& statement : Tokenize(text)) {
    if (statement.tokens.empty()) continue;
    const Token& head = statement.tokens.front();
    if (head.braced || head.bracket) continue;
    std::optional<SdcUnwired> unwired;
    if (head.text == "create_clock") {
      unwired = ParseCreateClock(statement, out);
    } else if (head.text == "create_generated_clock") {
      unwired = ParseCreateGeneratedClock(statement, out);
    } else if (head.text == "set_clock_uncertainty") {
      unwired = ParseSetClockUncertainty(statement, out);
    } else {
      continue;  // Not a clock statement: ignored entirely.
    }
    if (unwired.has_value()) out.unwired.push_back(std::move(*unwired));
  }
  return out;
}

}  // namespace eos::io
