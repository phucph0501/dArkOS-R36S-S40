// UI language for the S40 launcher, the s40web browser and the on-screen keyboard.
// Source strings are Vietnamese; tl("...") returns the English or Chinese text from ui.tsv
// (columns: vietnamese \t english \t chinese, "\n" escaped) or the Vietnamese text itself.
// The language is read once per process from $HOME/.config/s40/lang ("vi", "en", "zh"),
// or S40_LANG for tests, so changing it means restarting the program (the launcher does that).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>

namespace s40tr {

inline std::string unescape(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char c = s[++i];
      o += c == 'n' ? '\n' : c == 't' ? '\t' : c;
    } else o += s[i];
  }
  return o;
}

inline std::string& lang() {
  static std::string l = [] {
    if (const char* e = getenv("S40_LANG")) return std::string(e);
    const char* home = getenv("HOME");
    std::ifstream f(std::string(home ? home : "/tmp") + "/.config/s40/lang");
    std::string v;
    if (f) std::getline(f, v);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\r' || v.back() == '\n')) v.pop_back();
    return v == "en" || v == "zh" ? v : std::string("vi");
  }();
  return l;
}

inline std::unordered_map<std::string, std::string>& table() {
  static std::unordered_map<std::string, std::string> t = [] {
    std::unordered_map<std::string, std::string> m;
    if (lang() == "vi") return m;
    int col = lang() == "en" ? 1 : 2;
    // static initialisers may call tr() before main() knows the assets dir: look in the usual places
    const char* env = getenv("S40_LANG_FILE");
    for (std::string p : {std::string(env ? env : ""), std::string("/opt/s40/assets/lang/ui.tsv"), std::string("assets/lang/ui.tsv"),
                          std::string("../s40launcher/assets/lang/ui.tsv")}) {
      if (p.empty()) continue;
      std::ifstream f(p);
      if (!f) continue;
      std::string line;
      while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::string cols[3];
        size_t a = 0;
        for (int c = 0; c < 3; c++) {
          size_t b = line.find('\t', a);
          cols[c] = line.substr(a, b == std::string::npos ? std::string::npos : b - a);
          if (b == std::string::npos) break;
          a = b + 1;
        }
        if (!cols[col].empty()) m[unescape(cols[0])] = unescape(cols[col]);
      }
      break;
    }
    return m;
  }();
  return t;
}

}  // namespace s40tr

// translated UI text (pointer stays valid for the whole run)
inline const char* tl(const char* s) {
  auto& t = s40tr::table();
  if (t.empty()) return s;
  auto it = t.find(s);
  return it == t.end() ? s : it->second.c_str();
}
