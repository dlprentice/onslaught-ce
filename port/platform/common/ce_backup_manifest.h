// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ce_backup_name.h"
#include <cctype>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// The manifest is data. Its recorded Folder, Game and Original paths never name
// files to open.
namespace ce_preview {
struct Entry {
  std::string name, original, sha;
  uint32_t size = 0;
};
struct Manifest {
  std::string created, reason;
  std::vector<Entry> files;
};
inline bool digit(char c) { return c >= '0' && c <= '9'; }
inline char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; }
inline std::string fold(std::string s) {
  for (char &c : s)
    c = lower(c);
  return s;
}
inline bool safeName(const std::string &s) {
  if (s.empty() || s.size() > 200 || s.front() == '.' || s.back() == '.' ||
      s.back() == ' ' || CeBackupFileHasDirectionMark(s.data(), s.size()))
    return false;
  for (unsigned char c : s)
    if (c < 32 || c == 127 || std::strchr("/\\:*?\"<>|", c))
      return false;
  return true;
}
inline bool career(const std::string &s) {
  return s.size() > 4 && fold(s.substr(s.size() - 4)) == ".bes";
}
inline bool displayText(const std::string &s, size_t cap) {
  if (s.empty() || s.size() > cap ||
      CeBackupFileHasDirectionMark(s.data(), s.size()))
    return false;
  for (unsigned char c : s)
    if (c < 32 || c == 127)
      return false;
  return true;
}
class Parser {
  const std::string &s;
  size_t p = 0;
  void ws() {
    while (p < s.size() &&
           (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n'))
      ++p;
  }
  bool at(char c) {
    ws();
    if (p == s.size() || s[p] != c)
      return false;
    ++p;
    return true;
  }
  int hex() {
    if (p + 4 > s.size())
      return -1;
    int v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = lower(s[p++]);
      if (digit(c))
        v = v * 16 + c - '0';
      else if (c >= 'a' && c <= 'f')
        v = v * 16 + c - 'a' + 10;
      else
        return -1;
    }
    return v;
  }
  static void utf(std::string &v, unsigned n) {
    if (n < 128)
      v += char(n);
    else if (n < 2048) {
      v += char(192 | (n >> 6));
      v += char(128 | (n & 63));
    } else if (n < 65536) {
      v += char(224 | (n >> 12));
      v += char(128 | ((n >> 6) & 63));
      v += char(128 | (n & 63));
    } else {
      v += char(240 | (n >> 18));
      v += char(128 | ((n >> 12) & 63));
      v += char(128 | ((n >> 6) & 63));
      v += char(128 | (n & 63));
    }
  }
  bool str(std::string &v) {
    if (!at('"'))
      return false;
    v.clear();
    while (p < s.size() && v.size() <= 8192) {
      unsigned char c = s[p++];
      if (c == '"')
        return true;
      if (c < 32)
        return false;
      if (c != '\\') {
        if (c < 128) {
          v += char(c);
          continue;
        }
        int n = c >= 194 && c <= 223   ? 1
                : c >= 224 && c <= 239 ? 2
                : c >= 240 && c <= 244 ? 3
                                       : -1;
        if (n < 0 || size_t(n) > s.size() - p)
          return false;
        unsigned cp = c & ((1u << (6 - n)) - 1);
        for (int i = 0; i < n; ++i) {
          unsigned char next = s[p++];
          if ((next & 192) != 128)
            return false;
          cp = cp * 64 + (next & 63);
        }
        if ((n == 2 && cp < 2048) || (n == 3 && cp < 65536) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff))
          return false;
        utf(v, cp);
        continue;
      }
      if (p == s.size())
        return false;
      char e = s[p++];
      if (e == '"' || e == '\\' || e == '/')
        v += e;
      else if (e == 'u') {
        int a = hex();
        if (a < 0)
          return false;
        if (a >= 0xd800 && a <= 0xdbff) {
          if (p + 2 > s.size() || s[p++] != '\\' || s[p++] != 'u')
            return false;
          int b = hex();
          if (b < 0xdc00 || b > 0xdfff)
            return false;
          a = 0x10000 + ((a - 0xd800) << 10) + b - 0xdc00;
        } else if (a >= 0xdc00 && a <= 0xdfff)
          return false;
        if (a < 32)
          return false;
        utf(v, unsigned(a));
      } else
        return false;
    }
    return false;
  }
  bool num(uint32_t &v) {
    ws();
    size_t b = p;
    v = 0;
    while (p < s.size() && digit(s[p])) {
      if (p - b >= 8)
        return false;
      v = v * 10 + unsigned(s[p++] - '0');
    }
    return p > b && (p == b + 1 || s[b] != '0');
  }
  bool file(Entry &f) {
    if (!at('{'))
      return false;
    std::set<std::string> seen;
    do {
      std::string k;
      if (!str(k) || !seen.insert(k).second || !at(':'))
        return false;
      if (k == "Name") {
        if (!str(f.name) || !safeName(f.name))
          return false;
      } else if (k == "Original") {
        if (!str(f.original) || f.original.empty())
          return false;
      } else if (k == "Size") {
        if (!num(f.size) || f.size > 1024 * 1024)
          return false;
      } else if (k == "Sha256") {
        if (!str(f.sha) || f.sha.size() != 64)
          return false;
        for (char &c : f.sha) {
          c = lower(c);
          if (!digit(c) && (c < 'a' || c > 'f'))
            return false;
        }
      } else
        return false;
    } while (at(','));
    return at('}') && seen.size() == 4;
  }

public:
  explicit Parser(const std::string &src) : s(src) {}
  bool parse(Manifest &m) {
    if (s.empty() || s.size() > 65536 || !at('{'))
      return false;
    std::set<std::string> seen, names;
    std::string dummy;
    uint32_t total = 0;
    do {
      std::string k;
      if (!str(k) || !seen.insert(k).second || !at(':'))
        return false;
      if (k == "Folder" || k == "Game") {
        if (!str(dummy) || dummy.empty())
          return false;
      } else if (k == "Created") {
        if (!str(m.created) || !displayText(m.created, 64) ||
            m.created.size() < 10 || m.created[4] != '-' || m.created[7] != '-')
          return false;
        for (int pos : {0, 1, 2, 3, 5, 6, 8, 9})
          if (!digit(m.created[pos]))
            return false;
      } else if (k == "Reason") {
        ws();
        if (s.compare(p, 4, "null") == 0)
          p += 4;
        else if (!str(m.reason) || !displayText(m.reason, 200))
          return false;
      } else if (k == "Target") {
        uint32_t value;
        if (!num(value) || value > 3)
          return false;
      } else if (k == "Files") {
        if (!at('['))
          return false;
        do {
          Entry f;
          if (m.files.size() >= 32 || !file(f) ||
              !names.insert(fold(f.name)).second)
            return false;
          total += f.size;
          if (total > 2 * 1024 * 1024)
            return false;
          m.files.push_back(f);
        } while (at(','));
        if (!at(']'))
          return false;
      } else
        return false;
    } while (at(','));
    if (!at('}'))
      return false;
    ws();
    return p == s.size() && seen.count("Folder") && seen.count("Game") &&
           seen.count("Created") && seen.count("Files") && !m.files.empty();
  }
};
} // namespace ce_preview
