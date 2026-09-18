#include "constraints/Xdc.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace vb {

namespace {

struct Token {
  enum Kind { Word, Braced, Bracketed } kind = Word;
  std::string text;  // Braced/Bracketed: inner content, delimiters stripped
  bool literal = true;
};

bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                            c == '\f' || c == '\v'; }

std::string_view trim(std::string_view s) {
  while (!s.empty() && space(s.front())) s.remove_prefix(1);
  while (!s.empty() && space(s.back())) s.remove_suffix(1);
  return s;
}

// Backslash-newline substitution precedes Tcl tokenization, including in
// braces and comments. Keep source line numbers for useful diagnostics.
struct Source {
  std::string text;
  std::vector<size_t> lines;
};

Source continued(std::string_view input) {
  Source out;
  size_t line = 1;
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '\\' && i + 1 < input.size()) {
      size_t nl = i + 1;
      if (input[nl] == '\r' && nl + 1 < input.size()) ++nl;
      if (input[nl] == '\n') {
        out.text += ' ';
        out.lines.push_back(line++);
        i = nl;
        while (i + 1 < input.size() && (input[i + 1] == ' ' || input[i + 1] == '\t')) ++i;
        continue;
      }
      // An escaped backslash cannot itself start a continuation.
      out.text += input[i];
      out.lines.push_back(line);
      out.text += input[++i];
      out.lines.push_back(line);
      continue;
    }
    out.text += input[i];
    out.lines.push_back(line);
    if (input[i] == '\n') ++line;
  }
  return out;
}

// Find a brace/bracket group's end without evaluating its contents. Braces
// make brackets literal; quoted strings keep embedded ']' inside the group.
std::optional<size_t> groupEnd(std::string_view text, size_t start) {
  std::vector<char> stack{ text[start] };
  for (size_t i = start + 1; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\\' && i + 1 < text.size()) { ++i; continue; }
    const char top = stack.back();
    if (top == '{') {
      if (c == '{') stack.push_back(c);
      else if (c == '}') stack.pop_back();
    } else if (top == '"') {
      if (c == '"') stack.pop_back();
    } else {
      if (c == '[' || c == '{' || c == '"') stack.push_back(c);
      else if (c == ']') stack.pop_back();
    }
    if (stack.empty()) return i + 1;
  }
  return std::nullopt;
}

bool escaped(std::string_view text, size_t& pos, Token& tok) {
  if (++pos == text.size()) return false;
  const char c = text[pos++];
  switch (c) {
    case 'n': tok.text += '\n'; break;
    case 'r': tok.text += '\r'; break;
    case 't': tok.text += '\t'; break;
    case 'f': tok.text += '\f'; break;
    case 'v': tok.text += '\v'; break;
    case 'a': tok.text += '\a'; break;
    case 'b': tok.text += '\b'; break;
    default:
      // Numeric/Unicode Tcl escapes are outside the literal subset. Reject
      // instead of silently decoding them as the wrong value.
      if (c == 'x' || c == 'u' || c == 'U' || (c >= '0' && c <= '7'))
        tok.literal = false;
      tok.text += c;
  }
  return true;
}

// A literal word, a braced word, or an unevaluated bracket target. listMode
// is Tcl list parsing (-dict/-waveform): $ and [] are literal in a list.
std::optional<Token> readToken(std::string_view text, size_t& pos, bool listMode = false) {
  Token tok;
  const char first = text[pos];
  if (first == '{' || (first == '[' && !listMode)) {
    const auto end = groupEnd(text, pos);
    if (!end) return std::nullopt;
    tok.kind = first == '{' ? Token::Braced : Token::Bracketed;
    tok.text = text.substr(pos + 1, *end - pos - 2);
    pos = *end;
    if (pos < text.size() && !space(text[pos]) && (listMode || text[pos] != ';'))
      return std::nullopt;
    return tok;
  }
  const bool quotedWord = first == '"';
  if (quotedWord) ++pos;
  bool closed = !quotedWord;
  while (pos < text.size()) {
    const char c = text[pos];
    if (quotedWord && c == '"') { ++pos; closed = true; break; }
    if (!quotedWord && (space(c) || (!listMode && c == ';'))) break;
    if (c == '\\') {
      if (!escaped(text, pos, tok)) return std::nullopt;
      continue;
    }
    if (!listMode && (c == '$' || c == '[')) {
      tok.literal = false;
      if (c == '[') {
        const auto end = groupEnd(text, pos);
        if (!end) return std::nullopt;
        tok.text += text.substr(pos, *end - pos);
        pos = *end;
        continue;
      }
    }
    tok.text += c;
    ++pos;
  }
  if (!closed || (quotedWord && pos < text.size() && !space(text[pos]) &&
                  (listMode || text[pos] != ';'))) return std::nullopt;
  return tok;
}

std::optional<std::vector<Token>> tokenize(std::string_view text, bool listMode = false) {
  std::vector<Token> out;
  for (size_t pos = 0; pos < text.size();) {
    if (space(text[pos])) { ++pos; continue; }
    if (!listMode && text[pos] == ';') return std::nullopt;
    const auto tok = readToken(text, pos, listMode);
    if (!tok || !tok->literal) return std::nullopt;
    out.push_back(*tok);
  }
  return out;
}

// Target of a set_property / create_clock: "[get_ports {sw[0]}]" or
// "[current_design]".
struct Target {
  enum Kind { Ports, Design, Unknown } kind = Unknown;
  PortRef port;
};

Target parseTarget(const Token& tok) {
  Target t;
  if (tok.kind != Token::Bracketed) return t;
  const auto inner = tokenize(tok.text);
  if (!inner || inner->empty() || (*inner)[0].kind == Token::Bracketed) return t;
  const std::string& cmd = (*inner)[0].text;
  if (cmd == "current_design" && inner->size() == 1) {
    t.kind = Target::Design;
    return t;
  }
  if (cmd == "get_ports" && inner->size() == 2) {
    if ((*inner)[1].kind == Token::Bracketed) return t;
    if (auto ref = PortRef::parse((*inner)[1].text)) {
      t.kind = Target::Ports;
      t.port = *ref;
    }
    return t;
  }
  return t;
}

double parseDoubleOr(const std::string& s, double fallback, bool* ok) {
  double v = fallback;
  const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
  *ok = res.ec == std::errc{} && res.ptr == s.data() + s.size() && std::isfinite(v);
  return *ok ? v : fallback;
}

// Minimal stable formatting: "10", "10.5", "6.25". %.17g (max_digits10) so
// every double survives serialize -> parse exactly.
std::string formatDouble(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.17g", v);
  return buf;
}

// Double quotes with escapes work even for unmatched braces or a trailing
// backslash, where naively wrapping the value in braces is not sufficient.
std::string quoted(const std::string& v) {
  if (!v.empty() && v.find_first_of(" \t\r\n\f\v{}[]$;\\\"#") == std::string::npos) return v;
  std::string out = "\"";
  for (const char c : v) {
    switch (c) {
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\f': out += "\\f"; break;
      case '\v': out += "\\v"; break;
      case '\\': case '"': case '$': case '[': case ']': case '{': case '}':
        out += '\\'; out += c; break;
      default: out += c;
    }
  }
  return out + '"';
}

PinConstraint& pinFor(XdcDoc& doc, const PortRef& ref) {
  for (auto& p : doc.pins)
    if (p.port == ref) return p;
  doc.pins.push_back(PinConstraint{ref, {}, {}, {}});
  return doc.pins.back();
}

void applyPinProp(PinConstraint& pc, const std::string& key, const std::string& value) {
  if (key == "PACKAGE_PIN") { pc.packagePin = value; return; }
  if (key == "IOSTANDARD") { pc.iostandard = value; return; }
  for (auto& [k, v] : pc.extraProps)
    if (k == key) { v = value; return; }  // last-write-wins
  pc.extraProps.emplace_back(key, value);
}

void applyDesignProp(XdcDoc& doc, const std::string& key, const std::string& value) {
  for (auto& [k, v] : doc.designProps)
    if (k == key) { v = value; return; }
  doc.designProps.emplace_back(key, value);
}

void warn(XdcDoc& doc, size_t lineNo, const std::string& msg) {
  doc.warnings.push_back("line " + std::to_string(lineNo) + ": " + msg);
}

void parseSetProperty(XdcDoc& doc, size_t lineNo, const std::vector<Token>& toks) {
  // Forms: set_property -dict { K V ... } [target]
  //        set_property KEY VALUE [target]
  std::vector<std::pair<std::string, std::string>> props;
  size_t targetIdx = 0;
  if (toks.size() == 4 && toks[1].kind != Token::Bracketed && toks[1].text == "-dict" &&
      toks[2].kind != Token::Bracketed) {
    const auto inner = tokenize(toks[2].text, true);
    if (!inner || inner->size() % 2 != 0 || inner->empty()) {
      warn(doc, lineNo, "malformed -dict property list — skipped");
      return;
    }
    for (size_t i = 0; i < inner->size(); i += 2)
      props.emplace_back((*inner)[i].text, (*inner)[i + 1].text);
    targetIdx = 3;
  } else if (toks.size() == 4 && toks[1].kind != Token::Bracketed &&
             toks[2].kind != Token::Bracketed) {
    props.emplace_back(toks[1].text, toks[2].text);
    targetIdx = 3;
  } else {
    warn(doc, lineNo, "unrecognized set_property form — skipped");
    return;
  }

  const Target target = parseTarget(toks[targetIdx]);
  switch (target.kind) {
    case Target::Ports: {
      PinConstraint& pc = pinFor(doc, target.port);
      for (const auto& [k, v] : props) applyPinProp(pc, k, v);
      break;
    }
    case Target::Design:
      for (const auto& [k, v] : props) applyDesignProp(doc, k, v);
      break;
    case Target::Unknown:
      warn(doc, lineNo, "unsupported set_property target '" + toks[targetIdx].text +
                            "' — skipped");
      break;
  }
}

void parseCreateClock(XdcDoc& doc, size_t lineNo, const std::vector<Token>& toks) {
  ClockConstraint cc;
  bool havePeriod = false, haveTarget = false;
  for (size_t i = 1; i < toks.size(); ++i) {
    const Token& t = toks[i];
    if (t.kind != Token::Bracketed && t.text == "-add") {
      cc.add = true;
    } else if (t.kind != Token::Bracketed && t.text == "-name" && i + 1 < toks.size()) {
      if (toks[i + 1].kind == Token::Bracketed) {
        warn(doc, lineNo, "create_clock: unsupported Tcl substitution — skipped");
        return;
      }
      cc.name = toks[++i].text;
    } else if (t.kind != Token::Bracketed && t.text == "-period" && i + 1 < toks.size()) {
      if (toks[i + 1].kind == Token::Bracketed) {
        warn(doc, lineNo, "create_clock: unsupported Tcl substitution — skipped");
        return;
      }
      bool ok = false;
      cc.periodNs = parseDoubleOr(toks[++i].text, 0.0, &ok);
      havePeriod = ok;
      if (!ok || cc.periodNs <= 0) {
        warn(doc, lineNo, "create_clock: bad -period value — clock skipped");
        return;
      }
    } else if (t.kind != Token::Bracketed && t.text == "-waveform" && i + 1 < toks.size()) {
      if (toks[i + 1].kind == Token::Bracketed) {
        warn(doc, lineNo, "create_clock: unsupported Tcl substitution — skipped");
        return;
      }
      cc.waveformNs.reset();
      const auto edges = tokenize(toks[++i].text, true);
      if (edges && edges->size() == 2) {
        bool ok1 = false, ok2 = false;
        const double a = parseDoubleOr((*edges)[0].text, 0.0, &ok1);
        const double b = parseDoubleOr((*edges)[1].text, 0.0, &ok2);
        if (ok1 && ok2) cc.waveformNs = {a, b};
      }
      if (!cc.waveformNs)
        warn(doc, lineNo, "create_clock: unsupported -waveform (need exactly two "
                          "numeric edges) — waveform dropped, clock kept");
    } else if (t.kind == Token::Bracketed) {
      const Target target = parseTarget(t);
      if (target.kind == Target::Ports) {
        cc.port = target.port;
        haveTarget = true;
      } else {
        warn(doc, lineNo, "create_clock: unsupported target — skipped");
        return;
      }
    } else {
      warn(doc, lineNo, "create_clock: unrecognized argument '" + t.text + "' — ignored");
    }
  }
  if (!havePeriod || !haveTarget) {
    warn(doc, lineNo, "create_clock without -period or [get_ports ...] — skipped");
    return;
  }
  if (cc.waveformNs) {
    const auto [rise, fall] = *cc.waveformNs;
    // Vivado explicitly permits the falling edge to precede the rising
    // edge. Support either order, with distinct edges within one period.
    if (rise < 0 || fall < 0 || rise >= cc.periodNs || fall >= cc.periodNs || rise == fall) {
      warn(doc, lineNo, "create_clock: waveform edges must be distinct and within one "
                        "period — waveform dropped, clock kept");
      cc.waveformNs.reset();
    }
  }
  doc.clocks.push_back(std::move(cc));
}

}  // namespace

std::string PortRef::toString() const {
  return bit ? port + "[" + std::to_string(*bit) + "]" : port;
}

std::optional<PortRef> PortRef::parse(std::string_view text) {
  text = trim(text);
  if (text.empty()) return std::nullopt;
  // Verilog port names have no whitespace; a spaced argument is a multi-port
  // Tcl list ("{btnU btnD}"), which we do not support — reject so the caller
  // warn-skips instead of storing a bogus one-port constraint.
  if (text.find_first_of(" \t\r\n\f\v*?") != std::string_view::npos)
    return std::nullopt;
  const size_t open = text.find('[');
  if (open == std::string_view::npos) {
    if (text.find(']') != std::string_view::npos) return std::nullopt;
    return PortRef{std::string(text), std::nullopt};
  }
  if (open == 0 || text.back() != ']') return std::nullopt;
  const std::string_view idx = text.substr(open + 1, text.size() - open - 2);
  int32_t bit = 0;
  const auto res = std::from_chars(idx.data(), idx.data() + idx.size(), bit);
  if (res.ec != std::errc{} || res.ptr != idx.data() + idx.size())
    return std::nullopt;  // ranges ("15:0"), wildcards ("*"), empty
  return PortRef{std::string(text.substr(0, open)), bit};
}

XdcDoc parseXdc(std::string_view text) {
  XdcDoc doc;
  const Source source = continued(text);
  text = source.text;
  for (size_t pos = 0; pos < text.size();) {
    if (space(text[pos]) || text[pos] == ';') { ++pos; continue; }
    // '#' is a comment only at the beginning of a Tcl command.
    if (text[pos] == '#') {
      const size_t nl = text.find('\n', pos);
      pos = nl == std::string_view::npos ? text.size() : nl + 1;
      continue;
    }
    const size_t start = pos;
    const size_t lineNo = source.lines[start];
    std::vector<Token> toks;
    bool malformed = false, literal = true;
    while (pos < text.size() && text[pos] != '\n' && text[pos] != ';') {
      if (space(text[pos])) { ++pos; continue; }
      auto tok = readToken(text, pos);
      if (!tok) { malformed = true; break; }
      literal = literal && tok->literal;
      toks.push_back(std::move(*tok));
    }
    if (malformed) {
      warn(doc, lineNo, "malformed braces/brackets/quotes — skipped");
      // An unfinished group can consume later lines while searching for a
      // close delimiter. Recover at the next physical line, preserving the
      // existing warn-and-skip behavior for good constraints after typos.
      const size_t nl = text.find('\n', start);
      pos = nl == std::string_view::npos ? text.size() : nl + 1;
      continue;
    }
    if (!literal || toks.empty() || toks[0].kind == Token::Bracketed) {
      warn(doc, lineNo, "unsupported Tcl substitution or escape — skipped");
      continue;
    }
    const std::string& cmd = toks[0].text;
    if (cmd == "set_property") {
      parseSetProperty(doc, lineNo, toks);
    } else if (cmd == "create_clock") {
      parseCreateClock(doc, lineNo, toks);
    } else {
      warn(doc, lineNo, "unknown command '" + cmd + "' — skipped");
    }
  }
  return doc;
}

std::string writeXdc(const XdcDoc& doc) {
  std::ostringstream out;
  for (const auto& p : doc.pins) {
    out << "set_property -dict {";
    if (p.packagePin) out << " PACKAGE_PIN " << quoted(*p.packagePin);
    if (p.iostandard) out << " IOSTANDARD " << quoted(*p.iostandard);
    for (const auto& [k, v] : p.extraProps) out << " " << quoted(k) << " " << quoted(v);
    out << " } [get_ports " << quoted(p.port.toString()) << "]\n";
  }
  for (const auto& c : doc.clocks) {
    out << "create_clock";
    if (c.add) out << " -add";
    if (c.name) out << " -name " << quoted(*c.name);
    out << " -period " << formatDouble(c.periodNs);
    if (c.waveformNs)
      out << " -waveform {" << formatDouble(c.waveformNs->first) << " "
          << formatDouble(c.waveformNs->second) << "}";
    out << " [get_ports " << quoted(c.port.toString()) << "]\n";
  }
  for (const auto& [k, v] : doc.designProps)
    out << "set_property " << quoted(k) << " " << quoted(v) << " [current_design]\n";
  return out.str();
}

bool semanticallyEqual(const XdcDoc& a, const XdcDoc& b) {
  return a.pins == b.pins && a.clocks == b.clocks && a.designProps == b.designProps;
}

}  // namespace vb
