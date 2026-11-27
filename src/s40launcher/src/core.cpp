// Core: settings store, graphics, screen stack, chrome (status/title/softkeys), input, main loop.
#include "s40.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace s40 {

// ================= colors / themes =================
Col hex(const char* s) {
  if (*s == '#') s++;
  unsigned v = (unsigned)strtoul(s, nullptr, 16);
  return {Uint8(v >> 16), Uint8(v >> 8), Uint8(v)};
}
Col mix(Col a, Col b, float t) {
  return {Uint8(a.r + (b.r - a.r) * t), Uint8(a.g + (b.g - a.g) * t), Uint8(a.b + (b.b - a.b) * t), Uint8(a.a + (b.a - a.a) * t)};
}
Col lighten(Col c, int d) { return {Uint8(std::min(255, c.r + d)), Uint8(std::min(255, c.g + d)), Uint8(std::min(255, c.b + d)), c.a}; }

std::vector<Theme> THEMES = {
    {tl("Xanh dương"), hex("0b3d91"), hex("2f7de1"), hex("5aaeff"), hex("1561c9"), hex("f6f9ff"), hex("dde8f7"), hex("10213a"), hex("5a6b85")},
    {tl("Cam"), hex("9a3b00"), hex("f07a1a"), hex("ffae5c"), hex("e0620a"), hex("fff9f2"), hex("f5e4d3"), hex("3a1f0a"), hex("85664c")},
    {tl("Xanh lá"), hex("1d5e20"), hex("43a047"), hex("82d885"), hex("2e8b32"), hex("f5fbf4"), hex("dbeeda"), hex("122b13"), hex("58735a")},
    {tl("Tím"), hex("4a148c"), hex("8e44d6"), hex("c592ff"), hex("7b2fc9"), hex("faf6ff"), hex("e8ddf6"), hex("2a1240"), hex("6f5a85")},
    {tl("Đêm"), hex("0f131a"), hex("2b3442"), hex("4b95ff"), hex("1f5fc4"), hex("1c222c"), hex("11151c"), hex("e8edf5"), hex("8d99ab")},
};
const Theme& theme() { return THEMES[std::max(0, std::min((int)THEMES.size() - 1, S.geti("theme", 0)))]; }

// ================= store =================
Store S;
void Store::load(const std::string& p) {
  path = p;
  std::ifstream f(p);
  std::string line;
  while (std::getline(f, line)) {
    auto e = line.find('=');
    if (e != std::string::npos) v[line.substr(0, e)] = line.substr(e + 1);
  }
}
void Store::save() {
  std::string tmp = path + ".tmp";
  std::ofstream f(tmp);
  for (auto& kv : v) f << kv.first << '=' << kv.second << '\n';
  f.close();
  rename(tmp.c_str(), path.c_str());
}
std::string Store::get(const std::string& k, const std::string& d) const { auto it = v.find(k); return it == v.end() ? d : it->second; }
int Store::geti(const std::string& k, int d) const { auto it = v.find(k); return it == v.end() ? d : atoi(it->second.c_str()); }
bool Store::getb(const std::string& k, bool d) const { auto it = v.find(k); return it == v.end() ? d : it->second == "1"; }
void Store::set(const std::string& k, const std::string& val) { v[k] = val; save(); applySetting(k); }

// ================= graphics =================
Gfx G;
static inline void setCol(Col c) { SDL_SetRenderDrawColor(G.ren, c.r, c.g, c.b, c.a); }

bool Gfx::init(const std::string& assets, bool windowed, int scale) {
  assetDir = assets;
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) return false;
  TTF_Init();
  IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
  Uint32 flags = windowed ? 0 : SDL_WINDOW_FULLSCREEN;
  win = SDL_CreateWindow("S40", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * scale, H * scale, flags);
  if (!win) return false;
  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) return false;
  SDL_RenderSetLogicalSize(ren, W, H);
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_ShowCursor(SDL_DISABLE);
  return true;
}
void Gfx::flushTextCache() {
  for (auto& t : textCache) SDL_DestroyTexture(t.second);
  textCache.clear();
}
void Gfx::shutdown() {
  flushTextCache();
  for (auto& t : icons) SDL_DestroyTexture(t.second);
  for (auto& t : images) if (t.second) SDL_DestroyTexture(t.second);
  icons.clear(); images.clear();
  for (auto& f : fonts) TTF_CloseFont(f.second);
  fonts.clear();
  if (ren) SDL_DestroyRenderer(ren);
  if (win) SDL_DestroyWindow(win);
  ren = nullptr; win = nullptr;
}
void Gfx::clip(const SDL_Rect* r) { SDL_RenderSetClipRect(ren, r); }
void Gfx::rect(int x, int y, int w, int h, Col c) { setCol(c); SDL_Rect r{x, y, w, h}; SDL_RenderFillRect(ren, &r); }
void Gfx::line(int x1, int y1, int x2, int y2, Col c) { setCol(c); SDL_RenderDrawLine(ren, x1, y1, x2, y2); }

// rounded rect with vertical gradient as a triangle fan.
// Rounded shapes are anti-aliased: the solid fill stops half a pixel inside the true outline and a
// 1 px strip fading to transparent straddles it, so nested shapes (border + fill, frame + header)
// meet cleanly instead of showing stair-stepped, mismatched corners.
void Gfx::grad(int x, int y, int w, int h, Col top, Col bot, int radius) {
  if (w <= 0 || h <= 0) return;
  radius = std::min(radius, std::min(w, h) / 2);
  auto colAt = [&](float py) { return mix(top, bot, std::max(0.f, std::min(1.f, (py - y) / (float)h))); };
  if (radius <= 0) {  // plain rectangle: keep crisp pixel edges
    SDL_FPoint p[4] = {{(float)x, (float)y}, {(float)(x + w), (float)y}, {(float)(x + w), (float)(y + h)}, {(float)x, (float)(y + h)}};
    std::vector<SDL_Vertex> v;
    auto vert = [&](SDL_FPoint q) { Col c = colAt(q.y); v.push_back({q, {c.r, c.g, c.b, c.a}, {0, 0}}); };
    for (int t : {0, 1, 2, 0, 2, 3}) vert(p[t]);
    SDL_RenderGeometry(ren, nullptr, v.data(), (int)v.size(), nullptr, 0);
    return;
  }
  const float r = (float)radius, in = r - 0.5f;          // fill radius (half a pixel inside)
  const int seg = std::max(4, std::min(24, radius));     // ~1 segment per pixel of radius
  std::vector<SDL_FPoint> pts, nrm;
  auto corner = [&](float cx, float cy, float a0) {
    for (int i = 0; i <= seg; i++) {
      float a = a0 + (float)M_PI / 2 * i / seg, ca = cosf(a), sa = sinf(a);
      pts.push_back({cx + ca * in, cy + sa * in});
      nrm.push_back({ca, sa});
    }
  };
  corner(x + w - r, y + r, -M_PI / 2);
  corner(x + w - r, y + h - r, 0);
  corner(x + r, y + h - r, M_PI / 2);
  corner(x + r, y + r, M_PI);
  std::vector<SDL_Vertex> v;
  v.reserve(pts.size() * 9);
  auto vert = [&](SDL_FPoint p, Uint8 alphaScale) {
    Col c = colAt(p.y);
    v.push_back({p, {c.r, c.g, c.b, (Uint8)(c.a * alphaScale / 255)}, {0, 0}});
  };
  SDL_FPoint ctr{x + w / 2.f, y + h / 2.f};
  size_t n = pts.size();
  for (size_t i = 0; i < n; i++) {
    size_t j = (i + 1) % n;
    vert(ctr, 255); vert(pts[i], 255); vert(pts[j], 255);
    // anti-aliasing fringe: inner edge opaque, outer edge (1 px further out) transparent
    SDL_FPoint oi{pts[i].x + nrm[i].x, pts[i].y + nrm[i].y}, oj{pts[j].x + nrm[j].x, pts[j].y + nrm[j].y};
    vert(pts[i], 255); vert(oi, 0); vert(pts[j], 255);
    vert(pts[j], 255); vert(oi, 0); vert(oj, 0);
  }
  SDL_RenderGeometry(ren, nullptr, v.data(), (int)v.size(), nullptr, 0);
}
void Gfx::round(int x, int y, int w, int h, Col c, int radius) { grad(x, y, w, h, c, c, radius); }
void Gfx::circle(int cx, int cy, int r, Col c) { grad(cx - r, cy - r, r * 2, r * 2, c, c, r); }

TTF_Font* Gfx::font(int size, bool bold) {
  auto key = std::make_pair(size, bold);
  auto it = fonts.find(key);
  if (it != fonts.end()) return it->second;
  std::string p = assetDir + (bold ? "/fonts/NotoSans-Bold.ttf" : "/fonts/NotoSans-Regular.ttf");
  TTF_Font* f = TTF_OpenFont(p.c_str(), size);
  if (!f) f = TTF_OpenFont((assetDir + "/fonts/NotoSans-Regular.ttf").c_str(), size);
  fonts[key] = f;
  return f;
}
// Split text into runs: Noto Sans for normal text, DejaVu Sans for symbols Noto lacks (▲▼★✓♪...).
static TTF_Font* symbolFont(int size) {
  static std::map<int, TTF_Font*> fs;
  auto it = fs.find(size);
  if (it != fs.end()) return it->second;
  TTF_Font* f = TTF_OpenFont((G.assetDir + "/fonts/DejaVuSans.ttf").c_str(), size);
  fs[size] = f;
  return f;
}
// Han characters (Chinese keyboard, web titles): a CJK font installed on the system
static TTF_Font* cjkFont(int size) {
  static std::map<int, TTF_Font*> fs;
  auto it = fs.find(size);
  if (it != fs.end()) return it->second;
  TTF_Font* f = nullptr;
  for (const char* p : {"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"})
    if ((f = TTF_OpenFont(p, size))) break;
  fs[size] = f;
  return f;
}
static std::vector<std::pair<TTF_Font*, std::string>> textRuns(const std::string& s, TTF_Font* main, int size) {
  std::vector<std::pair<TTF_Font*, std::string>> runs;
  TTF_Font* sym = nullptr;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    Uint32 cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k < len && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    TTF_Font* f = main;
    if (cp >= 0x2000 && !TTF_GlyphIsProvided32(main, cp)) {
      if (!sym) sym = symbolFont(size);
      if (sym && TTF_GlyphIsProvided32(sym, cp)) f = sym;
      else if (TTF_Font* cjk = cjkFont(size)) f = cjk;
      else if (sym) f = sym;
    }
    if (runs.empty() || runs.back().first != f) runs.push_back({f, ""});
    runs.back().second += s.substr(i, len);
    i += len;
  }
  return runs;
}
int Gfx::textW(const std::string& s, int size, bool bold) {
  TTF_Font* f = font(size, bold);
  if (!f || s.empty()) return 0;
  int total = 0;
  for (auto& r : textRuns(s, f, size)) { int w = 0, h = 0; TTF_SizeUTF8(r.first, r.second.c_str(), &w, &h); total += w; }
  return total;
}
static std::string ellipsize(const std::string& s, int size, bool bold, int maxw) {
  if (maxw <= 0 || G.textW(s, size, bold) <= maxw) return s;
  std::string t = s;
  while (!t.empty()) {
    // drop one UTF-8 code point
    size_t n = t.size() - 1;
    while (n > 0 && (t[n] & 0xC0) == 0x80) n--;
    t.erase(n);
    if (G.textW(t + "…", size, bold) <= maxw) return t + "…";
  }
  return "…";
}
int Gfx::text(const std::string& s0, int x, int y, int size, Col c, bool bold, int align, int maxw) {
  if (s0.empty()) return 0;
  std::string s = ellipsize(s0, size, bold, maxw);
  char key[64];
  snprintf(key, sizeof key, "|%d|%d|%02x%02x%02x%02x", size, bold, c.r, c.g, c.b, c.a);
  std::string k = s + key;
  SDL_Texture* t;
  auto it = textCache.find(k);
  if (it != textCache.end()) t = it->second;
  else {
    if (textCache.size() > 600) flushTextCache();
    TTF_Font* f = font(size, bold);
    if (!f) return 0;
    auto runs = textRuns(s, f, size);
    SDL_Surface* surf = nullptr;
    if (runs.size() == 1) surf = TTF_RenderUTF8_Blended(runs[0].first, s.c_str(), {c.r, c.g, c.b, 255});
    else {
      // compose runs side by side on one surface, baseline-aligned via font ascent
      std::vector<SDL_Surface*> parts;
      int tw = 0, asc = 0, desc = 0;
      for (auto& r : runs) {
        SDL_Surface* p = TTF_RenderUTF8_Blended(r.first, r.second.c_str(), {c.r, c.g, c.b, 255});
        parts.push_back(p);
        if (p) tw += p->w;
        asc = std::max(asc, TTF_FontAscent(r.first));
        desc = std::max(desc, -TTF_FontDescent(r.first));
      }
      surf = SDL_CreateRGBSurfaceWithFormat(0, std::max(1, tw), asc + desc, 32, SDL_PIXELFORMAT_ARGB8888);
      SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_NONE);
      int x = 0;
      for (size_t i = 0; i < parts.size(); i++) {
        if (!parts[i]) continue;
        SDL_SetSurfaceBlendMode(parts[i], SDL_BLENDMODE_NONE);
        SDL_Rect d{x, asc - TTF_FontAscent(runs[i].first), parts[i]->w, parts[i]->h};
        SDL_BlitSurface(parts[i], nullptr, surf, &d);
        x += parts[i]->w;
        SDL_FreeSurface(parts[i]);
      }
      SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_BLEND);
    }
    if (!surf) return 0;
    t = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    SDL_SetTextureAlphaMod(t, c.a);
    textCache[k] = t;
  }
  int w, h;
  SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
  int dx = align == 1 ? x - w / 2 : align == 2 ? x - w : x;
  SDL_Rect d{dx, y, w, h};
  SDL_RenderCopy(ren, t, nullptr, &d);
  return w;
}
void Gfx::icon(const std::string& name, int x, int y, int size, Col c) {
  SDL_Texture* t;
  auto it = icons.find(name);
  if (it != icons.end()) t = it->second;
  else {
    t = IMG_LoadTexture(ren, (assetDir + "/icons/" + name + ".png").c_str());
    if (!t) t = IMG_LoadTexture(ren, (assetDir + "/icons/info.png").c_str());
    if (t) SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    icons[name] = t;
  }
  if (!t) return;
  SDL_SetTextureColorMod(t, c.r, c.g, c.b);
  SDL_SetTextureAlphaMod(t, c.a);
  SDL_Rect d{x, y, size, size};
  SDL_RenderCopy(ren, t, nullptr, &d);
}
void Gfx::badge(const std::string& ic, int x, int y, int size, Col c) {
  grad(x, y, size, size, lighten(c), c, size / 4);
  int is = size * 5 / 8;
  icon(ic, x + (size - is) / 2, y + (size - is) / 2, is, {255, 255, 255});
}
SDL_Texture* Gfx::image(const std::string& path) {
  auto it = images.find(path);
  if (it != images.end()) return it->second;
  if (images.size() > 40) {
    for (auto& t : images) if (t.second) SDL_DestroyTexture(t.second);
    images.clear();
  }
  SDL_Texture* t = IMG_LoadTexture(ren, path.c_str());
  images[path] = t;
  return t;
}
void Gfx::present() { SDL_RenderPresent(ren); }

// ================= helpers =================
const char* DAYS[7] = {tl("Chủ Nhật"), tl("Thứ Hai"), tl("Thứ Ba"), tl("Thứ Tư"), tl("Thứ Năm"), tl("Thứ Sáu"), tl("Thứ Bảy")};
struct tm nowTm() { time_t t = time(nullptr); struct tm r; localtime_r(&t, &r); return r; }
std::string fmtTime(int sec) {
  if (sec < 0) sec = 0;
  char b[16]; snprintf(b, sizeof b, "%d:%02d", sec / 60, sec % 60); return b;
}
std::string clockStr(bool big, std::string* ampm) {
  struct tm t = nowTm();
  char b[16];
  if (S.getb("h24", true)) { snprintf(b, sizeof b, "%02d:%02d", t.tm_hour, t.tm_min); return b; }
  int h = t.tm_hour % 12; if (!h) h = 12;
  snprintf(b, sizeof b, "%d:%02d", h, t.tm_min);
  std::string ap = t.tm_hour < 12 ? "SA" : "CH";
  if (ampm) { *ampm = ap; return b; }
  return std::string(b) + (big ? "" : " " + ap);
}
std::string dateNum(int d, int m, int y) {
  char b[24];
  std::string f = S.get("dateFmt", "dd/mm/yyyy");
  if (f == "mm/dd/yyyy") snprintf(b, sizeof b, "%02d/%02d/%d", m, d, y);
  else if (f == "yyyy-mm-dd") snprintf(b, sizeof b, "%d-%02d-%02d", y, m, d);
  else snprintf(b, sizeof b, "%02d/%02d/%d", d, m, y);
  return b;
}
std::string dateStr() { struct tm t = nowTm(); return std::string(DAYS[t.tm_wday]) + ", " + dateNum(t.tm_mday, t.tm_mon + 1, t.tm_year + 1900); }
std::string sh(const std::string& cmd) {
  std::string out;
  FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
  if (!p) return out;
  char buf[512];
  while (fgets(buf, sizeof buf, p)) out += buf;
  pclose(p);
  return out;
}
int run(const std::string& cmd) { int r = system(cmd.c_str()); return WIFEXITED(r) ? WEXITSTATUS(r) : -1; }
bool fileExists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
std::vector<std::string> listDir(const std::string& dir) {
  std::vector<std::string> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  while (auto* e = readdir(d)) if (e->d_name[0] != '.') out.push_back(e->d_name);
  closedir(d);
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
  return out;
}
std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> o; std::string cur;
  for (char c : s) { if (c == sep) { o.push_back(cur); cur.clear(); } else cur += c; }
  o.push_back(cur);
  return o;
}
std::string lower(const std::string& s) { std::string o = s; for (auto& c : o) c = (char)tolower((unsigned char)c); return o; }
bool isDevice() { return fileExists("/home/ark/.config/.DEVICE") || fileExists("/opt/s40/.device"); }
std::string romsBase() {
  if (const char* e = getenv("S40_ROMS")) return e;
  // "Switch to SD2 for Roms" rewrites every <path>/roms/ in es_systems.cfg to /roms2/ (and back)
  static std::string base; static Uint32 at = 0;
  if (!base.empty() && SDL_GetTicks() - at < 5000) return base;
  at = SDL_GetTicks();
  FILE* f = fopen("/etc/emulationstation/es_systems.cfg", "r");
  base = "/roms";
  if (f) {
    char line[512];
    while (fgets(line, sizeof line, f)) if (strstr(line, "<path>/roms2/")) { base = "/roms2"; break; }
    fclose(f);
  }
  return base;
}

// ================= screen stack =================
static std::vector<std::unique_ptr<Screen>> stack;
static std::vector<std::unique_ptr<Screen>> graveyard;  // delete popped screens after the frame
static bool animPending = false;
static Uint32 animStart = 0;
static std::string toastMsg, toastIcon;
static Uint32 toastUntil = 0;
static int exitCode = -1;
Uint32 lastInput = 0;
static void saveShot();

void push(Screen* s) {
  stack.emplace_back(s);
  if (!s->overlay) { animPending = true; }
  s->onEnter();
}
void pop() {
  if (stack.size() <= 1) return;
  Screen* s = stack.back().get();
  s->onExit();
  if (!s->overlay) animPending = true;
  graveyard.push_back(std::move(stack.back()));
  stack.pop_back();
}
void resetTo(Screen* s) {
  while (!stack.empty()) { stack.back()->onExit(); graveyard.push_back(std::move(stack.back())); stack.pop_back(); }
  push(s);
}
Screen* top() { return stack.empty() ? nullptr : stack.back().get(); }
size_t depth() { return stack.size(); }
void toast(const std::string& msg, const std::string& icon) { toastMsg = msg; toastIcon = icon; toastUntil = SDL_GetTicks() + 1600; }
void quitApp(int code) { exitCode = code; }

// ================= chrome =================
static void drawBattery(int x, int y, int pct) {
  G.round(x, y, 24, 12, {255, 255, 255}, 2);
  G.rect(x + 1, y + 1, 22, 10, {30, 34, 40});
  G.rect(x + 24, y + 3, 2, 6, {255, 255, 255});
  Col c = pct < 20 ? Col{255, 82, 82} : Col{124, 252, 154};
  G.rect(x + 2, y + 2, 20 * pct / 100, 8, c);
}
static void drawStatus() {
  G.grad(0, 0, W, STATUS_H, hex("2c323c"), hex("0b0e13"));
  int x = 8;
  Col wh{255, 255, 255};
  if (S.getb("wifi", true)) { G.icon(sys::wifiSsid().empty() ? "wifioff" : "wifi", x, 5, 18, wh); x += 22; }
  if (music::playing()) { G.icon("music", x, 5, 18, wh); x += 22; }
  if (S.geti("vol", 6) == 0) { G.icon("mute", x, 5, 18, wh); x += 22; }
  if (S.getb("alarmOn")) { G.icon("bell", x, 5, 18, wh); x += 22; }
  if (sys::headphone()) { G.icon("headphone", x, 5, 18, wh); x += 22; }
  if (sys::usbDevice()) { G.icon("usb", x, 5, 18, wh); x += 22; }
  int bat = sys::battery();
  std::string clk = clockStr();
  int cw = G.textW(clk, 15, true);
  G.text(clk, W - 10, 4, 15, wh, true, 2);
  int bx = W - 10 - cw - 36;
  drawBattery(bx, 8, bat);
  if (sys::charging()) G.icon("bolt", bx + 6, 6, 14, {255, 230, 80});
  if (S.getb("showBat", true)) G.text(std::to_string(bat) + "%", bx - 4, 4, 15, wh, true, 2);
}
static void drawTitle(Screen* s) {
  const Theme& t = theme();
  G.grad(0, STATUS_H, W, TITLE_H, t.a2, t.a1);
  G.rect(0, STATUS_H, W, 1, {255, 255, 255, 80});
  int x = 14;
  if (!s->ic.empty()) { G.icon(s->ic, x, STATUS_H + 9, 22, {255, 255, 255}); x += 32; }
  std::string info = s->info();
  int iw = info.empty() ? 0 : G.textW(info, 15) + 16;
  G.text(s->title(), x, STATUS_H + 7, 20, {255, 255, 255}, true, 0, W - x - iw - 14);
  if (!info.empty()) G.text(info, W - 14, STATUS_H + 11, 15, {235, 240, 250}, false, 2);
}
static void drawSoft(const Soft& sk) {
  const Theme& t = theme();
  int y = H - SOFT_H;
  G.grad(0, y, W, SOFT_H, hex("3b424e"), hex("0e1117"));
  G.rect(0, y, W, 1, {255, 255, 255, 36});
  bool help = S.getb("showHelp", true);
  Col wh{255, 255, 255};
  auto kb = [&](const std::string& k, int x) {
    int w = G.textW(k, 11, true) + 10;
    G.round(x, y + 18, w, 17, {255, 255, 255, 50}, 4);
    G.text(k, x + 5, y + 18, 11, wh, true);
    return w + 6;
  };
  std::string ka = S.get("keyA", "X"), kb2 = S.get("keyB", "B"), kok = S.get("keyOK", "A");
  if (!sk.l.empty()) { int x = 14; if (help) x += kb(ka, x); G.text(sk.l, x, y + 13, 19, wh, true, 0, 200); }
  if (!sk.r.empty()) {
    int w = G.textW(sk.r, 19, true);
    int x = W - 14;
    if (help) { int kw = G.textW(kb2, 11, true) + 10; x -= kw; kb(kb2, x); x -= 6; }
    G.text(sk.r, x - w, y + 13, 19, wh, true);
  }
  if (!sk.c.empty()) {
    int w = std::max(120, G.textW(sk.c, 17, true) + (help ? 60 : 36));
    int x = W / 2 - w / 2;
    G.grad(x, y + 9, w, 34, t.hl1, t.hl2, 17);
    int tx = W / 2;
    if (help) { int kw = G.textW(kok, 11, true) + 16; tx += kw / 2; kb(kok, tx - G.textW(sk.c, 17, true) / 2 - kw); }
    G.text(sk.c, tx, y + 14, 17, wh, true, 1);
  }
}
static void drawToast() {
  if (SDL_GetTicks() > toastUntil || toastMsg.empty()) return;
  int w = G.textW(toastMsg, 16, true) + 52, x = W / 2 - w / 2;
  G.round(x, 76, w, 38, {15, 18, 24, 235}, 10);
  G.icon(toastIcon, x + 14, 86, 18, {255, 255, 255});
  G.text(toastMsg, x + 40, 84, 16, {255, 255, 255}, true);
}

static void render() {
  size_t bi = 0;
  for (size_t i = stack.size(); i-- > 0;) if (!stack[i]->overlay) { bi = i; break; }
  Screen* base = stack[bi].get();
  Screen* t = stack.back().get();
  SDL_SetRenderDrawColor(G.ren, 0, 0, 0, 255);
  SDL_RenderClear(G.ren);
  SDL_Rect area;
  if (base->full) area = {0, 0, W, H};
  else if (base->noTitle) area = {0, STATUS_H, W, H - STATUS_H - SOFT_H};
  else area = {0, STATUS_H + TITLE_H, W, H - STATUS_H - TITLE_H - SOFT_H};
  if (!base->full) { const Theme& th = theme(); G.grad(area.x, area.y, area.w, area.h, th.bg1, th.bg2); }
  // slide-in transition
  int dx = 0;
  if (animPending) { animStart = SDL_GetTicks(); animPending = false; }
  std::string tr = S.get("transition", tl("Trượt"));
  Uint32 el = SDL_GetTicks() - animStart;
  if (tr == tl("Trượt") && el < 150) dx = (int)(18 * (1 - el / 150.0));
  SDL_Rect a2 = area; a2.x += dx;
  G.clip(&area);
  base->draw(a2);
  for (size_t i = bi + 1; i < stack.size(); i++) stack[i]->draw(area);
  G.clip(nullptr);
  if (!base->full) {
    drawStatus();
    if (!base->noTitle) drawTitle(base);
    drawSoft(t->soft());
  }
  drawToast();
  // brightness emulation for desktop builds (device uses the backlight)
  if (!isDevice()) {
    int b = S.geti("bri", 8);
    if (b < 10) G.rect(0, 0, W, H, {0, 0, 0, Uint8((10 - b) * 14)});
  }
  saveShot();
  G.present();
}

// ================= input =================
static Key mapKey(SDL_Keycode k) {
  switch (k) {
    case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;
    case SDLK_LEFT: return Key::Left;
    case SDLK_RIGHT: return Key::Right;
    case SDLK_RETURN: case SDLK_z: return Key::A;
    case SDLK_SPACE: return Key::OK;
    case SDLK_ESCAPE: case SDLK_x: case SDLK_BACKSPACE: return Key::B;
    case SDLK_q: return Key::L;
    case SDLK_e: return Key::R;
    default: return Key::None;
  }
}
// R36S: physical A = OK, physical B = back, X/SELECT = left softkey (Options), L1/R1 = music
static Key mapButton(Uint8 b) {
  switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return Key::Up;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return Key::Down;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return Key::Left;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Key::Right;
    case SDL_CONTROLLER_BUTTON_A: return Key::OK;
    case SDL_CONTROLLER_BUTTON_B: return Key::B;
    case SDL_CONTROLLER_BUTTON_X: case SDL_CONTROLLER_BUTTON_BACK: return Key::A;
    case SDL_CONTROLLER_BUTTON_Y: return Key::A;
    case SDL_CONTROLLER_BUTTON_START: return Key::OK;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return Key::L;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return Key::R;
    default: return Key::None;
  }
}
void keyClick();       // in screens.cpp
void checkIdle();      // in screens.cpp
void checkReminders(); // in apps.cpp

static void handleKey(Key k) {
  if (k == Key::None || stack.empty()) return;
  lastInput = SDL_GetTicks();
  keyClick();
  Screen* t = stack.back().get();
  if (!t->capture && (k == Key::L || k == Key::R)) {
    if (k == Key::L) music::toggle(); else music::next();
    return;
  }
  t->key(k);
}

void openControllers() {
  for (int i = 0; i < SDL_NumJoysticks(); i++)
    if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
}

// Test automation: S40_SCRIPT="wait:1500,shot:/tmp/a.png,key:A,wait:300,shot:/tmp/b.png,quit"
static std::vector<std::string> script;
static size_t scriptPos = 0;
static Uint32 scriptWaitUntil = 0;
static std::string pendingShot;
static Key keyFromName(const std::string& n) {
  static const std::map<std::string, Key> m = {{"up", Key::Up}, {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right}, {"A", Key::A}, {"B", Key::B}, {"OK", Key::OK}, {"L", Key::L}, {"R", Key::R}};
  auto it = m.find(n);
  return it == m.end() ? Key::None : it->second;
}
static void handleKey(Key k);
static void stepScript() {
  while (scriptPos < script.size() && SDL_GetTicks() >= scriptWaitUntil && pendingShot.empty()) {
    std::string cmd = script[scriptPos++];
    auto c = cmd.find(':');
    std::string op = cmd.substr(0, c), arg = c == std::string::npos ? "" : cmd.substr(c + 1);
    if (op == "wait") scriptWaitUntil = SDL_GetTicks() + atoi(arg.c_str());
    else if (op == "key") { handleKey(keyFromName(arg)); scriptWaitUntil = SDL_GetTicks() + 120; }
    else if (op == "shot") pendingShot = arg;
    else if (op == "type") oskSetText(arg);
    else if (op == "keys") oskTypeKeys(arg);
    else if (op == "quit") exitCode = 0;
  }
}
static void saveShot() {
  if (pendingShot.empty()) return;
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
  SDL_RenderReadPixels(G.ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
  IMG_SavePNG(s, pendingShot.c_str());
  SDL_FreeSurface(s);
  pendingShot.clear();
}

int mainLoop() {
  Uint32 last = SDL_GetTicks(), frames = 0;
  // S40_STATS=1: print frames drawn at exit; S40_NOTHROTTLE=1: old always-redraw behaviour (for comparison)
  bool stats = getenv("S40_STATS"), noThrottle = getenv("S40_NOTHROTTLE");
  Uint32 repeatAt = 0; Key held = Key::None;
  if (const char* sc = getenv("S40_SCRIPT")) script = split(sc, ',');
  auto onEvent = [&](const SDL_Event& e) {
    if (e.type == SDL_QUIT) exitCode = 0;
    else if (e.type == SDL_KEYDOWN && !e.key.repeat) handleKey(mapKey(e.key.keysym.sym));
    else if (e.type == SDL_KEYDOWN && e.key.repeat) { Key k = mapKey(e.key.keysym.sym); if (k >= Key::Up && k <= Key::Right) handleKey(k); }
    else if (e.type == SDL_CONTROLLERBUTTONDOWN) { Key k = mapButton(e.cbutton.button); handleKey(k); held = k; repeatAt = SDL_GetTicks() + 380; }
    else if (e.type == SDL_CONTROLLERBUTTONUP) held = Key::None;
    else if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
  };
  while (exitCode < 0) {
    stepScript();
    SDL_Event e;
    while (SDL_PollEvent(&e)) onEvent(e);
    // d-pad auto-repeat for controllers
    if (held >= Key::Up && held <= Key::Right && SDL_GetTicks() >= repeatAt) { handleKey(held); repeatAt = SDL_GetTicks() + 90; }
    Uint32 now = SDL_GetTicks();
    double dt = (now - last) / 1000.0;
    last = now;
    music::update();
    checkReminders();
    checkIdle();
    for (size_t i = 0; i < stack.size(); i++) stack[i]->tick(dt);
    graveyard.clear();
    if (stack.empty()) break;
    render(); frames++;
    if (stats) SDL_Delay(16);  // stands in for the device's 60 Hz vsync when rendering off-screen
    graveyard.clear();
    if (noThrottle) { if (!stack.back()->live && SDL_GetTicks() - lastInput > 1000) SDL_Delay(40); continue; }  // old behaviour
    // save power when idle: static screens drop to 10 fps, clock-style screens to their idleFps.
    // Waiting on the event queue keeps key presses instant.
    if (SDL_GetTicks() - lastInput > 1000 && SDL_GetTicks() > toastUntil && scriptPos >= script.size()) {
      Screen* s = stack.back().get();
      int fps = !s->live ? 10 : s->idleFps;
      // own wait loop instead of SDL_WaitEventTimeout: on KMSDRM that falls back to waking every 1 ms;
      // checking input every 30 ms keeps keys responsive at ~33 cheap wake-ups a second
      if (fps > 0) {
        Uint32 until = SDL_GetTicks() + 1000 / fps;
        while (SDL_GetTicks() < until) {
          SDL_PumpEvents();
          if (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0) { onEvent(e); break; }
          SDL_Delay(std::min<Uint32>(30, until - SDL_GetTicks()));
        }
      }
    }
  }
  if (stats) fprintf(stderr, "[stats] frames=%u seconds=%.1f\n", frames, SDL_GetTicks() / 1000.0);
  return exitCode < 0 ? 0 : exitCode;
}

}  // namespace s40

using namespace s40;

namespace s40 { namespace j2me { int run(const std::string& rom, bool dry); } }

int main(int argc, char** argv) {
  // headless J2ME runner (no UI): used by es_systems.cfg and by the launcher itself
  if (argc >= 3 && (std::string(argv[1]) == "--j2me" || std::string(argv[1]) == "--j2me-dry"))
    return s40::j2me::run(argv[2], std::string(argv[1]) == "--j2me-dry");
  bool windowed = false; int scale = 1;
  std::string assets = "/opt/s40/assets", conf;
  std::string shot;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--window") windowed = true;
    else if (a == "--scale" && i + 1 < argc) scale = atoi(argv[++i]);
    else if (a == "--assets" && i + 1 < argc) assets = argv[++i];
    else if (a == "--config" && i + 1 < argc) conf = argv[++i];
  }
  if (conf.empty()) {
    const char* home = getenv("HOME");
    std::string dir = std::string(home ? home : "/tmp") + "/.config/s40";
    mkdir((std::string(home ? home : "/tmp") + "/.config").c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    conf = dir + "/settings.cfg";
  }
  S.load(conf);
  if (!G.init(assets, windowed, scale)) { fprintf(stderr, "init failed: %s\n", SDL_GetError()); return 1; }
  openControllers();
  lastInput = SDL_GetTicks();
  for (auto& k : {"vol", "bri"}) applySetting(k);
  // programs started from here (PortMaster, terminal, apps) follow the UI language
  { const std::string& l = s40tr::lang(); setenv("LANG", l == "en" ? "en_US.UTF-8" : l == "zh" ? "zh_CN.UTF-8" : "vi_VN", 1); }
  // time zone chosen in Settings (the system default is set by the image build)
  if (!S.get("tz").empty() && fileExists("/usr/share/zoneinfo/" + S.get("tz"))) { setenv("TZ", S.get("tz").c_str(), 1); tzset(); }
  resetTo(BootScreen());
  int rc = mainLoop();
  music::stop();
  G.shutdown();
  SDL_Quit();
  return rc;
}
