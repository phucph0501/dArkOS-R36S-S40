// s40web - web browser for the R36S (640x480), C++/SDL2 shell around an embedded WPE WebKit view.
// Shell: original mini-browser UI in the spirit of classic feature-phone browsers:
//   start page with search, address field and a 3x3 speed dial; left softkey = Menu, right = Back;
//   virtual pointer moved with the D-pad (accelerates, scrolls at the edges), A = click, L1/R1 = page;
//   bookmarks, history, saved pages (offline HTML), find in page, mobile/desktop view, images/JS, zoom,
//   downloads (to <roms>/downloads, J2ME games to <roms>/j2me; <roms> = /roms or /roms2), fullscreen video with pad controls,
//   ad blocker with auto-updated uBlock/ABP lists (Adblock.cpp + web-process extension abext.cpp).
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "Adblock.h"
#include "s40kb.h"
#include "WpeView.h"

namespace web {

constexpr int W = 640, H = 480, TOP_H = 28, SOFT_H = 40, PAGE_Y = TOP_H, PAGE_H = H - TOP_H - SOFT_H;
struct C { Uint8 r, g, b, a = 255; };
static const C BLUE1{11, 61, 145}, BLUE2{47, 125, 225}, HL1{90, 174, 255}, HL2{21, 97, 201}, BG{246, 249, 255}, TXT{16, 33, 58}, SUB{90, 107, 133}, WHITE{255, 255, 255};

SDL_Window* win;
SDL_Renderer* ren;
SDL_Texture* pageTex;
std::string fontDir, dataDir;
std::map<std::pair<int, bool>, TTF_Font*> fonts;
std::map<std::string, SDL_Texture*> textCache;
WpeView view;
Adblock adblock;

TTF_Font* font(int sz, bool bold) {
  auto k = std::make_pair(sz, bold);
  if (fonts.count(k)) return fonts[k];
  TTF_Font* f = TTF_OpenFont((fontDir + (bold ? "/NotoSans-Bold.ttf" : "/NotoSans-Regular.ttf")).c_str(), sz);
  if (!f) f = TTF_OpenFont((fontDir + "/NotoSans-Regular.ttf").c_str(), sz);
  return fonts[k] = f;
}
void col(C c) { SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a); }
void rect(int x, int y, int w, int h, C c) { col(c); SDL_Rect r{x, y, w, h}; SDL_RenderFillRect(ren, &r); }
void grad(int x, int y, int w, int h, C a, C b) {
  for (int i = 0; i < h; i++) { float t = h > 1 ? i / (float)(h - 1) : 0; rect(x, y + i, w, 1, {Uint8(a.r + (b.r - a.r) * t), Uint8(a.g + (b.g - a.g) * t), Uint8(a.b + (b.b - a.b) * t), 255}); }
}
void frame(int x, int y, int w, int h, C c) { col(c); SDL_Rect r{x, y, w, h}; SDL_RenderDrawRect(ren, &r); }
// Han characters (Chinese keyboard, page titles) come from a system CJK font when Noto lacks them
TTF_Font* cjkFont(int sz) {
  static std::map<int, TTF_Font*> fs;
  if (fs.count(sz)) return fs[sz];
  TTF_Font* f = nullptr;
  for (const char* p : {"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"})
    if ((f = TTF_OpenFont(p, sz))) break;
  return fs[sz] = f;
}
// symbols Noto Sans lacks (keyboard keys ⇧ ⌫, ★ ♥ ...): DejaVu Sans, shipped with the launcher's fonts
TTF_Font* symFont(int sz) {
  static std::map<int, TTF_Font*> fs;
  if (fs.count(sz)) return fs[sz];
  TTF_Font* f = TTF_OpenFont((fontDir + "/DejaVuSans.ttf").c_str(), sz);
  if (!f) f = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", sz);
  return fs[sz] = f;
}
// split into (font, text) runs: main font, symbol / CJK fonts for glyphs the main font does not have
std::vector<std::pair<TTF_Font*, std::string>> runs(const std::string& s, int sz, bool bold) {
  std::vector<std::pair<TTF_Font*, std::string>> r;
  TTF_Font* mf = font(sz, bold);
  for (size_t i = 0; i < s.size();) {
    unsigned char ch = s[i];
    int len = ch < 0x80 ? 1 : (ch >> 5) == 6 ? 2 : (ch >> 4) == 14 ? 3 : 4;
    Uint32 cp = len == 1 ? ch : len == 2 ? (ch & 0x1F) : len == 3 ? (ch & 0x0F) : (ch & 0x07);
    for (int k = 1; k < len && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    TTF_Font* f = mf;
    if (cp >= 0x2000 && !TTF_GlyphIsProvided32(mf, cp)) {
      TTF_Font* sym = symFont(sz);
      if (sym && TTF_GlyphIsProvided32(sym, cp)) f = sym;      // ⇧ ⌫ ★ ♥ ...
      else if (TTF_Font* c = cjkFont(sz)) f = c;               // 你好
    }
    if (r.empty() || r.back().first != f) r.push_back({f, ""});
    r.back().second += s.substr(i, len);
    i += len;
  }
  return r;
}
int textW(const std::string& s, int sz, bool bold = false) {
  int total = 0;
  for (auto& r : runs(s, sz, bold)) { int w = 0, h; TTF_SizeUTF8(r.first, r.second.c_str(), &w, &h); total += w; }
  return total;
}
SDL_Surface* renderRuns(const std::string& s, int sz, bool bold, C c) {
  auto rs = runs(s, sz, bold);
  if (rs.size() == 1) return TTF_RenderUTF8_Blended(rs[0].first, s.c_str(), {c.r, c.g, c.b, 255});
  std::vector<SDL_Surface*> parts;
  int tw = 0, asc = 0, desc = 0;
  for (auto& r : rs) {
    SDL_Surface* p = TTF_RenderUTF8_Blended(r.first, r.second.c_str(), {c.r, c.g, c.b, 255});
    parts.push_back(p);
    if (p) tw += p->w;
    asc = std::max(asc, TTF_FontAscent(r.first));
    desc = std::max(desc, -TTF_FontDescent(r.first));
  }
  SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, std::max(1, tw), asc + desc, 32, SDL_PIXELFORMAT_ARGB8888);
  SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_NONE);
  int x = 0;
  for (size_t i = 0; i < parts.size(); i++) {
    if (!parts[i]) continue;
    SDL_SetSurfaceBlendMode(parts[i], SDL_BLENDMODE_NONE);
    SDL_Rect d{x, asc - TTF_FontAscent(rs[i].first), parts[i]->w, parts[i]->h};
    SDL_BlitSurface(parts[i], nullptr, out, &d);
    x += parts[i]->w;
    SDL_FreeSurface(parts[i]);
  }
  SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_BLEND);
  return out;
}
std::string fit(const std::string& s, int sz, bool bold, int maxw) {
  if (textW(s, sz, bold) <= maxw) return s;
  std::string t = s;
  while (!t.empty()) {
    size_t n = t.size() - 1;
    while (n > 0 && (t[n] & 0xC0) == 0x80) n--;
    t.erase(n);
    if (textW(t + "…", sz, bold) <= maxw) return t + "…";
  }
  return "";
}
int text(const std::string& s0, int x, int y, int sz, C c, bool bold = false, int align = 0, int maxw = 0) {
  if (s0.empty()) return 0;
  std::string s = maxw ? fit(s0, sz, bold, maxw) : s0;
  char k[48]; snprintf(k, sizeof k, "|%d%d%02x%02x%02x", sz, bold, c.r, c.g, c.b);
  std::string key = s + k;
  SDL_Texture* t = textCache.count(key) ? textCache[key] : nullptr;
  if (!t) {
    if (textCache.size() > 400) { for (auto& e : textCache) SDL_DestroyTexture(e.second); textCache.clear(); }
    SDL_Surface* su = renderRuns(s, sz, bold, c);
    if (!su) return 0;
    t = SDL_CreateTextureFromSurface(ren, su);
    SDL_FreeSurface(su);
    textCache[key] = t;
  }
  int w, h; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
  SDL_Rect d{align == 1 ? x - w / 2 : align == 2 ? x - w : x, y, w, h};
  SDL_RenderCopy(ren, t, nullptr, &d);
  return w;
}

// ---------------- settings + persistent lists ----------------
std::map<std::string, std::string> cfg;
void loadCfg() { std::ifstream f(dataDir + "/settings.cfg"); std::string l; while (std::getline(f, l)) { auto e = l.find('='); if (e != std::string::npos) cfg[l.substr(0, e)] = l.substr(e + 1); } }
void saveCfg() { std::ofstream f(dataDir + "/settings.cfg", std::ios::trunc); for (auto& kv : cfg) f << kv.first << '=' << kv.second << '\n'; }
bool cfgb(const std::string& k, bool d) { return cfg.count(k) ? cfg[k] == "1" : d; }
int cfgi(const std::string& k, int d) { return cfg.count(k) ? atoi(cfg[k].c_str()) : d; }
void applySettings() {
  view.setMobile(cfgb("mobile", true));
  view.setImages(cfgb("images", true));
  view.setJs(cfgb("js", true));
  view.setZoom(cfgi("zoom", 100) / 100.0);
}

struct Entry { std::string title, url; };
std::vector<Entry> load(const std::string& name) {
  std::vector<Entry> v; std::ifstream f(dataDir + "/" + name); std::string l;
  while (std::getline(f, l)) { auto t = l.find('\t'); if (t != std::string::npos) v.push_back({l.substr(0, t), l.substr(t + 1)}); }
  return v;
}
void save(const std::string& name, const std::vector<Entry>& v) {
  std::ofstream f(dataDir + "/" + name, std::ios::trunc);
  for (auto& e : v) f << e.title << '\t' << e.url << '\n';
}
std::vector<Entry> speedDial() {
  auto v = load("speeddial.txt");
  if (v.empty()) {
    v = {{"Google", "https://www.google.com"}, {"YouTube", "https://m.youtube.com"}, {"Wikipedia", "https://vi.m.wikipedia.org"},
         {"VnExpress", "https://vnexpress.net"}, {tl("Dân trí"), "https://dantri.com.vn"}, {"Facebook", "https://m.facebook.com"},
         {tl("Thời tiết"), "https://wttr.in/?format=4"}, {"dArkOS Wiki", "https://github.com/christianhaitian/dArkOS/wiki"}, {"", ""}};
    save("speeddial.txt", v);
  }
  v.resize(9);
  return v;
}
std::string host(const std::string& url) {
  auto a = url.find("://"); a = a == std::string::npos ? 0 : a + 3;
  auto b = url.find('/', a);
  std::string h = url.substr(a, b == std::string::npos ? std::string::npos : b - a);
  if (h.rfind("www.", 0) == 0) h = h.substr(4);
  return h;
}
std::string urlEncode(const std::string& s) {
  std::string o; char b[4];
  for (unsigned char c : s) { if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c; else if (c == ' ') o += '+'; else { snprintf(b, sizeof b, "%%%02X", c); o += b; } }
  return o;
}
std::string normalizeUrl(std::string u) {
  while (!u.empty() && isspace((unsigned char)u.back())) u.pop_back();
  while (!u.empty() && isspace((unsigned char)u.front())) u.erase(u.begin());
  if (u.find("://") == std::string::npos && u.rfind("about:", 0) != 0) u = "https://" + u;
  return u;
}

// ---------------- overlays ----------------
enum class Key { None, Up, Down, Left, Right, OK, Back, Menu, Go, Home, L, R };
struct Overlay {
  virtual ~Overlay() = default;
  virtual void draw() = 0;
  virtual bool key(Key k) = 0;
  virtual std::string softL() { return tl("Chọn"); }
  virtual std::string softR() { return tl("Quay lại"); }
};
std::vector<std::unique_ptr<Overlay>> overlays;
std::string toastMsg; Uint32 toastUntil = 0;
void toast(const std::string& m) { toastMsg = m; toastUntil = SDL_GetTicks() + 1800; }

struct Item { std::string label, sub; std::function<void()> fn; };
struct ListPopup : Overlay {
  std::string title; std::vector<Item> items; int sel = 0, top = 0; bool full;
  ListPopup(std::string t, std::vector<Item> it, bool fullScreen = false) : title(std::move(t)), items(std::move(it)), full(fullScreen) {}
  void draw() override {
    bool subs = false;
    for (auto& i : items) subs |= !i.sub.empty();
    int rowH = full || subs ? 46 : 38, vis = full ? 8 : std::min(subs ? 7 : 8, (int)items.size());
    int y0 = full ? TOP_H : H - SOFT_H - vis * rowH - 40, x0 = full ? 0 : 30, w = full ? W : W - 60;
    if (!full) rect(0, TOP_H, W, H - TOP_H - SOFT_H, {0, 0, 0, 110});
    rect(x0, y0, w, (full ? H - TOP_H - SOFT_H : vis * rowH + 40), BG);
    grad(x0, y0, w, 34, BLUE2, BLUE1);
    text(title, x0 + 12, y0 + 5, 17, WHITE, true, 0, w - 24);
    if (items.empty()) { text(tl("(Trống)"), x0 + w / 2, y0 + 80, 17, SUB, false, 1); return; }
    if (sel < top) top = sel;
    if (sel >= top + vis) top = sel - vis + 1;
    for (int i = top; i < std::min((int)items.size(), top + vis); i++) {
      int y = y0 + 38 + (i - top) * rowH;
      if (i == sel) grad(x0 + 4, y, w - 8, rowH - 2, HL1, HL2);
      C fg = i == sel ? WHITE : TXT;
      if (!items[i].sub.empty()) { text(items[i].label, x0 + 14, y + 2, 16, fg, true, 0, w - 30); text(items[i].sub, x0 + 14, y + 23, 12, i == sel ? WHITE : SUB, false, 0, w - 30); }
      else text(items[i].label, x0 + 14, y + (rowH - 22) / 2, 16, fg, i == sel, 0, w - 30);
    }
  }
  bool key(Key k) override {
    int n = (int)items.size();
    if (k == Key::Back) return false;
    if (!n) return true;
    if (k == Key::Up) sel = (sel - 1 + n) % n;
    else if (k == Key::Down) sel = (sel + 1) % n;
    else if (k == Key::Left) sel = std::max(0, sel - 8);
    else if (k == Key::Right) sel = std::min(n - 1, sel + 8);
    else if (k == Key::OK || k == Key::Menu) { auto f = items[sel].fn; if (f) { overlays.pop_back(); f(); return true; } }
    return true;
  }
};

// On-screen keyboard: the engine (layouts, Vietnamese Telex/VNI, English, Chinese Pinyin, symbols,
// settings in ~/.config/s40kb.cfg) is shared with the S40 launcher: common/s40kb.h.
// D-pad moves, A types, X/SELECT = done, B = backspace (back when empty), L = language, R = symbols.
s40kb::Pinyin pinyin;
std::string pinyinPath() {
  if (const char* p = getenv("S40KB_PINYIN")) return p;
  return fontDir + "/../kb/pinyin.txt";  // /opt/s40/assets/kb/pinyin.txt next to the launcher's fonts
}
struct Keyboard : Overlay {
  std::string title;
  s40kb::Keyboard kb;
  std::function<void(const std::string&)> done;
  Keyboard(std::string t, std::string v, std::function<void(const std::string&)> d, bool secret = false, bool url = false)
      : title(std::move(t)), done(std::move(d)) {
    kb.py = &pinyin;
    kb.begin(v, secret, url);
    ensurePinyin();
  }
  void ensurePinyin() { if (kb.lang() == "zh" && !pinyin.loaded) pinyin.load(pinyinPath()); }
  void draw() override {
    rect(0, TOP_H, W, H - TOP_H - SOFT_H, BG);
    grad(0, TOP_H, W, 32, BLUE2, BLUE1);
    text(title, 12, TOP_H + 4, 16, WHITE, true);
    // field + language badge
    int y = TOP_H + 38;
    rect(10, y, W - 20, 38, BLUE2); rect(12, y + 2, W - 24, 34, WHITE);
    std::string badge = s40kb::langLabel(kb.lang()) + (kb.lang() == "vi" ? (kb.cfg.viMethod == "vni" ? " VNI" : " Telex") : "");
    int bw = textW(badge, 13, true) + 14;
    rect(W - 18 - bw, y + 8, bw, 22, BLUE2);
    text(badge, W - 18 - bw / 2, y + 9, 13, WHITE, true, 1);
    std::string shown = kb.display();
    int cw = kb.comp.empty() ? 0 : textW(kb.comp, 18) + 4, maxw = W - 60 - bw;
    if (textW(shown, 18) + cw > maxw) {  // keep the end of long text visible
      std::u32string u = s40kb::toU32(shown);
      while (!u.empty() && textW(s40kb::toU8(u), 18) + cw > maxw - 14) u.erase(0, 1);
      shown = "…" + s40kb::toU8(u);
    }
    int tw = text(shown, 20, y + 6, 18, TXT);
    if (cw) { text(kb.comp, 22 + tw, y + 6, 18, BLUE1); rect(22 + tw, y + 30, cw - 4, 2, BLUE1); }
    rect(22 + tw + cw, y + 7, 2, 24, BLUE2);
    y += 44;
    // candidate bar (Chinese)
    if (kb.lang() == "zh") {
      auto cs = kb.candidates();
      rect(10, y, W - 20, 30, {226, 233, 244});
      if (cs.empty()) text(kb.comp.empty() ? tl("Gõ pinyin, ví dụ: nihao") : tl("Không có chữ cho \"") + kb.comp + "\"", 20, y + 5, 14, SUB);
      else {
        int cellW = 38, fit = (W - 24) / cellW;
        if (kb.cand < kb.candTop) kb.candTop = kb.cand;
        if (kb.cand >= kb.candTop + fit) kb.candTop = kb.cand - fit + 1;
        for (int i = kb.candTop; i < (int)cs.size() && i < kb.candTop + fit; i++) {
          int cx = 12 + (i - kb.candTop) * cellW;
          bool s = kb.row < 0 && i == kb.cand;
          if (s) grad(cx, y + 1, cellW - 4, 28, HL1, HL2);
          text(cs[i], cx + (cellW - 4) / 2, y + 3, 18, s ? WHITE : (i == 0 ? BLUE1 : TXT), i == 0, 1);
        }
      }
      y += 34;
    }
    // keys
    auto rows = kb.rows();
    int gap = 4, rh = std::min(42, (H - SOFT_H - 6 - y - gap * 4) / 5);
    for (int ri = 0; ri < (int)rows.size(); ri++) {
      auto& row = rows[ri];
      int units = 0;
      for (auto& k : row) units += k.width;
      float uw = (W - 20 - gap * (row.size() - 1)) / (float)units, kx = 10;
      for (int ci = 0; ci < (int)row.size(); ci++) {
        const auto& k = row[ci];
        int kw = (int)(uw * k.width + gap * (k.width - 1));
        bool s = kb.row == ri && kb.col == ci, on = k.kind == s40kb::Key::Shift && (kb.shift || kb.caps);
        if (s) grad((int)kx, y, kw, rh, HL1, HL2);
        else rect((int)kx, y, kw, rh, on ? BLUE2 : k.kind == s40kb::Key::Char ? C{220, 228, 240} : C{205, 215, 230});
        int fs = k.kind == s40kb::Key::Char ? 17 : 14;
        text(k.label, (int)kx + kw / 2, y + (rh - fs) / 2 - 3, fs, s || on ? WHITE : TXT, true, 1, kw - 4);
        kx += kw + gap;
      }
      y += rh + gap;
    }
  }
  std::string softL() override { return "Xong"; }
  std::string softR() override { return kb.text.empty() && kb.comp.empty() ? tl("Quay lại") : tl("Xóa"); }
  bool finish() {
    kb.finish();
    kb.cfg.save();
    auto d = done; auto v = kb.text;
    overlays.pop_back();
    d(v);
    return true;
  }
  bool key(Key k) override {
    ensurePinyin();
    switch (k) {
      case Key::Up: kb.move(-1, 0); break;
      case Key::Down: kb.move(1, 0); break;
      case Key::Left: kb.move(0, -1); break;
      case Key::Right: kb.move(0, 1); break;
      case Key::OK: kb.press(); if (kb.done) return finish(); break;
      case Key::Menu: return finish();
      case Key::Back:
        if (kb.text.empty() && kb.comp.empty()) return false;
        kb.backspace();
        break;
      case Key::L: {
        auto ls = kb.cfg.langs();
        if (!kb.secret && !kb.url && ls.size() > 1) {
          if (!kb.comp.empty()) { kb.text += kb.comp; kb.comp.clear(); }
          size_t i = std::find(ls.begin(), ls.end(), kb.cfg.lang) - ls.begin();
          kb.cfg.lang = ls[(i + 1) % ls.size()];
          kb.cfg.save();
          if (kb.row < 0) kb.row = 0;
        }
        break;
      }
      case Key::R: kb.page = kb.page == 0 ? 1 : 0; break;
      default: break;
    }
    return true;
  }
};
void keyboard(const std::string& title, const std::string& v, std::function<void(const std::string&)> d, bool secret = false, bool url = false) {
  // web address and password fields get the URL layout / hidden input
  if (title.rfind(tl("Đi tới địa chỉ"), 0) == 0 || title.rfind(tl("Địa chỉ"), 0) == 0) url = true;
  if (title == tl("Nhập mật khẩu")) secret = true;
  overlays.emplace_back(new Keyboard(title, v, std::move(d), secret, url));
}
void list(const std::string& title, std::vector<Item> items, bool full = false) { overlays.emplace_back(new ListPopup(title, std::move(items), full)); }

// ---------------- browser state ----------------
bool onStart = true;
// --app: opened by the launcher straight into a site (YouTube, Telegram...): no start page, B on the
// first page goes back to the launcher
bool appMode = false;
int startSel = 1;
std::string lastLoggedUrl, hoverHref;
bool quit = false;
float curX = W / 2, curY = PAGE_H / 3;  // virtual pointer in page coordinates
Uint32 lastHoverQuery = 0, clickCheckAt = 0;
bool hoverDirty = false;
int dirHeld[4] = {0, 0, 0, 0};  // up down left right: ms held (0 = released)

void openUrl(const std::string& u) {
  if (u.empty()) return;
  onStart = false;
  adblock.onNavigate(normalizeUrl(u));  // per-site exception must be in place before the first request
  view.load(normalizeUrl(u));
}
static const std::vector<Entry> ENGINES = {{"Google", "https://www.google.com/search?q="}, {"Bing", "https://www.bing.com/search?q="},
                                           {"DuckDuckGo", "https://duckduckgo.com/?q="}, {"Wikipedia", "https://vi.m.wikipedia.org/w/index.php?search="}};
int engineIdx() { int i = cfgi("engine", 0); return (i >= 0 && i < (int)ENGINES.size()) ? i : 0; }
void search(const std::string& q) { if (!q.empty()) openUrl(ENGINES[engineIdx()].url + urlEncode(q)); }
void goAddress() {
  keyboard(tl("Đi tới địa chỉ"), onStart ? "" : view.uri(), [](const std::string& u) {
    if (u.find('.') == std::string::npos && u.find("://") == std::string::npos) search(u); else openUrl(u);
  });
}
std::string pageTitle() { std::string t = view.title(); return t.empty() ? host(view.uri()) : t; }
void showEntries(const std::string& title, const std::string& file) {
  auto v = load(file);
  std::vector<Item> it;
  for (int i = (int)v.size() - 1; i >= 0; i--) {
    Entry e = v[i];
    it.push_back({e.title.empty() ? e.url : e.title, e.url.rfind("file://", 0) == 0 ? tl("Đã lưu trên máy") : e.url, [e, file, i] {
      list(e.title.empty() ? e.url : e.title, {
          {tl("Mở"), "", [e] { openUrl(e.url); }},
          {tl("Xóa"), "", [file, i] { auto v2 = load(file); if (i < (int)v2.size()) { if (v2[i].url.rfind("file://", 0) == 0) remove(v2[i].url.substr(7).c_str()); v2.erase(v2.begin() + i); save(file, v2); toast(tl("Đã xóa")); } }}});
    }});
  }
  list(title, it, true);
}
void addBookmark() { auto v = load("bookmarks.txt"); v.push_back({pageTitle(), view.uri()}); save("bookmarks.txt", v); toast(tl("Đã thêm dấu trang")); }
void savePage() {
  mkdir((dataDir + "/saved").c_str(), 0755);
  // WPE builds ship without MHTML support, so snapshot the live DOM instead: scripts removed,
  // same-origin stylesheets inlined, links/images made absolute via <base>.
  std::string path = dataDir + "/saved/" + std::to_string((long)time(nullptr)) + ".html";
  std::string title = pageTitle();
  static const char* js =
      "(function(){var d=document.documentElement.cloneNode(true);"
      "d.querySelectorAll('script,noscript,iframe').forEach(function(e){e.remove()});"
      "var css='';for(var i=0;i<document.styleSheets.length;i++){try{var r=document.styleSheets[i].cssRules;"
      "for(var j=0;j<r.length;j++)css+=r[j].cssText+'\\n'}catch(e){}}"
      "d.querySelectorAll('link[rel=stylesheet],style').forEach(function(e){e.remove()});"
      "d.querySelectorAll('img').forEach(function(e){if(e.currentSrc)e.setAttribute('src',e.currentSrc);e.removeAttribute('srcset');e.removeAttribute('loading')});"
      "var h=d.querySelector('head')||d;var b=document.createElement('base');b.href=location.href;h.insertBefore(b,h.firstChild);"
      "var m=document.createElement('meta');m.setAttribute('charset','utf-8');h.insertBefore(m,h.firstChild);"
      "var s=document.createElement('style');s.textContent=css;h.appendChild(s);"
      "return '<!DOCTYPE html>\\n'+d.outerHTML})()";
  view.runJs(js, [path, title](const std::string& html) {
    FILE* f = html.size() > 64 ? fopen(path.c_str(), "wb") : nullptr;
    if (!f) { toast(tl("Không lưu được trang")); return; }
    fwrite(html.data(), 1, html.size(), f);
    fclose(f);
    auto v = load("saved.txt"); v.push_back({title, "file://" + path}); save("saved.txt", v);
    toast(tl("Đã lưu trang để đọc khi không có mạng"));
  });
}
void findInPage() {
  keyboard(tl("Tìm trong trang"), "", [](const std::string& q) {
    if (q.empty()) return;
    view.find(q);
    toast(tl("Đang tìm \"") + q + tl("\" · Menu › Tìm tiếp"));
  });
}
// ---------------- downloads ----------------
// Saved where the rest of the system can see them: <roms>/downloads (ROM card, also visible from a PC),
// J2ME games straight into <roms>/j2me so the launcher lists them. ~/Downloads off-device.
// <roms> is /roms2 after dArkOS "Switch to SD2 for Roms" (it rewrites es_systems.cfg paths).
static std::string romsRoot() {
  std::string base = "/roms";
  if (FILE* f = fopen("/etc/emulationstation/es_systems.cfg", "r")) {
    char line[512];
    while (fgets(line, sizeof line, f)) if (strstr(line, "<path>/roms2/")) { base = "/roms2"; break; }
    fclose(f);
  }
  return base;
}
std::string downloadPath(std::string name, const std::string&) {
  for (char& c : name) if (c == '/' || c == '\\' || (unsigned char)c < 0x20) c = '_';
  if (name.empty() || name[0] == '.') name = "tai-ve" + name;
  std::string ext = name.find('.') != std::string::npos ? name.substr(name.rfind('.') + 1) : "";
  for (char& c : ext) c = (char)tolower((unsigned char)c);
  std::string dir;
  if (const char* d = getenv("S40WEB_DOWNLOADS")) dir = d;
  else if (std::string r = romsRoot(); access(r.c_str(), W_OK) == 0)
    dir = (ext == "jar" || ext == "jad") && access((r + "/j2me").c_str(), W_OK) == 0 ? r + "/j2me" : r + "/downloads";
  else { const char* home = getenv("HOME"); dir = std::string(home ? home : "/tmp") + "/Downloads"; }
  mkdir(dir.c_str(), 0755);
  std::string stem = ext.empty() ? name : name.substr(0, name.size() - ext.size() - 1), dot = ext.empty() ? "" : "." + name.substr(name.size() - ext.size());
  std::string path = dir + "/" + name;
  for (int i = 1; access(path.c_str(), F_OK) == 0 && i < 1000; i++) path = dir + "/" + stem + " (" + std::to_string(i) + ")" + dot;
  return path;
}
std::string sizeStr(uint64_t b) {
  char t[32];
  if (b >= 1024ull * 1024 * 1024) snprintf(t, sizeof t, "%.2f GB", b / 1073741824.0);
  else if (b >= 1024 * 1024) snprintf(t, sizeof t, "%.1f MB", b / 1048576.0);
  else snprintf(t, sizeof t, "%llu KB", (unsigned long long)(b / 1024));
  return t;
}
int runningDownloads(int* pct = nullptr) {
  int n = 0;
  uint64_t got = 0, tot = 0;
  for (auto& d : view.downloads)
    if (d->state == WpeView::Download::Running) { n++; got += d->received; tot += d->total; }
  if (pct) *pct = tot ? (int)(got * 100 / tot) : -1;
  return n;
}
void downloadsMenu() {
  std::vector<Item> it;
  for (int i = (int)view.downloads.size() - 1; i >= 0; i--) {
    auto d = view.downloads[i];
    std::string sub;
    switch (d->state) {
      case WpeView::Download::Running:
        sub = d->total ? std::to_string((int)(d->received * 100 / d->total)) + "% · " + sizeStr(d->received) + " / " + sizeStr(d->total) : tl("Đang tải · ") + sizeStr(d->received);
        break;
      case WpeView::Download::Done: sub = "Xong · " + sizeStr(d->total) + " · " + d->path.substr(0, d->path.rfind('/')); break;
      case WpeView::Download::Failed: sub = tl("Lỗi: ") + d->error; break;
      case WpeView::Download::Cancelled: sub = tl("Đã hủy"); break;
    }
    std::string name = d->name.empty() ? host(d->url) : d->name;
    it.push_back({name, sub, [d, name] {
                    std::vector<Item> o;
                    if (d->state == WpeView::Download::Running) o.push_back({tl("Hủy tải"), "", [d] { view.cancelDownload(*d); toast(tl("Đã hủy tải")); }});
                    else if (d->state != WpeView::Download::Done) o.push_back({tl("Tải lại"), "", [d] { view.download(d->url); toast(tl("Đang tải lại")); }});
                    o.push_back({tl("Bỏ khỏi danh sách"), "", [d] {
                                   auto& v = view.downloads;
                                   v.erase(std::remove(v.begin(), v.end(), d), v.end());
                                   downloadsMenu();
                                 }});
                    if (d->state == WpeView::Download::Done)
                      o.push_back({tl("Xóa tệp"), d->path, [d] {
                                     remove(d->path.c_str());
                                     auto& v = view.downloads;
                                     v.erase(std::remove(v.begin(), v.end(), d), v.end());
                                     toast(tl("Đã xóa tệp"));
                                     downloadsMenu();
                                   }});
                    list(name, o);
                  }});
  }
  list(tl("Tải xuống"), it, true);
}

// ---------------- video ----------------
// Controls the playing video (the fullscreen one, else the first on the page).
void videoJs(const std::string& action) {
  view.runJs("(function(){var v=document.fullscreenElement||document.webkitFullscreenElement;"
             "if(v&&v.tagName!='VIDEO')v=v.querySelector('video');if(!v)v=document.querySelector('video');if(!v)return;" + action + "})()");
}
void videoFullscreen() {
  view.runJs("(function(){var b=null,a=0;document.querySelectorAll('video').forEach(function(v){var r=v.getBoundingClientRect(),s=r.width*r.height;if(s>a){a=s;b=v}});"
             "if(!b)return 'none';if(b.paused)b.play();"
             // fullscreen the site's player (YouTube, video.js, JW, Plyr) like its own button does; the bare
             // <video> only when there is none - players stall when their video is pulled out of them
             "var t=b.closest('#movie_player,.html5-video-player,.video-js,.jwplayer,.plyr,[data-player]')||b;"
             "var f=t.requestFullscreen||t.webkitRequestFullscreen||b.webkitEnterFullscreen;"
             "if(f){var r=f.call(t);if(r&&r.catch)r.catch(function(){});}return 'ok';})()",
             [](const std::string& r) { if (r == "none") toast(tl("Trang này không có video")); });
}

void listAt(const std::string& title, std::vector<Item> items, int sel) {
  list(title, std::move(items));
  static_cast<ListPopup*>(overlays.back().get())->sel = sel;
}
void adblockListsMenu(int sel = 0) {
  std::vector<Item> it;
  int i = 0;
  for (auto& l : Adblock::catalog()) {
    std::string id = l.id;
    int me = i++;
    it.push_back({std::string(adblock.listOn(id) ? "[x] " : "[ ] ") + l.name, "", [id, me] { adblock.setListOn(id, !adblock.listOn(id)); adblockListsMenu(me); }});
  }
  listAt(tl("Danh sách lọc"), it, sel);
}
void adblockSitesMenu() {
  std::vector<Item> it;
  for (auto& h : adblock.sites())
    it.push_back({h, tl("Bấm để bỏ ngoại lệ (chặn lại quảng cáo)"), [h] { adblock.setSiteExcepted(h, false); toast(tl("Đã chặn quảng cáo lại trên ") + h); adblockSitesMenu(); }});
  list(tl("Trang không chặn quảng cáo"), it, true);
}
void adblockMenu(int sel = 0) {
  auto onoff = [](bool b) { return std::string(b ? tl("Bật") : tl("Tắt")); };
  listAt(tl("Chặn quảng cáo"), {
      {tl("Chặn quảng cáo: ") + onoff(adblock.enabled()), adblock.status(), [] { adblock.setEnabled(!adblock.enabled()); toast(adblock.enabled() ? tl("Đã bật chặn quảng cáo") : tl("Đã tắt chặn quảng cáo")); adblockMenu(0); }},
      {tl("Ẩn khung quảng cáo: ") + onoff(adblock.cosmetic()), tl("Ẩn ô trống còn lại sau khi chặn"), [] { adblock.setCosmetic(!adblock.cosmetic()); adblockMenu(1); }},
      {tl("Danh sách lọc"), "EasyList, ABPVN, uBlock…", [] { adblockListsMenu(); }},
      {tl("Cập nhật danh sách ngay"), "", [] { adblock.updateNow(); }},
      {tl("Tự động cập nhật: ") + onoff(adblock.autoUpdate()), tl("Mỗi 4 ngày khi có mạng"), [] { adblock.setAutoUpdate(!adblock.autoUpdate()); adblockMenu(4); }},
      {tl("Trang không chặn"), std::to_string(adblock.sites().size()) + " trang", adblockSitesMenu},
  }, sel);
}
void settingsMenu() {
  auto onoff = [](bool b) { return std::string(b ? tl("Bật") : tl("Tắt")); };
  list(tl("Cài đặt"), {
      {std::string(tl("Chế độ xem: ")) + (cfgb("mobile", true) ? tl("Di động") : tl("Máy tính")), tl("Di động = trang gọn cho màn nhỏ"), [] {
         cfg["mobile"] = cfgb("mobile", true) ? "0" : "1"; saveCfg(); applySettings(); if (!onStart) view.reload(); }},
      {tl("Công cụ tìm kiếm: ") + ENGINES[engineIdx()].title, "", [] { cfg["engine"] = std::to_string((engineIdx() + 1) % ENGINES.size()); saveCfg(); toast(tl("Tìm kiếm bằng ") + ENGINES[engineIdx()].title); }},
      {tl("Cỡ chữ (thu phóng): ") + std::to_string(cfgi("zoom", 100)) + "%", "", [] {
         int z = cfgi("zoom", 100); z = z >= 150 ? 80 : z + 10; cfg["zoom"] = std::to_string(z); saveCfg(); applySettings(); }},
      {tl("Chặn quảng cáo: ") + onoff(adblock.enabled()), adblock.status(), [] { adblockMenu(); }},
      {tl("Hình ảnh: ") + onoff(cfgb("images", true)), tl("Tắt để tải nhanh, đỡ tốn mạng"), [] { cfg["images"] = cfgb("images", true) ? "0" : "1"; saveCfg(); applySettings(); }},
      {"JavaScript: " + onoff(cfgb("js", true)), "", [] { cfg["js"] = cfgb("js", true) ? "0" : "1"; saveCfg(); applySettings(); }},
      {tl("Xóa lịch sử"), "", [] { save("history.txt", {}); toast(tl("Đã xóa lịch sử")); }},
  });
}
void mainMenu() {
  bool page = !onStart;
  std::vector<Item> it = {{tl("Đi tới địa chỉ"), "", goAddress}, {tl("Tìm kiếm"), "", [] { keyboard(tl("Tìm kiếm"), "", search); }},
                          {appMode ? tl("Thoát") : tl("Trang khởi động"), "", [] { if (appMode) quit = true; else onStart = true; }}};
  if (page) {
    it.push_back({view.loading() ? tl("Dừng tải") : tl("Tải lại"), "", [] { if (view.loading()) view.stop(); else view.reload(); }});
    if (view.canForward()) it.push_back({tl("Tiến"), "", [] { view.forward(); }});
    it.push_back({tl("Thêm dấu trang"), "", addBookmark});
    it.push_back({tl("Lưu trang"), tl("Đọc lại khi không có mạng"), savePage});
    it.push_back({tl("Tìm trong trang"), "", findInPage});
    if (view.lastFindCount() > 0) it.push_back({tl("Tìm tiếp"), "", [] { view.findNext(); }});
    it.push_back({tl("Lên đầu trang"), "", [] { view.keyTap(XKB_KEY_Home); }});
    it.push_back({tl("Xem video toàn màn hình"), tl("A: dừng/phát · Trái/Phải: tua · Lên/Xuống: âm lượng"), videoFullscreen});
    if (hoverHref.rfind("http", 0) == 0) {
      std::string u = hoverHref;
      it.push_back({tl("Tải về liên kết này"), u, [u] { view.download(u); toast(tl("Bắt đầu tải về")); }});
    }
    if (adblock.enabled()) {
      std::string h = host(view.uri());
      bool ex = adblock.siteExcepted(h);
      it.push_back({ex ? tl("Chặn quảng cáo trên trang này") : tl("Không chặn quảng cáo trang này"), h, [h, ex] {
                      adblock.setSiteExcepted(h, !ex); toast(ex ? tl("Đã bật chặn quảng cáo cho ") + h : tl("Đã tắt chặn quảng cáo cho ") + h); view.reload(); }});
    }
  }
  it.push_back({tl("Dấu trang"), "", [] { showEntries(tl("Dấu trang"), "bookmarks.txt"); }});
  it.push_back({tl("Lịch sử"), "", [] { showEntries(tl("Lịch sử"), "history.txt"); }});
  it.push_back({tl("Trang đã lưu"), "", [] { showEntries(tl("Trang đã lưu"), "saved.txt"); }});
  { int n = runningDownloads(); it.push_back({tl("Tải xuống"), n ? std::to_string(n) + tl(" tệp đang tải") : std::to_string(view.downloads.size()) + tl(" tệp"), downloadsMenu}); }
  it.push_back({tl("Cài đặt"), "", settingsMenu});
  it.push_back({tl("Thoát"), "", [] { quit = true; }});
  list("Menu", it);
}

// ---------------- start page ----------------
void drawStart() {
  rect(0, TOP_H, W, H - TOP_H - SOFT_H, BG);
  auto field = [](int idx, int y, const std::string& label) {
    bool s = startSel == idx;
    rect(14, y, W - 28, 36, s ? HL2 : C{180, 195, 215});
    rect(16, y + 2, W - 32, 32, WHITE);
    text(label, 28, y + 6, 16, SUB);
  };
  field(0, TOP_H + 10, tl("Tìm kiếm trên web (") + ENGINES[engineIdx()].title + ")");
  field(1, TOP_H + 52, tl("Nhập địa chỉ trang web"));
  auto dial = speedDial();
  int tw = (W - 28 - 16) / 3, th = 74, y0 = TOP_H + 98;
  for (int i = 0; i < 9; i++) {
    int x = 14 + (i % 3) * (tw + 8), y = y0 + (i / 3) * (th + 8);
    bool s = startSel == 2 + i;
    if (s) grad(x, y, tw, th, HL1, HL2); else rect(x, y, tw, th, WHITE);
    frame(x, y, tw, th, s ? HL2 : C{200, 210, 225});
    rect(x + 6, y + 6, 22, 22, s ? WHITE : BLUE2);
    text(std::to_string(i + 1), x + 17, y + 6, 14, s ? HL2 : WHITE, true, 1);
    if (dial[i].url.empty()) text(tl("+ Thêm"), x + tw / 2, y + 30, 15, s ? WHITE : SUB, false, 1);
    else { text(dial[i].title, x + 34, y + 6, 15, s ? WHITE : TXT, true, 0, tw - 40); text(host(dial[i].url), x + 8, y + 42, 13, s ? WHITE : SUB, false, 0, tw - 16); }
  }
  static const char* L[] = {tl("Dấu trang"), tl("Lịch sử"), tl("Trang đã lưu")};
  int lw = (W - 28 - 16) / 3, ly = y0 + 3 * (th + 8) + 2;
  for (int i = 0; i < 3; i++) {
    int x = 14 + i * (lw + 8);
    bool s = startSel == 11 + i;
    if (s) grad(x, ly, lw, 30, HL1, HL2); else rect(x, ly, lw, 30, {225, 233, 245});
    text(L[i], x + lw / 2, ly + 4, 15, s ? WHITE : BLUE1, true, 1);
  }
}
void startKey(Key k) {
  int s = startSel;
  auto dialIdx = [](int r, int c) { return 2 + r * 3 + c; };
  if (k == Key::Down) {
    if (s == 0) startSel = 1; else if (s == 1) startSel = 2;
    else if (s >= 2 && s <= 10) { int i = s - 2; startSel = i / 3 < 2 ? s + 3 : 11 + i % 3; }
    else startSel = 0;
  } else if (k == Key::Up) {
    if (s == 0) startSel = 11; else if (s == 1) startSel = 0;
    else if (s >= 2 && s <= 10) { int i = s - 2; startSel = i / 3 > 0 ? s - 3 : 1; }
    else startSel = dialIdx(2, s - 11);
  } else if (k == Key::Left || k == Key::Right) {
    int d = k == Key::Right ? 1 : -1;
    if (s >= 2 && s <= 10) { int i = s - 2, r = i / 3, c = (i % 3 + d + 3) % 3; startSel = dialIdx(r, c); }
    else if (s >= 11) startSel = 11 + ((s - 11 + d + 3) % 3);
  } else if (k == Key::OK) {
    if (s == 0) keyboard(tl("Tìm kiếm"), "", search);
    else if (s == 1) goAddress();
    else if (s >= 2 && s <= 10) {
      auto dial = speedDial();
      int i = s - 2;
      if (!dial[i].url.empty()) openUrl(dial[i].url);
      else keyboard(tl("Địa chỉ cho ô ") + std::to_string(i + 1), "https://", [i](const std::string& u) {
        if (u.size() < 9) return;
        auto d = speedDial(); d[i] = {host(u), normalizeUrl(u)}; save("speeddial.txt", d);
      });
    } else if (s == 11) showEntries(tl("Dấu trang"), "bookmarks.txt");
    else if (s == 12) showEntries(tl("Lịch sử"), "history.txt");
    else showEntries(tl("Trang đã lưu"), "saved.txt");
  } else if (k == Key::Menu && s >= 2 && s <= 10) {
    int i = s - 2;
    list(tl("Ô ") + std::to_string(i + 1), {
        {tl("Sửa địa chỉ"), "", [i] { auto d = speedDial(); keyboard(tl("Địa chỉ cho ô ") + std::to_string(i + 1), d[i].url.empty() ? "https://" : d[i].url, [i](const std::string& u) {
           auto d2 = speedDial(); d2[i] = {host(u), normalizeUrl(u)}; save("speeddial.txt", d2); }); }},
        {tl("Đổi tên"), "", [i] { auto d = speedDial(); keyboard(tl("Tên ô ") + std::to_string(i + 1), d[i].title, [i](const std::string& t) { auto d2 = speedDial(); d2[i].title = t; save("speeddial.txt", d2); }); }},
        {tl("Xóa ô"), "", [i] { auto d = speedDial(); d[i] = {"", ""}; save("speeddial.txt", d); }},
        {tl("Menu trình duyệt"), "", mainMenu}});
  } else if (k == Key::Menu) mainMenu();
  else if (k == Key::Back) quit = true;
}

// ---------------- page interaction ----------------
// After a click: if the focused element is a text field, edit it with the on-screen keyboard.
void checkFocusedInput() {
  view.runJs(
      "(function(){var e=document.activeElement;if(!e)return '';var t=(e.tagName||'').toLowerCase();"
      "var ty=(e.type||'').toLowerCase();if(t=='textarea'||e.isContentEditable||(t=='input'&&['text','search','email','url','tel','password','number',''].indexOf(ty)>=0))"
      "return (ty=='password'?'P':'T')+(e.value||e.innerText||'');return '';})()",
      [](const std::string& r) {
        if (r.empty()) return;
        bool pw = r[0] == 'P';
        keyboard(pw ? tl("Nhập mật khẩu") : tl("Nhập nội dung"), r.substr(1), [](const std::string& v) {
          std::string esc;
          for (char c : v) { if (c == '\\' || c == '\'') esc += '\\'; esc += c; }
          view.runJs("(function(){var e=document.activeElement;if(!e)return;"
                     "if(e.isContentEditable)e.innerText='" + esc + "';else{var p=Object.getOwnPropertyDescriptor(e.__proto__,'value');"
                     "if(p&&p.set)p.set.call(e,'" + esc + "');else e.value='" + esc + "';}"
                     "e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}));})()",
                     [](const std::string&) { view.keyTap(XKB_KEY_Return); });
        });
      });
}
void queryHover() {
  char js[512];
  snprintf(js, sizeof js, "(function(){var e=document.elementFromPoint(%d,%d);while(e&&!(e.href||e.onclick||e.tagName=='BUTTON'||e.tagName=='INPUT'))e=e.parentElement;"
                          "if(!e)return '';return e.href?String(e.href):('#'+e.tagName.toLowerCase());})()", (int)curX, (int)curY);
  view.runJs(js, [](const std::string& r) { hoverHref = r; });
}
void pageKey(Key k) {
  switch (k) {
    case Key::OK:
      view.click((int)curX, (int)curY);
      clickCheckAt = SDL_GetTicks() + 350;
      break;
    case Key::L: view.scroll((int)curX, (int)curY, -PAGE_H * 3 / 4); break;
    case Key::R: view.scroll((int)curX, (int)curY, PAGE_H * 3 / 4); break;
    case Key::Menu: mainMenu(); break;
    case Key::Go: goAddress(); break;
    case Key::Home: if (appMode) quit = true; else onStart = true; break;
    case Key::Back: if (view.canBack()) view.back(); else if (appMode) quit = true; else onStart = true; break;
    default: break;
  }
}
// D-pad moves the pointer with acceleration; pushing past the top/bottom edge scrolls the page.
void movePointer(double dt) {
  static const int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};
  bool moved = false;
  for (int d = 0; d < 4; d++) {
    if (!dirHeld[d]) continue;
    dirHeld[d] += (int)(dt * 1000);
    float speed = std::min(900.0f, 160.0f + dirHeld[d] * 0.9f);  // px per second
    float step = speed * (float)dt;
    curX += DX[d] * step;
    curY += DY[d] * step;
    moved = true;
  }
  if (!moved) return;
  if (curY < 4) { view.scroll((int)curX, 4, (int)(curY - 4) * 2); curY = 4; }
  if (curY > PAGE_H - 4) { view.scroll((int)curX, PAGE_H - 4, (int)(curY - (PAGE_H - 4)) * 2); curY = PAGE_H - 4; }
  curX = std::max(2.0f, std::min((float)W - 2, curX));
  view.pointerMove((int)curX, (int)curY);
  hoverDirty = true;
}
void drawPointer() {
  int x = (int)curX, y = PAGE_Y + (int)curY;
  bool link = !hoverHref.empty();
  // arrow cursor: white with dark outline; hand-coloured when over a link
  static const char* ARROW[] = {"X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X", "X.......X", "X....XXXX", "X..X..X", "X.X X..X", "XX  X..X", "X    XX"};
  for (int r = 0; r < 14; r++)
    for (int c = 0; ARROW[r][c]; c++) {
      char ch = ARROW[r][c];
      if (ch == 'X') rect(x + c, y + r, 1, 1, {20, 20, 20});
      else if (ch == '.') rect(x + c, y + r, 1, 1, link ? C{255, 200, 40} : WHITE);
    }
}

// ---------------- chrome ----------------
void drawChrome(const std::string& sl, const std::string& sr, const std::string& center) {
  grad(0, 0, W, TOP_H, BLUE2, BLUE1);
  std::string title = onStart ? tl("Trang khởi động") : (view.loading() && view.title().empty() ? host(view.uri()) : pageTitle());
  time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
  char clk[8]; snprintf(clk, sizeof clk, "%02d:%02d", lt.tm_hour, lt.tm_min);
  int pct, dls = runningDownloads(&pct);
  std::string dl = dls ? (pct >= 0 ? tl("Tải ") + std::to_string(pct) + "%" : tl("Đang tải")) : "";
  int dlw = dl.empty() ? 0 : textW(dl, 13, true) + 16;
  text(title, 10, 3, 15, WHITE, true, 0, W - 80 - dlw);
  text(clk, W - 10, 3, 15, WHITE, true, 2);
  if (dlw) { rect(W - 62 - dlw, 5, dlw - 6, 18, {255, 255, 255, 60}); text(dl, W - 65 - dlw / 2, 5, 13, WHITE, true, 1); }
  if (!onStart && view.loading()) {
    int p = (int)(view.progress() * W);
    rect(0, TOP_H, W, 3, {200, 215, 235}); rect(0, TOP_H, std::max(8, p), 3, BLUE2);
    if (overlays.empty()) {  // the thin bar above stays; the badge would cover open menus
      char b[32]; snprintf(b, sizeof b, tl("Đang tải… %d%%"), (int)(view.progress() * 100));
      int bw = 150; rect(W - bw - 8, H - SOFT_H - 30, bw, 24, {20, 25, 35, 220});
      text(b, W - bw / 2 - 8, H - SOFT_H - 28, 14, WHITE, false, 1);
    }
  }
  grad(0, H - SOFT_H, W, SOFT_H, {59, 66, 78}, {14, 17, 23});
  text(sl, 12, H - SOFT_H + 8, 18, WHITE, true);
  text(sr, W - 12, H - SOFT_H + 8, 18, WHITE, true, 2);
  if (!center.empty()) { int w = std::max(100, textW(center, 16, true) + 30); grad(W / 2 - w / 2, H - SOFT_H + 6, w, 28, HL1, HL2); text(center, W / 2, H - SOFT_H + 9, 16, WHITE, true, 1); }
  if (SDL_GetTicks() < toastUntil) {
    int w = textW(toastMsg, 15, true) + 30, ty = H - SOFT_H - 46;
    rect(W / 2 - w / 2, ty, w, 34, {15, 18, 24, 235});
    text(toastMsg, W / 2, ty + 6, 15, WHITE, true, 1);
  }
}

Key mapKey(SDL_Keycode k) {
  switch (k) {
    case SDLK_UP: return Key::Up; case SDLK_DOWN: return Key::Down; case SDLK_LEFT: return Key::Left; case SDLK_RIGHT: return Key::Right;
    case SDLK_SPACE: return Key::OK; case SDLK_RETURN: case SDLK_z: return Key::Menu; case SDLK_ESCAPE: case SDLK_x: case SDLK_BACKSPACE: return Key::Back;
    case SDLK_g: return Key::Go; case SDLK_h: return Key::Home; case SDLK_q: case SDLK_PAGEUP: return Key::L; case SDLK_e: case SDLK_PAGEDOWN: return Key::R;
    default: return Key::None;
  }
}
// R36S: A = OK/click, B = back, X/SELECT = Menu (left softkey), Y = go to address, START = start page, L1/R1 = page up/down
Key mapButton(Uint8 b) {
  switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return Key::Up; case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return Key::Down;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return Key::Left; case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Key::Right;
    case SDL_CONTROLLER_BUTTON_A: return Key::OK; case SDL_CONTROLLER_BUTTON_B: return Key::Back;
    case SDL_CONTROLLER_BUTTON_X: case SDL_CONTROLLER_BUTTON_BACK: return Key::Menu; case SDL_CONTROLLER_BUTTON_Y: return Key::Go;
    case SDL_CONTROLLER_BUTTON_START: return Key::Home; case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return Key::L; case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return Key::R;
    default: return Key::None;
  }
}
Uint32 fsHintUntil = 0;
int dirIndex(Key k) { return k == Key::Up ? 0 : k == Key::Down ? 1 : k == Key::Left ? 2 : k == Key::Right ? 3 : -1; }
void handleKey(Key k, bool down = true) {
  if (k == Key::None) return;
  if (view.fullscreen() && overlays.empty()) {  // video player controls
    if (!down) return;
    switch (k) {
      case Key::OK: videoJs("v.paused?v.play():v.pause();"); break;
      case Key::Left: videoJs("v.currentTime=Math.max(0,v.currentTime-10);"); break;
      case Key::Right: videoJs("v.currentTime=Math.min(v.duration||1e9,v.currentTime+10);"); break;
      case Key::Up: videoJs("v.muted=false;v.volume=Math.min(1,v.volume+0.1);"); break;
      case Key::Down: videoJs("v.volume=Math.max(0,v.volume-0.1);"); break;
      case Key::L: videoJs("v.currentTime=Math.max(0,v.currentTime-60);"); break;
      case Key::R: videoJs("v.currentTime=Math.min(v.duration||1e9,v.currentTime+60);"); break;
      case Key::Back: case Key::Menu: view.exitFullscreen(); break;
      default: break;
    }
    fsHintUntil = SDL_GetTicks() + 2500;
    return;
  }
  int d = dirIndex(k);
  bool pageMode = overlays.empty() && !onStart;
  if (d >= 0 && pageMode) { dirHeld[d] = down ? 1 : 0; return; }  // pointer movement is continuous
  if (!down) return;
  if (!overlays.empty()) { if (!overlays.back()->key(k)) overlays.pop_back(); return; }
  if (onStart) startKey(k); else pageKey(k);
}

// automated tests: S40WEB_SCRIPT="wait:1000,url:https://...,wait:5000,shot:/tmp/a.png,quit"
std::vector<std::string> script; size_t scriptPos = 0; Uint32 waitUntil = 0; std::string shot; Key scriptHeld = Key::None; Uint32 scriptHoldUntil = 0;
void stepScript() {
  if (scriptHeld != Key::None && SDL_GetTicks() >= scriptHoldUntil) { handleKey(scriptHeld, false); scriptHeld = Key::None; }
  while (scriptPos < script.size() && SDL_GetTicks() >= waitUntil && shot.empty() && scriptHeld == Key::None) {
    std::string c = script[scriptPos++], op = c.substr(0, c.find(':')), arg = c.find(':') == std::string::npos ? "" : c.substr(c.find(':') + 1);
    static const std::map<std::string, Key> K = {{"up", Key::Up}, {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right}, {"OK", Key::OK},
                                                  {"B", Key::Back}, {"menu", Key::Menu}, {"go", Key::Go}, {"home", Key::Home}, {"L", Key::L}, {"R", Key::R}};
    if (op == "wait") waitUntil = SDL_GetTicks() + atoi(arg.c_str());
    else if (op == "key" && K.count(arg)) { handleKey(K.at(arg)); handleKey(K.at(arg), false); waitUntil = SDL_GetTicks() + 120; }
    else if (op == "hold") { auto p = arg.find('@'); Key k = K.count(arg.substr(0, p)) ? K.at(arg.substr(0, p)) : Key::None; handleKey(k); scriptHeld = k; scriptHoldUntil = SDL_GetTicks() + atoi(arg.substr(p + 1).c_str()); }
    else if (op == "url") openUrl(arg);
    else if (op == "videofs") videoFullscreen();
    else if (op == "dl") view.download(arg);
    else if (op == "dlmenu") downloadsMenu();
    else if (op == "ptr") { curX = (float)atoi(arg.c_str()); curY = (float)atoi(arg.substr(arg.find_first_of(";:") + 1).c_str()); view.pointerMove((int)curX, (int)curY); hoverDirty = true; }
    else if (op == "type" && !overlays.empty()) { if (auto* kb = dynamic_cast<Keyboard*>(overlays.back().get())) kb->kb.text = arg; }
    else if (op == "keys" && !overlays.empty()) {  // type through the input engine (Telex / VNI / Pinyin)
      if (auto* kb = dynamic_cast<Keyboard*>(overlays.back().get())) { kb->ensurePinyin(); for (char ch : arg) kb->kb.typeChar(std::string(1, ch)); }
    }
    else if (op == "shot") shot = arg;
    else if (op == "quit") quit = true;
  }
}
void saveShot() {
  if (shot.empty()) return;
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
  SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
  IMG_SavePNG(s, shot.c_str());
  SDL_FreeSurface(s);
  shot.clear();
}

}  // namespace web

using namespace web;

int main(int argc, char** argv) {
  std::string startUrl, listsDir = "/opt/s40web/adblock";
  bool windowed = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--window") windowed = true;
    else if (a == "--app") appMode = true;
    else if (a == "--fonts" && i + 1 < argc) fontDir = argv[++i];
    else if (a == "--data" && i + 1 < argc) dataDir = argv[++i];
    else if (a == "--lists" && i + 1 < argc) listsDir = argv[++i];
    else if (a == "--abcompile" && i + 1 < argc) return Adblock::compileTest(argv[i + 1], "/tmp/s40web-abtest");
    else if (a == "--abp2wk" && i + 2 < argc) {  // test/bundle helper: s40web --abp2wk out.json list1.txt [list2.txt ...]
      std::vector<std::string> texts;
      for (int j = i + 2; j < argc; j++) { std::ifstream f(argv[j], std::ios::binary); std::stringstream b; b << f.rdbuf(); texts.push_back(b.str()); }
      abp::Result r = abp::convert(texts);
      std::ofstream(argv[i + 1], std::ios::binary) << r.filterJson;
      size_t spec = 0;
      for (auto& kv : r.specific) spec += kv.second.size();
      printf("hosts=%d url=%d exceptions=%d hiding=%d (generic %zu, site %zu in %zu domains) skipped=%d json=%zu\n", r.stats.hosts, r.stats.network,
             r.stats.exceptions, r.stats.cosmetic, r.generic.size(), spec, r.specific.size(), r.stats.skipped, r.filterJson.size());
      return 0;
    }
    else startUrl = a;
  }
  const char* home = getenv("HOME");
  if (dataDir.empty()) dataDir = std::string(home ? home : "/tmp") + "/.config/s40web";
  if (fontDir.empty()) fontDir = "/opt/s40/assets/fonts";
  mkdir((std::string(home ? home : "/tmp") + "/.config").c_str(), 0755);
  mkdir(dataDir.c_str(), 0755);
  mkdir((dataDir + "/cache").c_str(), 0755);
  loadCfg();

  // WebKit's web process sandbox needs bubblewrap with user namespaces and $XDG_RUNTIME_DIR.
  // Keep it when it works; old handheld kernels (RK3326 BSP 4.4) usually lack user namespaces.
  if (!getenv("XDG_RUNTIME_DIR") || access(getenv("XDG_RUNTIME_DIR"), W_OK) != 0) {
    std::string rt = "/tmp/s40web-run-" + std::to_string(getuid());
    mkdir(rt.c_str(), 0700);
    setenv("XDG_RUNTIME_DIR", rt.c_str(), 1);
  }
  // 1 GB device: compressed swap in RAM (same approach dArkOS uses for PPSSPP/Dolphin). Device only.
  bool onDevice = access("/home/ark/.config/.DEVICE", F_OK) == 0;
  if (onDevice && system("zramctl 2>/dev/null | grep -q zram0") != 0) {
    int rc = system("sudo modprobe zram num_devices=1 && echo lz4 | sudo tee /sys/block/zram0/comp_algorithm >/dev/null && "
                    "echo 512M | sudo tee /sys/block/zram0/disksize >/dev/null && sudo mkswap /dev/zram0 >/dev/null && sudo swapon /dev/zram0 -p 100");
    if (rc != 0) fprintf(stderr, "s40web: zram swap not enabled (rc=%d)\n", rc);
  }
  if (system("bwrap --ro-bind / / --dev /dev true >/dev/null 2>&1") != 0) {
    fprintf(stderr, "s40web: bubblewrap sandbox unavailable on this kernel, running WebKit without it\n");
    setenv("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS", "1", 1);
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  TTF_Init(); IMG_Init(IMG_INIT_PNG);
  win = SDL_CreateWindow("s40web", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, windowed ? 0 : SDL_WINDOW_FULLSCREEN);
  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  SDL_RenderSetLogicalSize(ren, W, H);
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_ShowCursor(SDL_DISABLE);
  for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
  pageTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, PAGE_H);

  {  // ad-block host filter: web-process extension shipped next to the binary
    char exe[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    std::string exeDir = n > 0 ? std::string(exe, n) : "";
    exeDir = exeDir.substr(0, exeDir.rfind('/'));
    if (access((exeDir + "/ext/s40abext.so").c_str(), R_OK) == 0) {
      view.extensionsDir = exeDir + "/ext";
      view.extensionData = dataDir + "/adblock";
    } else fprintf(stderr, "s40web: %s/ext/s40abext.so missing, host blocking disabled\n", exeDir.c_str());
    // WebKit's web/network processes need the same GBM shim the shell is linked with (libmali lacks
    // functions the BIND_NOW-linked WebKit requires); they inherit it through LD_PRELOAD.
    std::string shim = exeDir + "/lib/libs40gbm.so";
    if (access(shim.c_str(), R_OK) == 0) {
      const char* pre = getenv("LD_PRELOAD");
      setenv("LD_PRELOAD", pre && *pre ? (shim + ":" + pre).c_str() : shim.c_str(), 1);
    }
  }
  {  // The device's libEGL/libGLESv2 are Mali (libmali, gbm only). WebKit's web process needs an EGL with the
     // wayland platform (it renders into the shell's wpe-fdo surface), so without this it aborts with
     // "Could not create WPE EGL display" and no page ever paints. Mesa's EGL (GLVND + Mesa ICD, software)
     // is given to the web/network processes only: the shell above already loaded Mali for its own renderer,
     // and the loader reads these variables at process start, so only the children pick them up.
    char exe[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    std::string exeDir = n > 0 ? std::string(exe, n) : "";
    exeDir = exeDir.substr(0, exeDir.rfind('/'));
    std::string glvnd = exeDir + "/glvnd";
    const char* vendor = "/usr/share/glvnd/egl_vendor.d/50_mesa.json";
    if (access((glvnd + "/libEGL.so.1").c_str(), R_OK) == 0 && access(vendor, R_OK) == 0) {
      const char* ld = getenv("LD_LIBRARY_PATH");
      setenv("LD_LIBRARY_PATH", ld && *ld ? (glvnd + ":" + ld).c_str() : glvnd.c_str(), 1);
      setenv("__EGL_VENDOR_LIBRARY_FILENAMES", vendor, 1);
      setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
    }
  }
  if (!view.init(W, PAGE_H, dataDir, dataDir + "/cache")) { fprintf(stderr, "WPE WebKit init failed\n"); return 1; }
  applySettings();
  adblock.notify = [](const std::string& m) { toast(m); };
  view.onLoadStart = [](const std::string& uri) { adblock.onNavigate(uri); };
  adblock.init(view.webView(), dataDir, listsDir);
  view.downloadDest = downloadPath;
  view.onDownloadEvent = [](const WpeView::Download& d) {
    if (d.state == WpeView::Download::Running) toast(tl("Đang tải: ") + d.name);
    else if (d.state == WpeView::Download::Done) toast(tl("Đã tải xong: ") + d.name);
    else if (d.state == WpeView::Download::Failed) toast(tl("Tải lỗi: ") + d.name);
  };
  view.onFullscreen = [](bool on) {
    view.resize(W, on ? H : PAGE_H);
    for (auto& d : dirHeld) d = 0;
    fsHintUntil = on ? SDL_GetTicks() + 4000 : 0;
  };
  if (!startUrl.empty()) openUrl(startUrl);
  if (const char* sc = getenv("S40WEB_SCRIPT")) { std::stringstream ss(sc); std::string p; while (std::getline(ss, p, ',')) script.push_back(p); }

  Key held = Key::None; Uint32 repeatAt = 0, last = SDL_GetTicks();
  while (!quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) quit = true;
      else if (e.type == SDL_KEYDOWN && !e.key.repeat) handleKey(mapKey(e.key.keysym.sym));
      else if (e.type == SDL_KEYUP) handleKey(mapKey(e.key.keysym.sym), false);
      else if (e.type == SDL_CONTROLLERBUTTONDOWN) { Key k = mapButton(e.cbutton.button); handleKey(k); held = k; repeatAt = SDL_GetTicks() + 380; }
      else if (e.type == SDL_CONTROLLERBUTTONUP) { handleKey(mapButton(e.cbutton.button), false); held = Key::None; }
      else if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
    }
    // menu/list/keyboard navigation auto-repeat (pointer movement is handled continuously)
    if (held != Key::None && dirIndex(held) >= 0 && (!overlays.empty() || onStart) && SDL_GetTicks() >= repeatAt) { handleKey(held); repeatAt = SDL_GetTicks() + 90; }
    stepScript();
    Uint32 now = SDL_GetTicks();
    double dt = (now - last) / 1000.0;
    last = now;

    view.pump();
    adblock.poll();
    if (!onStart && overlays.empty()) {
      movePointer(dt);
      if (hoverDirty && now - lastHoverQuery > 180) { hoverDirty = false; lastHoverQuery = now; queryHover(); }
      if (clickCheckAt && now >= clickCheckAt) { clickCheckAt = 0; checkFocusedInput(); }
    }
    // history
    if (!onStart && !view.loading() && view.error().empty() && !view.uri().empty() && view.uri() != lastLoggedUrl && view.uri().rfind("file://", 0) != 0) {
      lastLoggedUrl = view.uri();
      auto h = load("history.txt");
      h.erase(std::remove_if(h.begin(), h.end(), [&](const Entry& x) { return x.url == lastLoggedUrl; }), h.end());
      h.push_back({pageTitle(), lastLoggedUrl});
      if (h.size() > 100) h.erase(h.begin(), h.begin() + (h.size() - 100));
      save("history.txt", h);
    }
    if (view.frameDirty()) {
      static int texW = W, texH = PAGE_H;
      if (view.frameW() != texW || view.frameH() != texH) {
        SDL_DestroyTexture(pageTex);
        texW = view.frameW(); texH = view.frameH();
        pageTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, texW, texH);
      }
      SDL_UpdateTexture(pageTex, nullptr, view.pixels(), texW * 4);
      view.clearDirty();
    }

    if (view.fullscreen() && overlays.empty()) {
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
      SDL_RenderClear(ren);
      SDL_Rect d{0, 0, W, H};
      SDL_RenderCopy(ren, pageTex, nullptr, &d);
      if (SDL_GetTicks() < fsHintUntil) {
        rect(0, H - 34, W, 34, {0, 0, 0, 170});
        text(tl("A: dừng/phát · Trái/Phải: tua 10s · L/R: 1 phút · Lên/Xuống: âm lượng · B: thoát"), W / 2, H - 28, 14, WHITE, false, 1);
      }
      if (SDL_GetTicks() < toastUntil) { int w = textW(toastMsg, 15, true) + 30; rect(W / 2 - w / 2, 10, w, 34, {15, 18, 24, 235}); text(toastMsg, W / 2, 16, 15, WHITE, true, 1); }
      saveShot();
      SDL_RenderPresent(ren);
      view.framePresented();
      { static Uint32 prev = 0; Uint32 t = SDL_GetTicks(); if (t - prev < 16) SDL_Delay(16 - (t - prev)); prev = SDL_GetTicks(); }
      continue;
    }
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderClear(ren);
    std::string sl = "Menu", sr = tl("Quay lại"), center;
    if (onStart) { drawStart(); sr = tl("Thoát"); center = startSel == 0 ? tl("Tìm") : startSel == 1 ? tl("Nhập") : tl("Mở"); }
    else {
      SDL_Rect d{0, PAGE_Y, W, PAGE_H};
      SDL_RenderCopy(ren, pageTex, nullptr, &d);
      if (!view.error().empty() && !view.loading()) {
        rect(30, 150, W - 60, 120, {255, 240, 240}); frame(30, 150, W - 60, 120, {220, 80, 80});
        text(tl("Không mở được trang"), W / 2, 165, 18, {180, 30, 30}, true, 1);
        text(view.error(), W / 2, 200, 14, TXT, false, 1, W - 90);
        text(tl("B: quay lại · Menu › Tải lại: thử lại"), W / 2, 232, 13, SUB, false, 1);
      }
      if (overlays.empty()) {
        drawPointer();
        // on a page A always clicks where the pointer is: no centre key label, only the link target
        center.clear();
        if (!hoverHref.empty() && hoverHref[0] != '#') {
          rect(0, H - SOFT_H - 22, W, 22, {240, 244, 250, 235}); text(hoverHref, 8, H - SOFT_H - 21, 13, SUB, false, 0, W - 16);
        }
      }
    }
    for (auto& o : overlays) o->draw();
    if (!overlays.empty()) { sl = overlays.back()->softL(); sr = overlays.back()->softR(); center.clear(); }
    drawChrome(sl, sr, center);
    saveShot();
    SDL_RenderPresent(ren);
    view.framePresented();
    // pace the loop to ~60 fps even when the renderer has no vsync (software / offscreen)
    { static Uint32 prev = 0; Uint32 t = SDL_GetTicks(); if (t - prev < 16) SDL_Delay(16 - (t - prev)); prev = SDL_GetTicks(); }
  }
  view.shutdown();
  SDL_Quit();
  return 0;
}
