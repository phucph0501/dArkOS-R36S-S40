// s40kb - the on-screen keyboard engine shared by the S40 launcher and the s40web browser.
// Header-only, no UI toolkit: layouts, cursor movement, Vietnamese (Telex / VNI), English,
// Chinese Pinyin with a candidate bar, symbol pages, and the settings both apps read
// ($HOME/.config/s40kb.cfg). Each app draws keyboard.rows() / candidates() in its own style.
#pragma once
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "s40tr.h"  // tl(): key labels in the UI language

namespace s40kb {

// ---------------------------------------------------------------- UTF-8
inline std::u32string toU32(const std::string& s) {
  std::u32string o;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    char32_t cp; int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; }
    else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; }
    else { cp = c & 0x07; n = 4; }
    for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3f);
    o += cp; i += n;
  }
  return o;
}
inline std::string toU8(const std::u32string& s) {
  std::string o;
  for (char32_t c : s) {
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xc0 | (c >> 6)); o += (char)(0x80 | (c & 0x3f)); }
    else if (c < 0x10000) { o += (char)(0xe0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3f)); o += (char)(0x80 | (c & 0x3f)); }
    else { o += (char)(0xf0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3f)); o += (char)(0x80 | ((c >> 6) & 0x3f)); o += (char)(0x80 | (c & 0x3f)); }
  }
  return o;
}

// ---------------------------------------------------------------- settings
struct Config {
  bool vi = true, en = true, zh = false;  // languages offered by the language key
  std::string lang = "vi";                // current / default language
  std::string viMethod = "telex";         // telex | vni
  bool newToneStyle = false;              // true: hoà, thuý (new) / false: hòa, thúy (classic)
  static std::string path() {
    const char* h = getenv("HOME");
    return std::string(h ? h : "/tmp") + "/.config/s40kb.cfg";
  }
  void load() {
    std::ifstream f(path());
    std::string l;
    while (std::getline(f, l)) {
      size_t e = l.find('=');
      if (e == std::string::npos) continue;
      std::string k = l.substr(0, e), v = l.substr(e + 1);
      if (k == "vi") vi = v == "1"; else if (k == "en") en = v == "1"; else if (k == "zh") zh = v == "1";
      else if (k == "lang") lang = v; else if (k == "vimethod") viMethod = v; else if (k == "newtone") newToneStyle = v == "1";
    }
    if (!vi && !en && !zh) en = true;
    if (!enabled(lang)) lang = vi ? "vi" : en ? "en" : "zh";
  }
  void save() const {
    std::string dir = path().substr(0, path().rfind('/'));
    ::mkdir(dir.c_str(), 0755);  // ~/.config may not exist yet
    std::ofstream f(path(), std::ios::trunc);
    f << "vi=" << vi << "\nen=" << en << "\nzh=" << zh << "\nlang=" << lang << "\nvimethod=" << viMethod << "\nnewtone=" << newToneStyle << "\n";
  }
  bool enabled(const std::string& l) const { return (l == "vi" && vi) || (l == "en" && en) || (l == "zh" && zh); }
  std::vector<std::string> langs() const {
    std::vector<std::string> v;
    if (vi) v.push_back("vi");
    if (en) v.push_back("en");
    if (zh) v.push_back("zh");
    return v;
  }
};
inline std::string langLabel(const std::string& l) { return l == "vi" ? "VI" : l == "zh" ? "中" : "EN"; }
inline std::string langName(const std::string& l) { return l == "vi" ? tl("Tiếng Việt") : l == "zh" ? "中文 (Pinyin)" : "English"; }

// ---------------------------------------------------------------- Vietnamese
namespace vn {
// base vowels x tones (none, sắc, huyền, hỏi, ngã, nặng)
static const char32_t* LOW[12] = {U"aáàảãạ", U"ăắằẳẵặ", U"âấầẩẫậ", U"eéèẻẽẹ", U"êếềểễệ", U"iíìỉĩị",
                                  U"oóòỏõọ", U"ôốồổỗộ", U"ơớờởỡợ", U"uúùủũụ", U"ưứừửữự", U"yýỳỷỹỵ"};
static const char32_t* UP[12] = {U"AÁÀẢÃẠ", U"ĂẮẰẲẴẶ", U"ÂẤẦẨẪẬ", U"EÉÈẺẼẸ", U"ÊẾỀỂỄỆ", U"IÍÌỈĨỊ",
                                 U"OÓÒỎÕỌ", U"ÔỐỒỔỖỘ", U"ƠỚỜỞỠỢ", U"UÚÙỦŨỤ", U"ƯỨỪỬỮỰ", U"YÝỲỶỸỴ"};
enum Base { A, AB, AC, E, EC, I, O, OC, OH, U, UH, Y };  // AB = ă, AC = â, EC = ê, OC = ô, OH = ơ, UH = ư
struct V { int base = -1, tone = 0; bool up = false; };
inline V info(char32_t c) {
  for (int b = 0; b < 12; b++)
    for (int t = 0; t < 6; t++) {
      if (LOW[b][t] == c) return {b, t, false};
      if (UP[b][t] == c) return {b, t, true};
    }
  return {};
}
inline char32_t make(int base, int tone, bool up) { return (up ? UP : LOW)[base][tone]; }
inline bool isVowel(char32_t c) { return info(c).base >= 0; }
inline bool isLetter(char32_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == U'đ' || c == U'Đ' || isVowel(c);
}
inline int family(int b) { return b <= AC ? A : b <= EC ? E : b == I ? I : b <= OH ? O : b <= UH ? U : Y; }

// the vowel that carries the tone mark (Vietnamese orthography rules)
inline int tonePos(const std::u32string& w, bool newStyle) {
  int n = (int)w.size(), start = 0;
  auto low = [&](int i) { char32_t c = w[i]; return (c >= 'A' && c <= 'Z') ? (char32_t)(c + 32) : c; };
  // "qu" and "gi" (+ vowel): the u / i belongs to the consonant
  if (n >= 2 && low(0) == 'q' && info(w[1]).base >= 0 && family(info(w[1]).base) == U) start = 2;
  else if (n >= 3 && low(0) == 'g' && info(w[1]).base == I && isVowel(w[2])) start = 2;
  int a = -1, b = -1;
  for (int i = start; i < n; i++) {
    if (isVowel(w[i])) { if (a < 0) a = i; b = i; }
    else if (a >= 0) break;
  }
  if (a < 0) {  // "gi" alone (gì), "qu..." without other vowel
    for (int i = 0; i < n; i++) if (isVowel(w[i])) return i;
    return -1;
  }
  for (int i = b; i >= a; i--) if (info(w[i]).base == OH) return i;  // ươ -> ơ
  for (int i = a; i <= b; i++) { int bs = info(w[i]).base; if (bs == AB || bs == AC || bs == EC || bs == OC || bs == UH) return i; }
  int len = b - a + 1;
  if (len == 1) return a;
  if (len >= 3) return a + 1;
  bool closed = b + 1 < n;  // consonant after the vowels
  if (closed) return b;
  int f1 = family(info(w[a]).base), f2 = family(info(w[b]).base);
  bool special = (f1 == O && (f2 == A || f2 == E)) || (f1 == U && f2 == Y);  // oa, oe, uy
  return special && newStyle ? b : a;
}
inline int currentTone(const std::u32string& w) {
  for (char32_t c : w) { V v = info(c); if (v.base >= 0 && v.tone) return v.tone; }
  return 0;
}
inline void setTone(std::u32string& w, int tone, bool newStyle) {
  for (auto& c : w) { V v = info(c); if (v.base >= 0) c = make(v.base, 0, v.up); }
  if (!tone) return;
  int p = tonePos(w, newStyle);
  if (p >= 0) { V v = info(w[p]); w[p] = make(v.base, tone, v.up); }
}
// change the shape of the last vowel of a family (circumflex / breve / horn); returns false if none
inline bool reshape(std::u32string& w, int fam, int target, bool& undone) {
  undone = false;
  for (int i = (int)w.size() - 1; i >= 0; i--) {
    V v = info(w[i]);
    if (v.base < 0 || family(v.base) != fam) continue;
    if (v.base == target) { w[i] = make(fam, v.tone, v.up); undone = true; return true; }  // pressed twice: undo
    w[i] = make(target, v.tone, v.up);
    return true;
  }
  return false;
}
// "uo" + horn -> "ươ" when something follows (người, được, tương); at the end of the syllable only
// the o takes the horn (thuở, huơ), so leave that case to the plain reshape
inline bool hornPair(std::u32string& w) {
  for (int i = (int)w.size() - 3; i >= 0; i--) {
    V u = info(w[i]), o = info(w[i + 1]);
    if (u.base >= 0 && o.base >= 0 && family(u.base) == U && family(o.base) == O && (u.base != UH || o.base != OH)) {
      w[i] = make(UH, u.tone, u.up);
      w[i + 1] = make(OH, o.tone, o.up);
      return true;
    }
  }
  return false;
}
// "uơ" followed by more letters becomes "ươ" (thuở stays, but tươi / người / được); not after q (quở)
inline void fixUo(std::u32string& w) {
  for (size_t i = 0; i + 2 < w.size(); i++) {
    V u = info(w[i]), o = info(w[i + 1]);
    if (u.base == U && o.base == OH && !(i > 0 && (w[i - 1] == 'q' || w[i - 1] == 'Q')) && isLetter(w[i + 2]))
      w[i] = make(UH, u.tone, u.up);
  }
}
// Process one key for the word at the end of the text. Returns true if the key was consumed as a
// modifier (the word was changed), false if it should simply be appended.
inline bool apply(std::u32string& w, char32_t key, bool vni, bool newStyle) {
  if (w.empty()) {
    if (!vni && (key == 'w' || key == 'W')) { w += key == 'W' ? U'Ư' : U'ư'; return true; }
    return false;
  }
  char32_t k = (key >= 'A' && key <= 'Z') ? key + 32 : key;
  int tone = -1;
  if (vni) { if (k >= '1' && k <= '5') tone = (int)(k - '0'); else if (k == '0') tone = 0; }
  else { const char* tk = "zsfrxj"; for (int i = 0; i < 6; i++) if ((char32_t)tk[i] == k) tone = i; }
  bool hasVowel = std::any_of(w.begin(), w.end(), isVowel);
  if (tone >= 0) {
    if (!hasVowel) return false;
    int cur = currentTone(w);
    if (tone == 0) { if (!cur) return false; setTone(w, 0, newStyle); return true; }
    if (cur == tone) { setTone(w, 0, newStyle); return false; }  // pressed twice: undo, key typed literally
    setTone(w, tone, newStyle);
    return true;
  }
  int cur = currentTone(w);
  bool changed = false, undone = false;
  if ((vni && k == '9') || (!vni && k == 'd')) {
    char32_t& f = w[0];
    if (f == 'd' || f == 'D') { f = f == 'D' ? U'Đ' : U'đ'; return true; }
    if (!vni && (f == U'đ' || f == U'Đ') && w.size() == 1) { f = f == U'Đ' ? 'D' : 'd'; return false; }
    return false;
  }
  if (vni) {
    if (k == '6') changed = reshape(w, A, AC, undone) || reshape(w, E, EC, undone) || reshape(w, O, OC, undone);
    else if (k == '7') changed = hornPair(w) || reshape(w, O, OH, undone) || reshape(w, U, UH, undone);
    else if (k == '8') changed = reshape(w, A, AB, undone);
    if (!changed) return false;
    setTone(w, cur, newStyle);
    return !undone;
  }
  if (k == 'a') changed = reshape(w, A, AC, undone);
  else if (k == 'e') changed = reshape(w, E, EC, undone);
  else if (k == 'o') changed = reshape(w, O, OC, undone);
  else if (k == 'w') {
    changed = hornPair(w) || reshape(w, A, AB, undone) || reshape(w, O, OH, undone) || reshape(w, U, UH, undone);
    if (!changed && !hasVowel) { w += key == 'W' ? U'Ư' : U'ư'; changed = true; }
  } else return false;
  if (!changed) return false;
  setTone(w, cur, newStyle);
  return !undone;  // undo: the key itself is then appended
}
}  // namespace vn

// ---------------------------------------------------------------- Chinese Pinyin
struct Pinyin {
  std::map<std::string, std::u32string> table;  // syllable -> chars, most frequent first
  bool loaded = false;
  void load(const std::string& path) {
    if (loaded) return;
    loaded = true;
    std::ifstream f(path);
    std::string l;
    while (std::getline(f, l)) {
      if (l.empty() || l[0] == '#') continue;
      size_t t = l.find('\t');
      if (t != std::string::npos) table[l.substr(0, t)] = toU32(l.substr(t + 1));
    }
  }
  // longest syllable at the start of comp (greedy segmentation of "nihao" -> "ni")
  std::string head(const std::string& comp) const {
    for (size_t n = std::min<size_t>(6, comp.size()); n > 0; n--)
      if (table.count(comp.substr(0, n))) return comp.substr(0, n);
    return "";
  }
  std::vector<std::string> candidates(const std::string& comp, size_t max = 60) const {
    std::vector<std::string> out;
    if (comp.empty()) return out;
    std::string h = head(comp);
    std::u32string seen;
    auto add = [&](const std::u32string& cs, size_t lim) {
      for (size_t i = 0; i < cs.size() && i < lim && out.size() < max; i++)
        if (seen.find(cs[i]) == std::u32string::npos) { seen += cs[i]; out.push_back(toU8(std::u32string(1, cs[i]))); }
    };
    if (!h.empty() && h.size() == comp.size()) add(table.at(h), max);
    // syllables still being typed: "zh" -> zhong, zhe, zhi... (best few of each)
    if (h.size() != comp.size() || out.size() < 8)
      for (auto it = table.lower_bound(comp); it != table.end() && it->first.compare(0, comp.size(), comp) == 0; ++it) add(it->second, 3);
    if (!h.empty() && h.size() != comp.size()) add(table.at(h), max);
    return out;
  }
};

// ---------------------------------------------------------------- keyboard
struct Key {
  enum Kind { Char, Shift, Page, Lang, Space, Back, Done, Enter } kind = Char;
  std::string label, out;
  int width = 1;  // in letter-key units (action row)
};

struct Keyboard {
  Config cfg;
  Pinyin* py = nullptr;          // shared table (load once per app)
  std::string text;              // committed text
  std::string comp;              // pinyin being composed (Chinese)
  bool shift = false, caps = false, secret = false, url = false;  // url: web address field
  int page = 0;                  // 0 letters, 1 symbols, 2 more symbols
  int row = 1, col = 0, cand = 0, candTop = 0;  // cursor; row -1 = candidate bar
  bool done = false, cancelled = false;

  void begin(const std::string& init, bool isSecret, bool isUrl = false) {
    cfg.load();
    text = init; comp.clear(); secret = isSecret; url = isUrl; page = 0; shift = caps = false;
    row = 1; col = 0; cand = candTop = 0; done = cancelled = false;
    if (secret && cfg.lang != "en" && cfg.en) lang("en");  // passwords: no transformations
  }
  std::string lang() const { return secret || url ? "en" : cfg.lang; }
  void lang(const std::string& l) { cfg.lang = l; }
  std::vector<std::string> candidates() const { return (py && lang() == "zh") ? py->candidates(comp) : std::vector<std::string>(); }

  std::vector<std::vector<Key>> rows() const {
    std::vector<std::vector<Key>> r;
    auto chars = [&](const std::u32string& s) {
      std::vector<Key> v;
      for (char32_t c : s) {
        Key k; k.kind = Key::Char;
        char32_t o = c;
        if (page == 0 && (shift || caps) && c >= 'a' && c <= 'z') o = c - 32;
        k.label = k.out = toU8(std::u32string(1, o));
        v.push_back(k);
      }
      return v;
    };
    bool zh = lang() == "zh";
    if (page == 0) {
      r.push_back(chars(U"1234567890"));
      r.push_back(chars(U"qwertyuiop"));
      r.push_back(chars(url ? U"asdfghjkl-" : U"asdfghjkl'"));
      r.push_back(chars(url ? U"zxcvbnm.:/" : zh ? U"zxcvbnm，。？" : U"zxcvbnm,.?"));
    } else if (page == 1) {
      r.push_back(chars(U"1234567890"));
      r.push_back(chars(U"!@#$%^&*()"));
      r.push_back(chars(U"-_=+[]{};:"));
      r.push_back(chars(U"\"/\\|<>`~,."));
    } else {
      r.push_back(chars(zh ? U"，。？！、：；“”…" : U"…–—•·°±×÷€"));
      r.push_back(chars(zh ? U"（）《》【】￥·～—" : U"£¥©®™§¶«»¿"));
      r.push_back(chars(U"♥★☆✓✗←→↑↓♪"));
      r.push_back(chars(U"☺☹½¼¾²³µ¡ß"));
    }
    std::vector<Key> a(6);
    a[0].kind = Key::Shift; a[0].label = caps ? "⇪" : "⇧";
    a[1].kind = Key::Page; a[1].label = page == 0 ? "?123" : page == 1 ? "#+=" : "ABC";
    a[2].kind = Key::Lang; a[2].label = langLabel(lang());
    a[3].kind = Key::Space; a[3].label = zh && !comp.empty() ? tl("Chọn") : tl("Cách"); a[3].width = 2;
    a[4].kind = Key::Back; a[4].label = "⌫";
    a[5].kind = Key::Done; a[5].label = tl("Xong");
    r.push_back(a);
    return r;
  }
  int rowLen(int r) const { auto rs = rows(); return r < 0 ? (int)candidates().size() : (int)rs[r].size(); }

  // ---- cursor ----
  void move(int dr, int dc) {
    auto rs = rows();
    int last = (int)rs.size() - 1;
    bool hasCand = !candidates().empty();
    if (dc) {
      if (row < 0) { int n = (int)candidates().size(); if (n) cand = (cand + dc + n) % n; return; }
      int n = (int)rs[row].size();
      col = (col + dc + n) % n;
      return;
    }
    int from = row, oldCols = row < 0 ? 10 : (int)rs[row].size();
    row += dr;
    if (row < (hasCand ? -1 : 0)) row = last;
    if (row > last) row = hasCand ? -1 : 0;
    if (row < 0) { cand = 0; return; }
    int n = (int)rs[row].size();
    if (from < 0) col = std::min(col, n - 1);
    else if (oldCols != n) {  // letters (10) <-> action row (6): keep roughly the same x
      static const int map10to6[10] = {0, 0, 1, 2, 3, 3, 3, 4, 5, 5}, map6to10[6] = {0, 2, 3, 5, 7, 9};
      col = n == 6 ? map10to6[std::min(col, 9)] : map6to10[std::min(col, 5)];
    } else col = std::min(col, n - 1);
  }

  // ---- input ----
  void backspace() {
    if (!comp.empty()) { comp.pop_back(); cand = 0; if (comp.empty() && row < 0) row = 0; return; }
    std::u32string t = toU32(text);
    if (!t.empty()) t.pop_back();
    text = toU8(t);
  }
  void commitCandidate(int i) {
    auto c = candidates();
    if (i < 0 || i >= (int)c.size()) return;
    text += c[i];
    std::string h = py ? py->head(comp) : "";
    comp = h.empty() ? "" : comp.substr(h.size());  // continue with the rest of "nihao"
    cand = 0;
    if (comp.empty() && row < 0) row = 0;
  }
  void typeChar(const std::string& s) {
    std::string l = lang();
    std::u32string c = toU32(s);
    if (l == "zh" && page == 0 && c.size() == 1 && ((c[0] >= 'a' && c[0] <= 'z') || (c[0] >= 'A' && c[0] <= 'Z'))) {
      comp += (char)(c[0] | 0x20); cand = 0; return;
    }
    if (l == "zh" && !comp.empty()) { auto cd = candidates(); if (!cd.empty()) commitCandidate(0); else { text += comp; comp.clear(); } }
    if (l == "vi" && c.size() == 1) {
      std::u32string t = toU32(text);
      size_t st = t.size();
      while (st > 0 && vn::isLetter(t[st - 1])) st--;
      std::u32string w = t.substr(st);
      bool vni = cfg.viMethod == "vni";
      bool letterKey = (c[0] >= 'a' && c[0] <= 'z') || (c[0] >= 'A' && c[0] <= 'Z');
      bool modKey = vni ? (c[0] >= '0' && c[0] <= '9') : letterKey;
      if (modKey && (!w.empty() || (!vni && (c[0] == 'w' || c[0] == 'W')))) {
        bool consumed = vn::apply(w, c[0], vni, cfg.newToneStyle);
        if (!consumed && letterKey) w += c[0];
        vn::fixUo(w);
        int tone = vn::currentTone(w);
        if (tone) vn::setTone(w, tone, cfg.newToneStyle);  // tone may need to move (hoa + n -> hoàn)
        t = t.substr(0, st) + w;
        if (!consumed && !letterKey) t += c[0];
        text = toU8(t);
        return;
      }
    }
    text += s;
  }
  // press the key under the cursor (OK button)
  void press() {
    if (row < 0) { commitCandidate(cand); return; }
    auto rs = rows();
    const Key& k = rs[row][std::min(col, (int)rs[row].size() - 1)];
    switch (k.kind) {
      case Key::Char:
        typeChar(k.out);
        if (shift && !caps) shift = false;
        break;
      case Key::Shift:
        if (caps) { caps = shift = false; } else if (shift) caps = true; else shift = true;
        break;
      case Key::Page: page = (page + 1) % 3; break;
      case Key::Lang: {
        if (!comp.empty()) { text += comp; comp.clear(); }
        auto ls = cfg.langs();
        if (secret || url || ls.size() < 2) break;
        size_t i = std::find(ls.begin(), ls.end(), cfg.lang) - ls.begin();
        cfg.lang = ls[(i + 1) % ls.size()];
        cfg.save();  // remembered across apps
        break;
      }
      case Key::Space:
        if (lang() == "zh" && !comp.empty()) commitCandidate(0);
        else text += " ";
        break;
      case Key::Back: backspace(); break;
      case Key::Done: finish(); break;
      case Key::Enter: break;
    }
  }
  void finish() {
    if (!comp.empty()) { text += comp; comp.clear(); }
    done = true;
  }
  // what the input field shows (with the pinyin being composed)
  std::string display() const {
    std::string t = text;
    if (secret) {
      std::u32string u = toU32(text);
      t.clear();
      for (size_t i = 0; i + 1 < u.size(); i++) t += "•";
      if (!u.empty()) t += toU8(std::u32string(1, u.back()));
    }
    return t;
  }
};

}  // namespace s40kb
