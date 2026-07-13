#include "constraints/Xdc.h"

#include <cctype>
#include <charconv>
#include <cstdio>
#include <sstream>

namespace vb {

namespace {

struct Token {
  enum Kind { Word, Braced, Bracketed } kind = Word;
  std::string text;  // Braced/Bracketed: inner content, delimiters stripped
};

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

// Strip comments ('#' outside braces/brackets — covers both whole-line
// comments and Digilent's trailing ";#Sch name = ..." style), a trailing
// command separator ';', CR, and surrounding whitespace.
std::string_view cleanLine(std::string_view line) {
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  int depth = 0;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '{' || c == '[') ++depth;
    else if (c == '}' || c == ']') --depth;
    else if (c == '#' && depth == 0) { line = line.substr(0, i); break; }
  }
  line = trim(line);
  while (!line.empty() && line.back() == ';') line = trim(line.substr(0, line.size() - 1));
  return line;
}

// Whitespace-separated tokens; {...} and [...] are single tokens (bracket
// nesting tracked so "[get_ports {sw[0]}]" stays one token).
std::optional<std::vector<Token>> tokenize(std::string_view line) {
  std::vector<Token> out;
  size_t i = 0;
  while (i < line.size()) {
    const char c = line[i];
    if (c == ' ' || c == '\t') { ++i; continue; }
    if (c == '{' || c == '[') {
      const char open = c, close = (c == '{') ? '}' : ']';
      int depth = 1;
      size_t j = i + 1;
      for (; j < line.size() && depth > 0; ++j) {
        if (line[j] == open) ++depth;
        else if (line[j] == close) --depth;
      }
      if (depth != 0) return std::nullopt;  // unbalanced
      out.push_back({c == '{' ? Token::Braced : Token::Bracketed,
                     std::string(trim(line.substr(i + 1, j - i - 2)))});
      i = j;
      continue;
    }
    size_t j = i;
    while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '{' &&
           line[j] != '[')
      ++j;
    out.push_back({Token::Word, std::string(line.substr(i, j - i))});
    i = j;
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
  if (!inner || inner->empty()) return t;
  const std::string& cmd = (*inner)[0].text;
  if (cmd == "current_design" && inner->size() == 1) {
    t.kind = Target::Design;
    return t;
  }
  if (cmd == "get_ports" && inner->size() == 2) {
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
  *ok = res.ec == std::errc{} && res.ptr == s.data() + s.size();
  return *ok ? v : fallback;
}

// Minimal stable formatting: "10", "10.5", "6.25". %.17g (max_digits10) so
// every double survives serialize -> parse exactly.
std::string formatDouble(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.17g", v);
  return buf;
}

// Brace-quote values that would not survive re-tokenization bare (empty or
// whitespace-containing), so writeXdc output always reparses to the same doc.
std::string quoted(const std::string& v) {
  if (v.empty() || v.find_first_of(" \t") != std::string::npos) return "{" + v + "}";
  return v;
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
  if (toks.size() == 4 && toks[1].kind == Token::Word && toks[1].text == "-dict" &&
      toks[2].kind == Token::Braced) {
    const auto inner = tokenize(toks[2].text);
    if (!inner || inner->size() % 2 != 0 || inner->empty()) {
      warn(doc, lineNo, "malformed -dict property list — skipped");
      return;
    }
    for (size_t i = 0; i < inner->size(); i += 2)
      props.emplace_back((*inner)[i].text, (*inner)[i + 1].text);
    targetIdx = 3;
  } else if (toks.size() == 4 && toks[1].kind == Token::Word) {
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
    if (t.kind == Token::Word && t.text == "-add") {
      cc.add = true;
    } else if (t.kind == Token::Word && t.text == "-name" && i + 1 < toks.size()) {
      cc.name = toks[++i].text;
    } else if (t.kind == Token::Word && t.text == "-period" && i + 1 < toks.size()) {
      bool ok = false;
      cc.periodNs = parseDoubleOr(toks[++i].text, 0.0, &ok);
      havePeriod = ok;
      if (!ok) {
        warn(doc, lineNo, "create_clock: bad -period value — clock skipped");
        return;
      }
    } else if (t.kind == Token::Word && t.text == "-waveform" && i + 1 < toks.size()) {
      const auto edges = tokenize(toks[++i].text);
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
      }
    } else {
      warn(doc, lineNo, "create_clock: unrecognized argument '" + t.text + "' — ignored");
    }
  }
  if (!havePeriod || !haveTarget) {
    warn(doc, lineNo, "create_clock without -period or [get_ports ...] — skipped");
    return;
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
  if (text.find_first_of(" \t") != std::string_view::npos) return std::nullopt;
  const size_t open = text.find('[');
  if (open == std::string_view::npos) {
    if (text.find(']') != std::string_view::npos) return std::nullopt;
    return PortRef{std::string(text), std::nullopt};
  }
  if (open == 0 || text.back() != ']') return std::nullopt;
  const std::string_view idx = text.substr(open + 1, text.size() - open - 2);
  uint32_t bit = 0;
  const auto res = std::from_chars(idx.data(), idx.data() + idx.size(), bit);
  if (res.ec != std::errc{} || res.ptr != idx.data() + idx.size())
    return std::nullopt;  // ranges ("15:0"), wildcards ("*"), empty
  return PortRef{std::string(text.substr(0, open)), bit};
}

XdcDoc parseXdc(std::string_view text) {
  XdcDoc doc;
  size_t lineNo = 0;
  while (!text.empty()) {
    ++lineNo;
    const size_t nl = text.find('\n');
    std::string_view rawLine = text.substr(0, nl);
    text = (nl == std::string_view::npos) ? std::string_view{} : text.substr(nl + 1);

    const std::string_view line = cleanLine(rawLine);
    if (line.empty()) continue;

    const auto toks = tokenize(line);
    if (!toks || toks->empty()) {
      warn(doc, lineNo, "unbalanced braces/brackets — skipped");
      continue;
    }
    const std::string& cmd = (*toks)[0].text;
    if (cmd == "set_property") {
      parseSetProperty(doc, lineNo, *toks);
    } else if (cmd == "create_clock") {
      parseCreateClock(doc, lineNo, *toks);
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
    for (const auto& [k, v] : p.extraProps) out << " " << k << " " << quoted(v);
    out << " } [get_ports {" << p.port.toString() << "}]\n";
  }
  for (const auto& c : doc.clocks) {
    out << "create_clock";
    if (c.add) out << " -add";
    if (c.name) out << " -name " << quoted(*c.name);
    out << " -period " << formatDouble(c.periodNs);
    if (c.waveformNs)
      out << " -waveform {" << formatDouble(c.waveformNs->first) << " "
          << formatDouble(c.waveformNs->second) << "}";
    out << " [get_ports {" << c.port.toString() << "}]\n";
  }
  for (const auto& [k, v] : doc.designProps)
    out << "set_property " << k << " " << quoted(v) << " [current_design]\n";
  return out.str();
}

bool semanticallyEqual(const XdcDoc& a, const XdcDoc& b) {
  return a.pins == b.pins && a.clocks == b.clocks && a.designProps == b.designProps;
}

}  // namespace vb
