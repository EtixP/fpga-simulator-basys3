#include "design/VerilatorJson.h"

#include <cstdlib>

namespace vb::design {
namespace {

// A forward-only JSON reader: walk the members you want, skip the rest.
class JsonCursor {
public:
  explicit JsonCursor(std::string_view text) : text_(text) {}

  bool failed() const { return failed_; }

  bool beginObject() { return begin('{'); }
  bool beginArray() { return begin('['); }

  // Next member of the current object; false (and leaves it) at its end.
  bool nextMember(std::string& key) {
    if (!nextItem('}')) return false;
    if (!readString(key)) return false;
    skipSpace();
    return expect(':');
  }

  // Next element of the current array; false (and leaves it) at its end.
  bool nextElement() { return nextItem(']'); }

  bool readString(std::string& out) {
    skipSpace();
    if (!expect('"')) return false;
    out.clear();
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') return true;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= text_.size()) break;
      const char escape = text_[pos_++];
      switch (escape) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned code = 0;
          if (!readHex4(code)) return fail();
          if (code >= 0xD800 && code < 0xDC00 && pos_ + 1 < text_.size() && text_[pos_] == '\\'
              && text_[pos_ + 1] == 'u') {
            pos_ += 2;
            unsigned low = 0;
            if (!readHex4(low) || low < 0xDC00 || low >= 0xE000) return fail();
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          }
          appendUtf8(out, code);
          break;
        }
        default:
          // Verilator's own escape for bytes outside ASCII: up to three
          // octal digits.
          if (escape >= '0' && escape <= '7') {
            unsigned byte = unsigned(escape - '0');
            for (int digits = 1; digits < 3 && pos_ < text_.size() && text_[pos_] >= '0'
                                 && text_[pos_] <= '7'; ++digits)
              byte = byte * 8 + unsigned(text_[pos_++] - '0');
            if (byte > 0xFF) return fail();
            out += char(byte);
            break;
          }
          return fail();
      }
    }
    return fail();
  }

  bool readNumber(double& out) {
    skipSpace();
    const std::size_t start = pos_;
    while (pos_ < text_.size() && std::string_view("+-0123456789.eE").find(text_[pos_]) != std::string_view::npos)
      ++pos_;
    if (start == pos_) return fail();
    const std::string number(text_.substr(start, pos_ - start));
    char* end = nullptr;
    out = std::strtod(number.c_str(), &end);
    return end == number.c_str() + number.size() ? true : fail();
  }

  // Skips one value of any depth. Containers are skipped by counting
  // brackets outside strings, without recursion: Verilator nests one level
  // per expression operand, so a long else-if chain is thousands deep.
  bool skipValue() {
    skipSpace();
    if (pos_ >= text_.size()) return fail();
    const char c = text_[pos_];
    if (c == '"') return skipString();
    if (c == '{' || c == '[') {
      std::size_t depth = 0;
      while (pos_ < text_.size()) {
        const char next = text_[pos_];
        if (next == '"') {
          if (!skipString()) return false;
          continue;
        }
        ++pos_;
        if (next == '{' || next == '[') ++depth;
        else if ((next == '}' || next == ']') && --depth == 0) return true;
      }
      return fail();
    }
    for (const std::string_view word : {"true", "false", "null"}) {
      if (text_.substr(pos_, word.size()) == word) {
        pos_ += word.size();
        return true;
      }
    }
    double ignored = 0;
    return readNumber(ignored);
  }

private:
  bool skipString() {
    if (!expect('"')) return false;
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') return true;
      if (c == '\\') ++pos_;
    }
    return fail();
  }

  bool begin(char open) {
    skipSpace();
    if (!expect(open)) return false;
    first_.push_back(true);
    return true;
  }

  bool nextItem(char close) {
    if (failed_ || first_.empty()) return false;
    skipSpace();
    if (pos_ < text_.size() && text_[pos_] == close) {
      ++pos_;
      first_.pop_back();
      return false;
    }
    if (!first_.back() && !expect(',')) return false;
    first_.back() = false;
    return true;
  }

  void skipSpace() {
    while (pos_ < text_.size() && std::string_view(" \t\r\n").find(text_[pos_]) != std::string_view::npos)
      ++pos_;
  }

  bool expect(char c) {
    if (pos_ < text_.size() && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return fail();
  }

  bool readHex4(unsigned& code) {
    if (pos_ + 4 > text_.size()) return false;
    code = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_++];
      code <<= 4;
      if (c >= '0' && c <= '9') code |= unsigned(c - '0');
      else if (c >= 'a' && c <= 'f') code |= unsigned(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') code |= unsigned(c - 'A' + 10);
      else return false;
    }
    return true;
  }

  static void appendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) {
      out += char(code);
    } else if (code < 0x800) {
      out += char(0xC0 | (code >> 6));
      out += char(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
      out += char(0xE0 | (code >> 12));
      out += char(0x80 | ((code >> 6) & 0x3F));
      out += char(0x80 | (code & 0x3F));
    } else {
      out += char(0xF0 | (code >> 18));
      out += char(0x80 | ((code >> 12) & 0x3F));
      out += char(0x80 | ((code >> 6) & 0x3F));
      out += char(0x80 | (code & 0x3F));
    }
  }

  bool fail() {
    failed_ = true;
    return false;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  std::vector<bool> first_;
  bool failed_ = false;
};

// A module's input ports: the VAR statements directly in its body with
// "direction":"INPUT".
bool readInputs(JsonCursor& json, std::vector<std::string>& inputs) {
  if (!json.beginArray()) return false;
  while (json.nextElement()) {
    std::string type, name, direction, field;
    if (!json.beginObject()) return false;
    while (json.nextMember(field)) {
      const bool ok = field == "type" ? json.readString(type)
          : field == "name" ? json.readString(name)
          : field == "direction" ? json.readString(direction)
          : json.skipValue();
      if (!ok) return false;
    }
    if (type == "VAR" && direction == "INPUT") inputs.push_back(name);
  }
  return !json.failed();
}
}  // namespace

std::optional<std::vector<TopModule>> topModules(std::string_view treeJson) {
  JsonCursor json(treeJson);
  std::vector<TopModule> tops;
  bool sawModules = false;
  std::string key;
  if (!json.beginObject()) return std::nullopt;
  while (json.nextMember(key)) {
    if (key != "modulesp") {
      if (!json.skipValue()) return std::nullopt;
      continue;
    }
    sawModules = true;
    if (!json.beginArray()) return std::nullopt;
    while (json.nextElement()) {
      TopModule module;
      double level = 0;
      if (!json.beginObject()) return std::nullopt;
      std::string field;
      while (json.nextMember(field)) {
        const bool ok = field == "name" ? json.readString(module.name)
            : field == "level" ? json.readNumber(level)
            : field == "stmtsp" ? readInputs(json, module.inputs)
            : json.skipValue();
        if (!ok) return std::nullopt;
      }
      if (level == 1 && !module.name.empty()) tops.push_back(std::move(module));
    }
  }
  if (json.failed() || !sawModules) return std::nullopt;
  return tops;
}

std::optional<std::vector<FileRead>> filesRead(std::string_view metaJson) {
  JsonCursor json(metaJson);
  std::vector<FileRead> files;
  bool sawFiles = false;
  std::string key;
  if (!json.beginObject()) return std::nullopt;
  while (json.nextMember(key)) {
    if (key != "files") {
      if (!json.skipValue()) return std::nullopt;
      continue;
    }
    sawFiles = true;
    if (!json.beginObject()) return std::nullopt;
    std::string id;
    while (json.nextMember(id)) {
      FileRead file;
      if (!json.beginObject()) return std::nullopt;
      std::string field;
      while (json.nextMember(field)) {
        const bool ok = field == "realpath" ? json.readString(file.real)
            : field == "filename" ? json.readString(file.found)
            : json.skipValue();
        if (!ok) return std::nullopt;
      }
      if (!file.real.empty() && file.real.front() != '<') files.push_back(std::move(file));
    }
  }
  if (json.failed() || !sawFiles) return std::nullopt;
  return files;
}

}  // namespace vb::design
