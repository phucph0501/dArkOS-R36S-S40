// Screens: boot, home (standby), main menu, games, music, photos, video, internet apps, favorites, power, sleep.
#include "s40.h"
#include <SDL2/SDL_mixer.h>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <ctime>
#include <unistd.h>

namespace s40 {

extern Uint32 lastInput;
Screen* PlayerScreen();
Screen* SystemsScreen();
Screen* SettingsScreen();
Screen* DisplayPage();
Screen* AppsScreen();
Screen* CalendarApp();

void keyClick() {
  // short beep through SDL audio would conflict with the music device; keep silent on desktop
}

// ---------- favorites / recent (stored as lists in settings) ----------
static std::vector<std::string> getList(const std::string& k) {
  std::vector<std::string> o;
  for (auto& s : split(S.get(k), '\x1f')) if (!s.empty()) o.push_back(s);
  return o;
}
static void setList(const std::string& k, const std::vector<std::string>& v) {
  std::string s;
  for (auto& x : v) { if (!s.empty()) s += '\x1f'; s += x; }
  S.set(k, s);
}
bool isFav(const std::string& k) { auto v = getList("fav"); return std::find(v.begin(), v.end(), k) != v.end(); }
void toggleFav(const std::string& k) {
  auto v = getList("fav");
  auto it = std::find(v.begin(), v.end(), k);
  if (it != v.end()) { v.erase(it); toast(tl("Đã bỏ khỏi Yêu thích"), "star"); } else { v.push_back(k); toast(tl("Đã thêm vào Yêu thích"), "star"); }
  setList("fav", v);
}
std::string favLabel(const std::string& k) { return isFav(k) ? tl("Bỏ khỏi Yêu thích") : tl("Thêm vào Yêu thích"); }
void addRecent(const std::string& k) {
  auto v = getList("recent");
  v.erase(std::remove(v.begin(), v.end(), k), v.end());
  v.insert(v.begin(), k);
  if (v.size() > 15) v.resize(15);
  setList("recent", v);
}
static const Col SYS_COLORS[] = {hex("d84040"), hex("7a5cc7"), hex("3f51b5"), hex("6b8e23"), hex("37474f"), hex("5c6bc0"), hex("2e7d32"), hex("e67e22"), hex("00897b"), hex("ad1457")};
Col sysColor(const std::string& name) { unsigned h = 0; for (char c : name) h = h * 31 + (unsigned char)c; return SYS_COLORS[h % 10]; }
std::string romTitle(const std::string& f) {
  std::string b = f.substr(0, f.find_last_of('.'));
  auto p = b.find(" ("); if (p != std::string::npos && p > 0) b = b.substr(0, p);
  for (auto& c : b) if (c == '_') c = ' ';
  return b;
}

// ================= boot =================
struct Boot : Screen {
  double t = 0;
  Boot() { full = true; live = true; capture = true; }
  void tick(double dt) override {
    t += dt;
    if (t > 1.6) goHome();
  }
  static void goHome() {
    resetTo(HomeScreen());
    // first start after installing: time zone, keyboard, Wi-Fi, model, home text
    if (!S.getb("setupDone", false)) push(SetupWizard());
    // ROMs were switched to the second card, but it is not there: the game list would just be empty
    if (isDevice() && romsBase() == "/roms2" && sh("grep -c ' /roms2 ' /proc/mounts").find('1') != 0)
      toast(tl("Không thấy thẻ nhớ 2: cắm lại thẻ, hoặc Cài đặt › Trò chơi › Thẻ nhớ ROM"), "warn");
  }
  void draw(const SDL_Rect& a) override {
    G.grad(a.x, a.y, a.w, a.h, hex("1b2a4a"), hex("000000"));
    std::string t1 = homeTitle(), t2 = homeSub();
    if (!t1.empty()) G.text(t1, W / 2, 170, 64, hex("7fb8ff"), true, 1, W - 40);
    G.text(t2.empty() ? "Debian 13 trixie" : t2, W / 2, 260, 17, {200, 210, 225}, false, 1, W - 40);
    int on = (int)(t * 4) % 5;
    for (int i = 0; i < 5; i++) G.circle(W / 2 - 40 + i * 20, 310, 5, i <= on ? hex("2f7de1") : Col{47, 125, 225, 60});
  }
  void key(Key) override { goHome(); }
};
Screen* BootScreen() { return new Boot; }

// ================= sleep (big clock on black) =================
static bool radioActive();
// Standby screen: big clock on black with the backlight at minimum; after "Ngủ sâu sau màn hình chờ"
// the device goes into real suspend (deep sleep) unless music or radio is playing. Waking from
// suspend (power key) closes the screen.
struct SleepScr : Screen {
  Uint32 entered = 0;
  time_t lastWall = 0;
  bool suspended = false;
  SleepScr() { full = true; live = true; capture = true; idleFps = 1; }
  void tick(double) override {
    time_t now = time(nullptr);
    if (suspended && lastWall && now - lastWall > 5) { pop(); return; }  // woke up from deep sleep
    lastWall = now;
    if (suspended) return;
    std::string d = S.get("deepSleep", tl("1 phút"));
    if (d == tl("Không") || music::playing() || radioActive()) return;
    Uint32 after = d == tl("Ngay") ? 0 : (Uint32)atoi(d.c_str()) * 60000u;
    if (SDL_GetTicks() - entered >= after) { suspended = true; sys::suspend(); }
  }
  void onEnter() override { sys::setBrightness(1); entered = SDL_GetTicks(); }
  void onExit() override { sys::setBrightness(S.geti("bri", 8)); }
  void draw(const SDL_Rect&) override {
    G.rect(0, 0, W, H, {0, 0, 0});
    if (!S.getb("sleepClock", true)) return;
    int mi = (int)(time(nullptr) / 60), dx = (mi * 37) % 60 - 30, dy = (mi * 53) % 50 - 25;
    std::string ap; std::string c = clockStr(true, &ap);
    int w = G.text(c, W / 2 + dx, 140 + dy, 110, {216, 221, 230}, true, 1);
    if (!ap.empty()) G.text(ap, W / 2 + dx + w / 2 + 6, 220 + dy, 26, {216, 221, 230}, true);
    struct tm t = nowTm();
    Lunar l = toLunar(t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    std::string d = dateStr() + (S.getb("showLunar", true) ? " · " + std::to_string(l.d) + "/" + std::to_string(l.m) + tl(" ÂL") : "");
    G.text(d, W / 2 + dx, 280 + dy, 18, {138, 143, 152}, false, 1);
    if (S.getb("wxSleep", true)) {
      std::string wx = weatherLine(true);
      if (!wx.empty()) G.text(wx, W / 2 + dx, 340 + dy, 16, {150, 156, 168}, false, 1);
    }
    std::string extra = std::to_string(sys::battery()) + "%";
    if (music::playing() && music::current()) extra += "  ♪ " + music::current()->title;
    G.text(extra, W / 2 + dx, 312 + dy, 16, {120, 125, 135}, false, 1);
  }
  void key(Key) override { pop(); }
};
Screen* SleepScreen() { return new SleepScr; }

// auto-sleep after the idle time chosen in Settings (big clock on black, backlight dimmed)
extern bool g_booted;
void checkIdle() {
  if (!g_booted) return;
  std::string s = S.get("suspend", tl("15 phút"));
  int mins = s == tl("Tắt") ? 0 : atoi(s.c_str());
  if (!mins || !top() || top()->full || top()->noIdle) return;
  if (SDL_GetTicks() - lastInput > (Uint32)mins * 60000u) { lastInput = SDL_GetTicks(); push(SleepScreen()); }
}

// ================= home / standby =================
static std::string wallPath() {
  std::string w = S.get("wallpaper");
  return w;
}
bool g_booted = false;  // reminders / auto-sleep start only after the boot screen
struct Home : Screen {
  int sel = 0;
  Home() { noTitle = true; live = true; idleFps = 2; g_booted = true; }
  struct Ic { std::string ic, label, val; int bar; std::function<void(int)> adj; std::function<void()> ok; };
  std::vector<Ic> icons() {
    std::vector<Ic> v;
    int vol = S.geti("vol", 6), bri = S.geti("bri", 8);
    v.push_back({vol ? "volume" : "mute", tl("Âm lượng"), std::to_string(vol) + "/10", vol, [](int d) { S.seti("vol", std::max(0, std::min(10, S.geti("vol", 6) + d))); },
                 [] { if (S.geti("vol", 6)) { S.seti("lastVol", S.geti("vol", 6)); S.seti("vol", 0); toast(tl("Tắt tiếng"), "mute"); } else { S.seti("vol", S.geti("lastVol", 5)); toast(tl("Bật tiếng"), "volume"); } }});
    v.push_back({"sun", tl("Độ sáng"), std::to_string(bri) + "/10", bri, [](int d) { S.seti("bri", std::max(1, std::min(10, S.geti("bri", 8) + d))); }, [] { push(DisplayPage()); }});
    auto* tr = music::current();
    v.push_back({music::playing() ? "pause" : "play", tl("Nhạc"), tr ? tr->title : tl("Chưa phát"), -1, [](int d) { if (!music::active()) music::toggle(); else if (d > 0) music::next(); else music::prev(); },
                 [] { extern Screen* PlayerScreen(); push(PlayerScreen()); }});
    std::string ssid = sys::wifiSsid();
    v.push_back({S.getb("wifi", true) ? "wifi" : "wifioff", "Wi-Fi", !S.getb("wifi", true) ? tl("Tắt") : ssid.empty() ? tl("Chưa kết nối") : ssid, -1,
                 [](int) { S.setb("wifi", !S.getb("wifi", true)); }, [] { S.setb("wifi", !S.getb("wifi", true)); }});
    auto rec = getList("recent");
    v.push_back({"game", tl("Chơi tiếp"), rec.empty() ? tl("Chưa có") : romTitle(split(rec[0], '|').back()), -1, nullptr, [rec] {
                   if (rec.empty()) { toast(tl("Chưa chơi trò nào"), "info"); return; }
                   auto p = split(rec[0], '|');
                   for (auto& s : systems()) if (s.name == p[0]) { addRecent(rec[0]); launchGame(s, p[1]); return; }
                 }});
    return v;
  }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    std::string wp = wallPath();
    SDL_Texture* tex = wp.empty() ? nullptr : G.image(wp);
    if (tex) { SDL_Rect d = a; SDL_RenderCopy(G.ren, tex, nullptr, &d); }
    else G.grad(a.x, a.y, a.w, a.h, hex(S.get("wallTop", "1e3c72").c_str()), hex(S.get("wallBot", "6dd5ed").c_str()));
    G.grad(a.x, a.y + a.h / 2, a.w, a.h / 2, {0, 0, 0, 0}, {0, 0, 0, 110});
    Col wh{255, 255, 255};
    auto shadowText = [&](const std::string& s, int x, int y, int sz, bool b, int maxw = 0) {
      G.text(s, x + 1, y + 2, sz, {0, 0, 0, 120}, b, 1, maxw); G.text(s, x, y, sz, wh, b, 1, maxw); };
    int titleW = W - 40;
    std::string t1 = homeTitle(), t2 = homeSub();
    if (!t1.empty()) shadowText(t1, W / 2, a.y + 8, 20, true, titleW);
    if (!t2.empty()) G.text(t2, W / 2, a.y + 36, 13, {255, 255, 255, 215}, false, 1, W - 40);
    std::string ap; std::string c = clockStr(true, &ap);
    shadowText(c, W / 2, a.y + 52, 84, true);
    if (!ap.empty()) G.text(ap, W / 2 + G.textW(c, 84, true) / 2 + 6, a.y + 108, 24, wh, true);
    shadowText(dateStr(), W / 2, a.y + 148, 19, false);
    struct tm tm = nowTm();
    if (S.getb("showLunar", true)) {
      Lunar l = toLunar(tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
      std::string h = lunarHoliday(l);
      if (h.empty() && l.d == 15) h = tl("Rằm"); else if (h.empty() && l.d == 1) h = tl("Mùng 1");
      G.text(std::to_string(l.d) + "/" + std::to_string(l.m) + (l.leap ? tl(" nhuận") : "") + tl(" ÂL · ") + lunarYearName(l.y) + (h.empty() ? "" : " · " + h), W / 2, a.y + 174, 14, {255, 255, 255, 230}, false, 1);
    }
    // weather: a centred chip under the date / lunar-date lines (it used to sit at the top right)
    int wxShown = 0;
    if (S.getb("wxHome", true)) {
      std::string wx = weatherLine(true);
      if (!wx.empty()) {
        int ww = std::min(W - 60, G.textW(wx, 15, true) + 30);
        int wy = a.y + (S.getb("showLunar", true) ? 196 : 172);
        G.round(W / 2 - ww / 2, wy, ww, 28, {0, 0, 0, 90}, 14);
        G.text(wx, W / 2, wy + 5, 15, wh, true, 1, ww - 20);
        wxShown = 1;
      }
    }
    if (auto* tr = music::current()) {
      int x = 110, y = a.y + (wxShown ? 232 : 200), w = W - 220;
      G.round(x, y, w, 60, {0, 0, 0, 110}, 12);
      G.icon(music::playing() ? "music" : "pause", x + 14, y + 17, 26, wh);
      G.text(tr->title, x + 52, y + 7, 17, wh, true, 0, w - 120);
      G.text(tr->artist, x + 52, y + 30, 13, {255, 255, 255, 215}, false, 0, w - 120);
      double L = music::length();
      if (L > 0) { G.round(x + 52, y + 49, w - 120, 5, {255, 255, 255, 70}, 2); G.round(x + 52, y + 49, (int)((w - 120) * std::min(1.0, music::pos() / L)), 5, wh, 2); }
      G.text(fmtTime((int)music::pos()), x + w - 14, y + 22, 13, wh, false, 2);
    }
    auto ic = icons();
    int n = (int)ic.size(), sw = 60, gap = 14, x0 = W / 2 - (n * sw + (n - 1) * gap) / 2, y = a.y + a.h - 56 - 60;
    for (int i = 0; i < n; i++) {
      int x = x0 + i * (sw + gap);
      if (i == sel) { G.round(x - 5, y - 5, sw + 10, sw + 10, wh, 18); G.grad(x - 3, y - 3, sw + 6, sw + 6, t.hl1, t.hl2, 16); G.icon(ic[i].ic, x + 13, y + 13, 34, wh); }
      else { G.round(x, y, sw, sw, {0, 0, 0, 100}, 16); G.icon(ic[i].ic, x + 15, y + 15, 30, wh); }
    }
    int ly = a.y + a.h - 46;
    G.round(40, ly, W - 80, 32, {0, 0, 0, 105}, 16);
    const Ic& s = ic[sel];
    int lw = G.textW(s.label, 16, true);
    int bw = s.bar >= 0 ? 10 * 9 : G.textW(s.val, 16);
    int hint = 70, total = lw + 12 + bw + 12 + hint, x = W / 2 - total / 2;
    G.text(s.label, x, ly + 5, 16, wh, true); x += lw + 12;
    if (s.bar >= 0) { for (int k = 0; k < 10; k++) G.round(x + k * 9, ly + 9, 7, 14, {255, 255, 255, Uint8(k < s.bar ? 255 : 70)}, 2); x += bw + 12; }
    else { G.text(s.val, x, ly + 5, 16, wh, false, 0, 220); x += bw + 12; }
    G.text(s.adj ? tl("▲▼ chỉnh") : tl("OK mở"), x, ly + 8, 12, {255, 255, 255, 180});
  }
  Soft soft() override { return {tl("Menu"), tl("Chọn"), tl("Phím tắt")}; }
  void key(Key k) override {
    auto ic = icons();
    int n = (int)ic.size();
    if (k == Key::Left) sel = (sel - 1 + n) % n;
    else if (k == Key::Right) sel = (sel + 1) % n;
    else if ((k == Key::Up || k == Key::Down) && ic[sel].adj) ic[sel].adj(k == Key::Up ? 1 : -1);
    else if (k == Key::OK && ic[sel].ok) ic[sel].ok();
    else if (k == Key::A) push(MainMenu());
    else if (k == Key::B) {
      push(new ListScreen(tl("Phím tắt"), [] {
        Items v;
        auto add = [&](const char* l, const char* ic, const char* col, std::function<void()> f) { Item it; it.label = l; it.icon = ic; it.color = hex(col); it.ok = f; v.push_back(it); };
        add(tl("Đang phát"), "music", "e91e63", [] { extern Screen* PlayerScreen(); push(PlayerScreen()); });
        add(tl("Trò chơi"), "game", "e53935", [] { extern Screen* SystemsScreen(); push(SystemsScreen()); });
        add(tl("Lịch"), "calendar", "c62828", [] { push(CalendarApp()); });
        add(tl("Cài đặt"), "settings", "546e7a", [] { push(SettingsScreen()); });
        add(tl("Ngủ"), "moon", "3949ab", [] { push(SleepScreen()); });
        return v;
      }, "bolt"));
    }
  }
};
Screen* HomeScreen() { return new Home; }

// ================= games =================
Screen* GamesScreen(int si);
Screen* GameBrowser(int si);
Screen* SystemsScreen() {
  auto* l = new ListScreen(tl("Trò chơi"), [] {
    Items v;
    auto& ss = systems();
    auto hidden = getList("hidden");
    for (int i = 0; i < (int)ss.size(); i++) {
      if (std::find(hidden.begin(), hidden.end(), ss[i].name) != hidden.end()) continue;
      Item it; it.label = ss[i].fullname.empty() ? ss[i].name : ss[i].fullname;
      it.sub = std::to_string(ss[i].roms.size()) + tl(" trò chơi");
      it.icon = "game"; it.color = sysColor(ss[i].name);
      it.ok = [i] { push(GameBrowser(i)); };  // names, cover art and short descriptions (games_meta.cpp)
      v.push_back(it);
    }
    if (S.get("sortSys", tl("Mặc định")) != tl("Mặc định")) std::sort(v.begin(), v.end(), [](const Item& a, const Item& b) { return lower(a.label) < lower(b.label); });
    return v;
  }, "game");
  l->empty = tl("Chưa có trò chơi nào.\nChép ROM vào thẻ EASYROMS theo thư mục hệ máy.");
  l->opts = [](const Item& it, int) { return std::vector<Opt>{{tl("Mở"), "folder", it.ok}, {tl("Quét lại thẻ nhớ"), "restart", [] { systems(true); toast(tl("Đã quét lại"), "check"); }}}; };
  return l;
}
// ---------- per-game J2ME settings ----------
// <J2ME_DIR>/cfg/<game>.cfg, read by the C++ runner (j2me.cpp); overrides the automatic profile.
static std::string j2meConfPath(const std::string& rom) {
  const char* d = getenv("S40_J2ME_DIR");
  return std::string(d ? d : "/opt/j2me") + "/cfg/" + rom.substr(0, rom.find_last_of('.')) + ".cfg";
}
static std::map<std::string, std::string> readConf(const std::string& p) {
  std::map<std::string, std::string> m;
  for (auto& l : split(sh("cat '" + p + "'"), '\n')) { auto e = l.find('='); if (e != std::string::npos && l[0] != '#') m[trim(l.substr(0, e))] = trim(l.substr(e + 1)); }
  return m;
}
static void writeConf(const std::string& p, const std::map<std::string, std::string>& m) {
  std::string dir = p.substr(0, p.find_last_of('/'));
  run("mkdir -p '" + dir + "'");
  FILE* f = fopen(p.c_str(), "w");
  if (!f) { toast(tl("Không ghi được cấu hình"), "warn"); return; }
  for (auto& kv : m) fprintf(f, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
  fclose(f);
}
Screen* J2meConfig(const std::string& rom) {
  struct Field { const char* key; const char* label; const char* icon; const char* col; std::vector<std::string> vals, labels; };
  static const std::vector<Field> F = {
      {"core", tl("Lõi giả lập"), "chip", "e65100", {"jlr", "plus"}, {"J2ME Loader (3D GPU)", "FreeJ2ME-Plus (3D CPU)"}},
      {"res", tl("Độ phân giải"), "photo", "1e88e5", {"auto", "240x320", "320x240", "176x208", "176x220", "128x160", "208x208"}, {tl("Tự động"), "240×320", "320×240", "176×208", "176×220", "128×160", "208×208"}},
      {"fps", tl("Giới hạn khung hình"), "timer", "00897b", {"30", "60", "20", "native"}, {"30", "60", "20", tl("Không giới hạn")}},
      {"rotate", tl("Xoay màn hình"), "restart", "6d4c41", {"0", "90", "180", "270"}, {"0°", "90°", "180°", "270°"}},
      {"scale", tl("Co giãn"), "photo", "546e7a", {"fit", "integer"}, {tl("Vừa màn hình"), tl("Số nguyên (nét)")}},
      {"filter", tl("Lọc ảnh"), "sun", "f9a825", {"sharp", "linear"}, {tl("Sắc nét"), tl("Mịn")}},
      {"fast3d", tl("3D nhanh (lõi Plus)"), "bolt", "c62828", {"off", "on"}, {tl("Tắt"), tl("Bật")}},
  };
  std::string path = j2meConfPath(rom);
  auto* l = new ListScreen(tl("Cấu hình Java"), [path] {
    auto m = readConf(path);
    Items v;
    for (auto& f : F) {
      Item it; it.label = f.label; it.icon = f.icon; it.color = hex(f.col); it.okLabel = tl("Chọn"); it.sub = tl("← → để thay đổi");
      std::string cur = m.count(f.key) ? m[f.key] : "";
      int i = (int)(std::find(f.vals.begin(), f.vals.end(), cur) - f.vals.begin());
      it.right = i < (int)f.vals.size() ? f.labels[i] : tl("Mặc định");
      const Field* fp = &f;
      it.adj = [path, fp](int d) {
        auto m2 = readConf(path);
        int n = (int)fp->vals.size(), k = (int)(std::find(fp->vals.begin(), fp->vals.end(), m2[fp->key]) - fp->vals.begin());
        k = k >= n ? (d > 0 ? 0 : n - 1) : (k + d + n) % n;
        m2[fp->key] = fp->vals[k]; writeConf(path, m2);
      };
      it.ok = [path, fp] {
        auto m2 = readConf(path);
        int k = (int)(std::find(fp->vals.begin(), fp->vals.end(), m2[fp->key]) - fp->vals.begin());
        openPicker(fp->label, fp->labels, k < (int)fp->vals.size() ? k : -1, [path, fp](int c) { auto m3 = readConf(path); m3[fp->key] = fp->vals[c]; writeConf(path, m3); });
      };
      v.push_back(it);
    }
    return v;
  }, "chip");
  l->opts = [path](const Item&, int) { return std::vector<Opt>{{tl("Về mặc định (tự nhận theo game)"), "restart", [path] { run("rm -f '" + path + "'"); toast(tl("Đã về cấu hình tự động"), "check"); }}}; };
  return l;
}

Screen* GamesScreen(int si) {
  auto* l = new ListScreen(systems()[si].fullname, [si] {
    Items v;
    auto& s = systems()[si];
    std::vector<int> idx(s.roms.size());
    for (int i = 0; i < (int)idx.size(); i++) idx[i] = i;
    if (S.getb("favFirst")) std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return isFav(s.name + "|" + s.roms[a]) > isFav(s.name + "|" + s.roms[b]); });
    for (int i : idx) {
      Item it; it.label = romTitle(s.roms[i]); it.sub = s.roms[i];
      it.icon = "game"; it.color = sysColor(s.name);
      std::string key = s.name + "|" + s.roms[i];
      it.fav = isFav(key); it.okLabel = tl("Chơi");
      it.ok = [si, i, key] { addRecent(key); launchGame(systems()[si], systems()[si].roms[i]); };
      it.right = key;  // stash key; hidden by drawing? keep short
      it.right.clear();
      v.push_back(it);
    }
    return v;
  }, "game");
  l->opts = [si](const Item& it, int) {
    auto& s = systems()[si];
    std::string rom = it.sub, key = s.name + "|" + rom;
    std::vector<Opt> o{{tl("Chơi"), "play", it.ok}, {favLabel(key), "star", [key] { toggleFav(key); }, hex("f5a300")}};
    if (s.name == "j2me") o.push_back({tl("Cấu hình Java (lõi, độ phân giải…)"), "chip", [rom] { push(J2meConfig(rom)); }, hex("e65100")});
    o.push_back({tl("Thông tin"), "info", [s, rom] { infoBox(romTitle(rom), {{tl("Hệ máy"), s.fullname}, {tl("Tệp"), rom}, {tl("Trình giả lập"), s.name == "j2me" ? "J2ME Loader / FreeJ2ME-Plus" : s.core.empty() ? s.emulator : s.core}}); }});
    return o;
  };
  return l;
}
Screen* RecentScreen() {
  auto* l = new ListScreen(tl("Gần đây"), [] {
    Items v;
    for (auto& k : getList("recent")) {
      auto p = split(k, '|');
      if (p.size() < 2) continue;
      Item it; it.label = romTitle(p[1]); it.sub = p[0]; it.icon = "game"; it.color = sysColor(p[0]); it.okLabel = tl("Chơi"); it.fav = isFav(k);
      it.ok = [k, p] { for (auto& s : systems()) if (s.name == p[0]) { addRecent(k); launchGame(s, p[1]); return; } toast(tl("Không tìm thấy tệp"), "warn"); };
      v.push_back(it);
    }
    return v;
  }, "recent");
  l->empty = tl("Chưa chơi trò nào.");
  l->opts = [](const Item& it, int) { return std::vector<Opt>{{tl("Chơi"), "play", it.ok}, {tl("Xóa lịch sử"), "trash", [] { confirmBox(tl("Xóa toàn bộ lịch sử chơi?"), [] { S.set("recent", ""); }, "trash"); }, hex("c62828")}}; };
  return l;
}
Screen* FavScreen() {
  auto* l = new ListScreen(tl("Yêu thích"), [] {
    Items v;
    for (auto& k : getList("fav")) {
      auto p = split(k, '|');
      if (p.size() < 2) continue;
      Item it; it.label = romTitle(p[1]); it.sub = p[0]; it.icon = "game"; it.color = sysColor(p[0]); it.okLabel = tl("Chơi");
      it.ok = [k, p] { for (auto& s : systems()) if (s.name == p[0]) { addRecent(k); launchGame(s, p[1]); return; } };
      it.right = ""; v.push_back(it);
      v.back().sub = p[0];
    }
    return v;
  }, "star");
  l->empty = tl("Chưa có mục yêu thích.\nNhấn Tùy chọn trên một trò chơi để thêm.");
  l->opts = [](const Item& it, int i) {
    auto favs = getList("fav");
    std::string k = i >= 0 && i < (int)favs.size() ? favs[i] : "";
    if (k.empty()) return std::vector<Opt>{};
    return std::vector<Opt>{{tl("Mở"), "check", it.ok}, {tl("Bỏ khỏi Yêu thích"), "star", [k] { toggleFav(k); }, hex("f5a300")}};
  };
  return l;
}

// ================= music =================
struct Player : Screen {
  Player() { live = true; idleFps = 15; ic = "music"; }
  std::string title() override { return tl("Đang phát"); }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    auto* tr = music::current();
    if (!tr) { G.text(tl("Chưa có bài hát nào đang phát."), a.x + a.w / 2, a.y + 150, 18, t.sub, false, 1); G.text(tl("Nhấn OK để phát tất cả."), a.x + a.w / 2, a.y + 178, 18, t.sub, false, 1); return; }
    unsigned h = 0; for (char c : tr->title) h = h * 31 + (unsigned char)c;
    Col c1 = SYS_COLORS[h % 10];
    G.grad(a.x + 28, a.y + 18, 184, 184, lighten(c1, 50), c1, 14);
    G.icon("music", a.x + 78, a.y + 40, 84, {255, 255, 255, 230});
    double now = SDL_GetTicks() / 1000.0;
    for (int k = 0; k < 7; k++) {
      int bh = music::playing() ? 8 + (int)(fabs(sin(now * (3 + k * .7) + k * 1.3)) * 44) : 6;
      G.rect(a.x + 28 + 50 + k * 13, a.y + 18 + 184 - 26 - bh, 8, bh, {255, 255, 255, 215});
    }
    int x = a.x + 236;
    G.text(tr->title, x, a.y + 24, 26, t.txt, true, 0, a.w - 260);
    G.text(tr->artist, x, a.y + 62, 18, t.sub, false, 0, a.w - 260);
    G.text(std::string(music::playing() ? tl("Đang phát") : tl("Tạm dừng")), x, a.y + 100, 15, t.sub);
    auto chip = [&](int cx, const std::string& ic2, const std::string& lab, bool on) {
      int w = G.textW(lab, 13) + 40;
      G.round(cx, a.y + 130, w, 26, on ? t.a2 : Col{0, 0, 0, 22}, 13);
      G.icon(ic2, cx + 9, a.y + 135, 16, on ? Col{255, 255, 255} : t.sub);
      G.text(lab, cx + 30, a.y + 133, 13, on ? Col{255, 255, 255} : t.sub);
      return w + 10;
    };
    int cx = x;
    cx += chip(cx, "shuffle", tl("Ngẫu nhiên"), S.getb("shuffle"));
    static const char* REP[] = {tl("Tắt"), tl("Tất cả"), tl("Một bài")};
    chip(cx, "repeat", std::string(tl("Lặp: ")) + REP[S.geti("repeat", 1)], S.geti("repeat", 1) != 0);
    double L = music::length(), p = music::pos();
    int px = a.x + 28, pw = a.w - 56, py = a.y + 222;
    G.round(px, py, pw, 8, {0, 0, 0, 40}, 4);
    if (L > 0) G.grad(px, py, (int)(pw * std::min(1.0, p / L)), 8, t.hl1, t.hl2, 4);
    G.text(fmtTime((int)p), px, py + 12, 14, t.sub);
    if (L > 0) G.text("-" + fmtTime((int)(L - p)), px + pw, py + 12, 14, t.sub, false, 2);
    int cy = a.y + 268, mx = a.x + a.w / 2;
    G.round(mx - 35 - 82, cy + 9, 52, 52, {0, 0, 0, 18}, 26); G.icon("prev", mx - 35 - 82 + 13, cy + 22, 26, t.txt);
    G.grad(mx - 35, cy, 70, 70, t.hl1, t.hl2, 35); G.icon(music::playing() ? "pause" : "play", mx - 16, cy + 19, 32, {255, 255, 255});
    G.round(mx + 35 + 30, cy + 9, 52, 52, {0, 0, 0, 18}, 26); G.icon("next", mx + 35 + 43, cy + 22, 26, t.txt);
    int vol = S.geti("vol", 6);
    G.icon(vol ? "volume" : "mute", a.x + a.w - 130, cy + 26, 18, t.sub);
    for (int k = 0; k < 10; k++) G.round(a.x + a.w - 108 + k * 8, cy + 28, 6, 14, {t.sub.r, t.sub.g, t.sub.b, Uint8(k < vol ? 255 : 70)}, 2);
  }
  Soft soft() override { return {tl("Tùy chọn"), music::playing() ? tl("Tạm dừng") : tl("Phát"), tl("Quay lại")}; }
  void key(Key k) override {
    if (k == Key::OK) music::toggle();
    else if (k == Key::Left) music::prev();
    else if (k == Key::Right) music::next();
    else if (k == Key::Up || k == Key::Down) { S.seti("vol", std::max(0, std::min(10, S.geti("vol", 6) + (k == Key::Up ? 1 : -1)))); toast(tl("Âm lượng ") + std::to_string(S.geti("vol", 6)) + "/10", "volume"); }
    else if (k == Key::A) {
      static const char* REP[] = {tl("Tắt"), tl("Tất cả"), tl("Một bài")};
      openOptions({{std::string(tl("Phát ngẫu nhiên: ")) + (S.getb("shuffle") ? tl("Bật") : tl("Tắt")), "shuffle", [] { S.setb("shuffle", !S.getb("shuffle")); }},
                   {std::string(tl("Lặp lại: ")) + REP[S.geti("repeat", 1)], "repeat", [] { S.seti("repeat", (S.geti("repeat", 1) + 1) % 3); }},
                   {music::active() ? tl("Dừng phát") : "", "pause", [] { music::stop(); }, hex("c62828")}});
    } else if (k == Key::B) pop();
  }
};
Screen* PlayerScreen() { return new Player; }
Screen* MusicScreen() {
  auto* l = new ListScreen(tl("Nhạc"), [] {
    Items v;
    auto& lib = music::library();
    for (int i = 0; i < (int)lib.size(); i++) {
      Item it; it.label = lib[i].title; it.sub = lib[i].artist; it.icon = "music"; it.color = SYS_COLORS[i % 10]; it.okLabel = tl("Phát");
      if (music::current() == &lib[i]) it.right = music::playing() ? tl("Đang phát") : tl("Tạm dừng");
      it.ok = [i] { std::vector<int> all; for (int j = 0; j < (int)music::library().size(); j++) all.push_back(j); music::playList(all, i); push(PlayerScreen()); };
      v.push_back(it);
    }
    return v;
  }, "music");
  l->empty = tl("Chưa có bài hát.\nChép tệp MP3/OGG/FLAC vào thư mục music trên thẻ EASYROMS.");
  l->opts = [](const Item& it, int) {
    return std::vector<Opt>{{tl("Phát"), "play", it.ok}, {tl("Đang phát"), "music", [] { push(PlayerScreen()); }},
                            {tl("Quét lại thư mục nhạc"), "restart", [] { music::library(true); toast(tl("Đã quét lại"), "check"); }}};
  };
  return l;
}

// ================= photos =================
static std::string romsRoot() { return romsBase(); }
// Wallpaper from a photo: cropped to fill the screen and scaled down to 640x480 once, saved next to
// the settings (a phone photo would otherwise be decoded at full size into a texture larger than
// the Mali's 4096 px limit and take tens of MB of RAM).
bool setWallpaper(const std::string& src) {
  SDL_Surface* in = IMG_Load(src.c_str());
  if (!in) { toast(tl("Không mở được ảnh này"), "warn"); return false; }
  SDL_Surface* rgb = SDL_ConvertSurfaceFormat(in, SDL_PIXELFORMAT_ARGB8888, 0);
  SDL_FreeSurface(in);
  if (!rgb) return false;
  double s = std::max(W / (double)rgb->w, H / (double)rgb->h);  // cover
  SDL_Rect crop{0, 0, std::min(rgb->w, (int)(W / s)), std::min(rgb->h, (int)(H / s))};
  crop.x = (rgb->w - crop.w) / 2; crop.y = (rgb->h - crop.h) / 2;
  SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
  SDL_Rect full{0, 0, W, H};
  int rc = out ? SDL_SoftStretchLinear(rgb, &crop, out, &full) : -1;
  SDL_FreeSurface(rgb);
  if (rc != 0) { if (out) SDL_FreeSurface(out); toast(tl("Không xử lý được ảnh"), "warn"); return false; }
  const char* home = getenv("HOME");
  std::string dir = isDevice() ? "/home/ark/.config/s40" : std::string(home ? home : "/tmp") + "/.config/s40";
  run("mkdir -p '" + dir + "'");
  // a new name each time: the image cache is keyed by path
  std::string old = S.get("wallpaper"), dst = dir + "/wallpaper-" + std::to_string(time(nullptr)) + ".png";
  bool ok = IMG_SavePNG(out, dst.c_str()) == 0;
  SDL_FreeSurface(out);
  if (!ok) { toast(tl("Không lưu được hình nền"), "warn"); return false; }
  if (old.find("/.config/s40/wallpaper-") != std::string::npos) remove(old.c_str());
  S.set("wallpaper", dst);
  toast(tl("Đã đặt làm hình nền"), "check");
  return true;
}
// ================= hidden items (videos / photos), optional password =================
// A hidden item is not listed until the user unlocks "mục ẩn" (asking for the password when one is set). The list of hidden paths
// is hidden.txt next to the settings; the password is stored only as a salted SHA-256.
static std::string sha256hex(const std::string& msg) {
  static const uint32_t K[64] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  std::string m = msg; uint64_t bits = (uint64_t)msg.size() * 8;
  m.push_back((char)0x80); while (m.size() % 64 != 56) m.push_back(0);
  for (int i = 7; i >= 0; i--) m.push_back((char)((bits >> (i * 8)) & 0xff));
  auto rr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = ((uint8_t)m[off + i * 4] << 24) | ((uint8_t)m[off + i * 4 + 1] << 16) | ((uint8_t)m[off + i * 4 + 2] << 8) | (uint8_t)m[off + i * 4 + 3];
    for (int i = 16; i < 64; i++) { uint32_t s0 = rr(w[i-15], 7) ^ rr(w[i-15], 18) ^ (w[i-15] >> 3), s1 = rr(w[i-2], 17) ^ rr(w[i-2], 19) ^ (w[i-2] >> 10); w[i] = w[i-16] + s0 + w[i-7] + s1; }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t S1 = rr(e, 6) ^ rr(e, 11) ^ rr(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K[i] + w[i];
      uint32_t S0 = rr(a, 2) ^ rr(a, 13) ^ rr(a, 22), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  char out[65]; for (int i = 0; i < 8; i++) snprintf(out + i * 8, 9, "%08x", h[i]);
  return out;
}
static std::string pinHash(const std::string& pin) {
  std::string salt = S.get("hideSalt");
  if (salt.empty()) { salt = sha256hex(std::to_string(SDL_GetTicks()) + std::to_string(time(nullptr)) + std::to_string((long)getpid())).substr(0, 16); S.set("hideSalt", salt); }
  std::string h = pin; for (int i = 0; i < 2000; i++) h = sha256hex(salt + h);
  return h;
}
static bool hasPin() { return !S.get("hidePin").empty(); }
static std::string hiddenPath() { return S.path.substr(0, S.path.find_last_of('/')) + "/hidden.txt"; }
static bool g_hiddenOpen = false;   // unlocked until "Khóa lại" or the launcher restarts
static int g_hideRev = 0;           // bumped on every change: grids reload their file list
static std::vector<std::string> hiddenList() {  // cached: read again only after a change
  static std::vector<std::string> cache; static int cacheRev = -1;
  if (cacheRev != g_hideRev) { cache.clear(); for (auto& l : split(sh("cat '" + hiddenPath() + "' 2>/dev/null"), '\n')) if (!trim(l).empty()) cache.push_back(l); cacheRev = g_hideRev; }
  return cache;
}
// EmulationStation also hides a file with <hidden>true</hidden> in the gamelist.xml of its folder (what the "Show-Hide Movie" tool
// toggles): S40 honours that too, so one switch hides the files in both front-ends.
static std::string shq(const std::string& x) { std::string o = "'"; for (char c : x) { if (c == '\'') o += "'\\''"; else o += c; } return o + "'"; }
static std::vector<std::string> esHiddenIn(const std::string& dir) {  // file names hidden by <hidden>true</hidden>, cached per change
  static std::map<std::string, std::vector<std::string>> cache; static int cacheRev = -1;
  if (cacheRev != g_hideRev) { cache.clear(); cacheRev = g_hideRev; }
  auto it = cache.find(dir); if (it != cache.end()) return it->second;
  std::vector<std::string> out;
  std::string x = sh("cat " + shq(dir + "/gamelist.xml") + " 2>/dev/null");
  size_t pos = 0;
  while ((pos = x.find("<game>", pos)) != std::string::npos) {
    size_t end = x.find("</game>", pos); if (end == std::string::npos) end = x.size();
    std::string blk = x.substr(pos, end - pos);
    size_t a = blk.find("<path>"), b = blk.find("</path>");
    if (a != std::string::npos && b != std::string::npos && blk.find("<hidden>true</hidden>") != std::string::npos) {
      std::string nm = blk.substr(a + 6, b - a - 6); if (nm.rfind("./", 0) == 0) nm = nm.substr(2);
      out.push_back(nm);
    }
    pos = end;
  }
  cache[dir] = out; return out;
}
static bool esHidden(const std::string& path) {
  size_t sl = path.find_last_of('/'); if (sl == std::string::npos) return false;
  std::string nm = path.substr(sl + 1);
  for (auto& n : esHiddenIn(path.substr(0, sl))) if (n == nm) return true;
  return false;
}
static bool isHidden(const std::string& path) { for (auto& l : hiddenList()) if (l == path) return true; return esHidden(path); }
static void setHidden(const std::string& path, bool hide) {
  std::vector<std::string> v = hiddenList(), o;
  for (auto& l : v) if (l != path) o.push_back(l);
  if (hide) o.push_back(path);
  std::string txt; for (auto& l : o) txt += l + "\n";
  FILE* f = fopen(hiddenPath().c_str(), "w"); if (f) { fputs(txt.c_str(), f); fclose(f); }
  if (!hide && esHidden(path)) {  // also clear EmulationStation's flag for this file
    size_t sl = path.find_last_of('/');
    run("S40GL=" + shq(path.substr(0, sl) + "/gamelist.xml") + " S40NAME=" + shq(path.substr(sl + 1)) +
        " python3 -c \"import os,re;g=os.environ['S40GL'];n=os.environ['S40NAME'];t=open(g,encoding='utf-8').read();"
        "t=re.sub(r'<game>(?:(?!</game>).)*?<path>\\./'+re.escape(n)+r'</path>(?:(?!</game>).)*</game>',lambda m:m.group(0).replace('<hidden>true</hidden>','<hidden>false</hidden>'),t,flags=re.S);"
        "open(g,'w',encoding='utf-8').write(t)\"");
  }
  g_hideRev++;
}
static void askPin(std::function<void()> ok) {
  if (!hasPin()) { ok(); return; }
  push(OSK(tl("Mật khẩu mục ẩn"), "", true, [ok](const std::string& p) { if (pinHash(p) == S.get("hidePin")) ok(); else toast(tl("Sai mật khẩu"), "warn"); }));
}
static void hiddenMenu() {
  std::vector<Opt> o;
  if (g_hiddenOpen) o.push_back({tl("Khóa lại mục ẩn"), "lock", [] { g_hiddenOpen = false; g_hideRev++; toast(tl("Đã khóa mục ẩn"), "lock"); }});
  else o.push_back({tl("Hiện mục ẩn"), "lock", [] { askPin([] { g_hiddenOpen = true; g_hideRev++; toast(tl("Đang hiện mục ẩn"), "check"); }); }});
  o.push_back({hasPin() ? tl("Đổi mật khẩu") : tl("Đặt mật khẩu"), "key", [] {
    auto setNew = [] { push(OSK(tl("Mật khẩu mới"), "", true, [](const std::string& p) {
      if (trim(p).empty()) return;
      S.set("hidePin", pinHash(p)); toast(tl("Đã đặt mật khẩu"), "check"); })); };
    if (hasPin()) askPin(setNew); else setNew(); }});
  if (hasPin()) o.push_back({tl("Xóa mật khẩu"), "trash", [] { askPin([] { S.set("hidePin", ""); toast(tl("Đã xóa mật khẩu"), "check"); }); }});
  openOptions(o, tl("Mục ẩn"));
}
// options for one file: hide / unhide + the hidden-items menu
static std::vector<Opt> hideOptions(const std::string& path) {
  std::vector<Opt> o;
  if (!path.empty()) {
    if (isHidden(path)) o.push_back({tl("Bỏ ẩn"), "check", [path] { setHidden(path, false); toast(tl("Đã bỏ ẩn"), "check"); }});
    else o.push_back({tl("Ẩn mục này"), "lock", [path] { setHidden(path, true); toast(tl("Đã ẩn (mở lại ở Mục ẩn)"), "lock"); }});
  }
  o.push_back({tl("Mục ẩn và mật khẩu…"), "key", hiddenMenu});
  return o;
}
static bool visible(const std::string& path) { return g_hiddenOpen || !isHidden(path); }

std::vector<std::string> photoFiles() {
  std::vector<std::string> out;
  for (auto dir : {romsRoot() + "/photos", romsRoot() + "/screenshots"})
    for (auto& f : listDir(dir)) {
      std::string e = lower(f.substr(f.find_last_of('.') + 1));
      if (e == "png" || e == "jpg" || e == "jpeg" || e == "bmp") out.push_back(dir + "/" + f);
    }
  return out;
}
struct Viewer : Screen {
  std::vector<std::string> files; int i; bool slide = false; double t = 0;
  Viewer(std::vector<std::string> f, int at) : files(std::move(f)), i(at) { ic = "photo"; live = true; idleFps = 2; }
  std::string title() override { auto p = files[i]; return p.substr(p.find_last_of('/') + 1); }
  std::string info() override { return std::to_string(i + 1) + "/" + std::to_string(files.size()); }
  void tick(double dt) override { if (slide && (t += dt) > 3) { t = 0; i = (i + 1) % (int)files.size(); } }
  void draw(const SDL_Rect& a) override {
    G.rect(a.x, a.y, a.w, a.h, {0, 0, 0});
    if (SDL_Texture* tx = G.image(files[i])) {
      int w, h; SDL_QueryTexture(tx, nullptr, nullptr, &w, &h);
      double s = std::min(a.w / (double)w, a.h / (double)h);
      SDL_Rect d{a.x + (a.w - (int)(w * s)) / 2, a.y + (a.h - (int)(h * s)) / 2, (int)(w * s), (int)(h * s)};
      SDL_RenderCopy(G.ren, tx, nullptr, &d);
    }
    G.text(slide ? tl("▶ Trình chiếu") : tl("◀ ▶ chuyển ảnh"), a.x + a.w - 14, a.y + a.h - 28, 14, {255, 255, 255, 200}, false, 2);
  }
  Soft soft() override { return {tl("Tùy chọn"), slide ? tl("Dừng") : tl("Trình chiếu"), tl("Quay lại")}; }
  void key(Key k) override {
    int n = (int)files.size();
    if (k == Key::Left) i = (i - 1 + n) % n;
    else if (k == Key::Right) i = (i + 1) % n;
    else if (k == Key::OK) { slide = !slide; t = 0; }
    else if (k == Key::A) { std::string f = files[i]; openOptions({{tl("Đặt làm hình nền"), "photo", [f] { setWallpaper(f); }}}); }
    else if (k == Key::B) pop();
  }
};
struct PhotoGrid : Screen {
  std::vector<std::string> files; int sel = 0, topRow = 0, rev = -1;
  void reload() { files.clear(); for (auto& f : photoFiles()) if (visible(f)) files.push_back(f); rev = g_hideRev; sel = std::min(sel, std::max(0, (int)files.size() - 1)); }
  PhotoGrid() { ic = "photo"; reload(); }
  std::string title() override { return tl("Ảnh"); }
  std::string info() override { return files.empty() ? "" : std::to_string(sel + 1) + "/" + std::to_string(files.size()); }
  void draw(const SDL_Rect& a) override {
    if (rev != g_hideRev) reload();
    const Theme& t = theme();
    if (files.empty()) { G.text(tl("Chưa có ảnh. Chép ảnh vào thư mục photos."), a.x + a.w / 2, a.y + 160, 17, t.sub, false, 1); return; }
    int cols = 4, rows = 3, cw = (a.w - 24) / cols, ch = a.h / rows, r = sel / cols;
    if (r < topRow) topRow = r;
    if (r >= topRow + rows) topRow = r - rows + 1;
    for (int i = topRow * cols; i < std::min((int)files.size(), (topRow + rows) * cols); i++) {
      int x = a.x + 8 + (i % cols) * cw, y = a.y + (i / cols - topRow) * ch + 4;
      if (i == sel) G.grad(x, y, cw - 6, ch - 6, t.hl1, t.hl2, 8);
      if (SDL_Texture* tx = G.image(files[i])) { SDL_Rect d{x + 4, y + 4, cw - 14, ch - 30}; SDL_RenderCopy(G.ren, tx, nullptr, &d); }
      std::string nm = files[i].substr(files[i].find_last_of('/') + 1);
      G.text(nm, x + 6, y + ch - 26, 12, i == sel ? Col{255, 255, 255} : t.txt, true, 0, cw - 16);
    }
  }
  Soft soft() override { return {tl("Tùy chọn"), files.empty() ? "" : tl("Xem"), tl("Quay lại")}; }
  void key(Key k) override {
    int n = (int)files.size();
    if (k == Key::B) { pop(); return; }
    if (k == Key::A) { openOptions(hideOptions(n ? files[sel] : std::string())); return; }
    if (!n) return;
    if (k == Key::Left) sel = (sel - 1 + n) % n;
    else if (k == Key::Right) sel = (sel + 1) % n;
    else if (k == Key::Up && sel >= 4) sel -= 4;
    else if (k == Key::Down) sel = std::min(n - 1, sel + 4);
    else if (k == Key::OK) push(new Viewer(files, sel));
  }
};
Screen* PhotosScreen() { return new PhotoGrid; }

// ================= video (local files via dArkOS media player) =================
static std::vector<std::string> g_videoPaths;
Screen* VideoScreen() {
  auto* l = new ListScreen(tl("Video"), [] {
    Items v; g_videoPaths.clear();
    auto isVideo = [](const std::string& f) {
      auto d = f.find_last_of('.'); if (d == std::string::npos) return false;
      std::string e = lower(f.substr(d + 1));
      return e == "mp4" || e == "mkv" || e == "avi" || e == "webm" || e == "mov" || e == "m4v" || e == "ts" || e == "flv" || e == "wmv";
    };
    auto add = [&](const std::string& dir, const std::string& f, const std::string& sub) {
      Item it; it.label = f.substr(0, f.find_last_of('.')); it.sub = sub; it.icon = "video"; it.color = hex("8e24aa"); it.okLabel = tl("Phát");
      std::string p = dir + "/" + f;
      if (!visible(p)) return;
      if (isHidden(p)) it.sub += tl(" · ẩn");
      g_videoPaths.push_back(p);
      // mpv + gamepad bridge when the YouTube player is installed (A pause, left/right seek, B back), else ffplay
      it.ok = [p] {
        if (fileExists("/opt/ytdl/ytplay.sh")) sys::launchExternal("/opt/ytdl/ytplay.sh '" + p + "'");
        else sys::launchExternal("ffplay -fs -autoexit -loglevel quiet '" + p + "'");
      };
      v.push_back(it);
    };
    // movies / videos (S40 layout) and mplayer (dArkOS's own video folder), one level of sub-folders; both spellings of the case
    std::vector<std::string> seen;
    for (auto name : {"movies", "Movies", "videos", "Videos", "mplayer"}) {
      std::string dir = romsRoot() + "/" + name;
      bool dup = false; for (auto& d : seen) if (lower(d) == lower(dir)) dup = true;
      if (dup || !fileExists(dir)) continue;
      seen.push_back(dir);
      for (auto& f : listDir(dir)) {
        if (isVideo(f)) { add(dir, f, lower(f.substr(f.find_last_of('.') + 1))); continue; }
        std::string sd = dir + "/" + f;
        for (auto& g : listDir(sd)) if (isVideo(g)) add(sd, g, f);
      }
    }
    return v;
  }, "video");
  l->opts = [](const Item&, int i) { return hideOptions(i >= 0 && i < (int)g_videoPaths.size() ? g_videoPaths[i] : std::string()); };
  l->empty = tl("Chưa có video.\nChép phim vào thư mục movies trên thẻ EASYROMS.");
  return l;
}

// ================= internet apps (external players) =================
static bool online() { if (!S.getb("wifi", true) || sys::wifiSsid().empty()) { toast(tl("Cần kết nối Wi-Fi"), "wifioff"); return false; } return true; }
struct Station { std::string name, url, group; };
static std::vector<Station> loadM3U(const std::string& path) {
  std::vector<Station> out;
  std::string name, group;
  for (auto& l0 : split(sh("cat '" + path + "'"), '\n')) {
    std::string l = trim(l0);
    if (l.rfind("#EXTINF", 0) == 0) {
      auto c = l.find_last_of(','); name = c == std::string::npos ? tl("Kênh") : trim(l.substr(c + 1));
      auto g = l.find("group-title=\""); group = g == std::string::npos ? "" : l.substr(g + 13, l.find('"', g + 13) - g - 13);
    } else if (!l.empty() && l[0] != '#') { out.push_back({name.empty() ? l : name, l, group}); name.clear(); }
  }
  return out;
}
static pid_t radioPid = 0;
static std::string radioName;
static bool radioActive() { return radioPid > 0; }
static void radioStop() { if (radioPid > 0) { kill(radioPid, SIGTERM); radioPid = 0; radioName.clear(); } }
static void radioPlay(const Station& s) {
  radioStop(); music::stop();
  pid_t p = fork();
  if (p == 0) { setsid(); execlp("ffplay", "ffplay", "-nodisp", "-loglevel", "quiet", s.url.c_str(), (char*)nullptr); _exit(127); }
  radioPid = p; radioName = s.name;
  toast(tl("Đang phát ") + s.name, "radio");
}
Screen* StreamList(const std::string& title, const std::string& file, const std::string& icon, bool audio, const std::string& onlyGroup = "", const std::string& query = "") {
  auto* l = new ListScreen(title, [file, icon, audio, onlyGroup, query] {
    Items v;
    // the user's list on the card (EASYROMS/<file>) wins; otherwise the tested list shipped with the launcher
    std::string path = romsRoot() + "/" + file;
    if (!fileExists(path)) path = G.assetDir + "/" + file;
    for (auto& s : loadM3U(path)) {
      if (!onlyGroup.empty() && s.group != onlyGroup) continue;
      if (!query.empty() && lower(s.name).find(query) == std::string::npos) continue;
      if (!query.empty() && v.size() >= 300) break;
      Item it; it.label = s.name; it.sub = (onlyGroup.empty() && query.empty()) ? std::string() : (query.empty() ? std::string() : s.group); it.icon = icon; it.color = hex(audio ? "6a1b9a" : "283593");
      it.okLabel = audio ? (radioName == s.name ? tl("Tắt") : tl("Nghe")) : tl("Xem");
      if (audio && radioName == s.name) it.right = tl("Đang phát");
      it.ok = [s, audio] {
        if (!online()) return;
        if (audio) { if (radioName == s.name) radioStop(); else radioPlay(s); }
        else sys::launchExternal("/opt/ytdl/ytplay.sh '" + s.url + "'");
      };
      v.push_back(it);
    }
    return v;
  }, icon);
  l->empty = tl("Chưa có danh sách kênh.\nChép tệp ") + file + tl(" (định dạng m3u) vào thẻ EASYROMS.");
  return l;
}
// Radio: first the countries (group-title), then the stations of the chosen country. The first group in the file (Việt Nam) stays on top.
Screen* GroupedStreams(const std::string& title, const std::string& file, const std::string& icon, bool audio = true) {
  auto* l = new ListScreen(title, [file, icon, audio] {
    Items v;
    std::string path = romsRoot() + "/" + file;
    if (!fileExists(path)) path = G.assetDir + "/" + file;
    std::vector<std::pair<std::string, int>> groups;
    for (auto& s : loadM3U(path)) {
      bool f = false;
      for (auto& g : groups) if (g.first == s.group) { g.second++; f = true; break; }
      if (!f) groups.push_back({s.group, 1});
    }
    {
      Item sr; sr.label = audio ? tl("Tìm đài") : tl("Tìm kênh"); sr.sub = tl("Gõ tên, tìm trong tất cả các nước"); sr.icon = "web"; sr.color = hex("00897b"); sr.okLabel = tl("Tìm");
      sr.ok = [file, icon, audio] { push(OSK(audio ? tl("Tìm đài") : tl("Tìm kênh"), "", false, [file, icon, audio](const std::string& q0) {
        std::string q = lower(trim(q0)); if (q.empty()) return;
        push(StreamList(tl("Kết quả: ") + trim(q0), file, icon, audio, "", q)); })); };
      v.push_back(sr);
    }
    for (auto& g : groups) {
      Item it; it.label = g.first.empty() ? tl("Khác") : g.first; it.sub = std::to_string(g.second) + (audio ? tl(" đài") : tl(" kênh")); it.icon = icon; it.color = hex(audio ? "6a1b9a" : "283593"); it.okLabel = tl("Mở");
      std::string grp = g.first, ttl = it.label;
      it.ok = [grp, ttl, file, icon, audio] { push(StreamList(ttl, file, icon, audio, grp)); };
      v.push_back(it);
    }
    return v;
  }, icon);
  l->empty = tl("Chưa có danh sách kênh.\nChép tệp ") + file + tl(" (định dạng m3u) vào thẻ EASYROMS.");
  return l;
}
// TV: the iptv-org playlists, split by category, language and country; inside each, one group per category/language/country
Screen* TvScreen() {
  auto* l = new ListScreen(tl("Xem TV"), [] {
    Items v;
    struct E { const char* name; const char* file; const char* sub; };
    for (E e : {E{"Quốc gia", "tv/quoc_gia.m3u", "Chọn nước, rồi chọn kênh"}, E{"Thể loại", "tv/the_loai.m3u", "Tin tức, thể thao, phim, nhạc..."}, E{"Ngôn ngữ", "tv/ngon_ngu.m3u", "Theo ngôn ngữ phát sóng"}}) {
      Item it; it.label = tl(e.name); it.sub = tl(e.sub); it.icon = "tv"; it.color = hex("283593"); it.okLabel = tl("Mở");
      std::string f = e.file, t = it.label;
      it.ok = [f, t] { push(GroupedStreams(t, f, "tv", false)); };
      v.push_back(it);
    }
    return v;
  }, "tv");
  return l;
}
static Screen* NotInstalled(const std::string& title, const std::string& icon, const std::string& msg) {
  auto* l = new ListScreen(title, [] { return Items{}; }, icon);
  l->empty = msg;
  return l;
}
// Native web browser (C++/SDL2, NetSurf libcss/libdom/hubbub + Duktape JS): /opt/s40web/s40web [url]
struct ExtLaunch : Screen {
  std::string ttl, cmd; bool net, started = false;
  ExtLaunch(std::string t, std::string c, std::string icon, bool needNet) : ttl(std::move(t)), cmd(std::move(c)), net(needNet) { ic = icon; live = true; }
  std::string title() override { return ttl; }
  void tick(double) override { if (!started) { started = true; if (!net || online()) sys::launchExternal(cmd); pop(); } }
  void draw(const SDL_Rect& a) override { G.text(tl("Đang mở ") + ttl + "…", a.x + a.w / 2, a.y + a.h / 2 - 12, 18, theme().sub, false, 1); }
  void key(Key) override {}
};
Screen* BrowserApp() {
  if (!fileExists("/opt/s40web/s40web")) return NotInstalled(tl("Trình duyệt"), "web", tl("Chưa cài trình duyệt."));
  return new ExtLaunch(tl("Trình duyệt"), "/opt/s40web/s40web", "web", false);
}
// Online video = YouTube in the WebKit browser: MSE playback through GStreamer, quality capped to
// H.264 480p/30 for the A35 (s40web Adblock.cpp), ads skipped, fullscreen with pad controls.
Screen* OnlineVideoApp() {
  if (ytAppInstalled()) return YouTubeApp();  // no web page: no ads, far lighter than the browser
  if (!fileExists("/opt/s40web/s40web")) return NotInstalled(tl("Video trực tuyến"), "video", tl("Chưa cài trình duyệt."));
  return new ExtLaunch(tl("Video trực tuyến"), "/opt/s40web/s40web --app https://m.youtube.com/", "video", true);
}
// Facebook and Telegram: their web versions in the browser's app mode (Facebook Lite / Telegram
// apps are Android-only). Messenger chats are inside m.facebook.com; Telegram Web A ~300 MB.
Screen* WebApp(const std::string& title, const std::string& icon, const std::string& url) {
  if (!fileExists("/opt/s40web/s40web")) return NotInstalled(title, icon, tl("Chưa cài trình duyệt."));
  return new ExtLaunch(title, "/opt/s40web/s40web --app " + url, icon, true);
}
// PortMaster (ports store/launcher, github.com/PortsMaster). dArkOS ships its self-contained installer
// in /roms/tools; it is run once if PortMaster is not installed yet, then PortMaster is opened.
static std::string portMasterScript() {
  std::string r = romsRoot();
  // dArkOS installs into /opt/system/Tools and moves PortMaster.sh next to the PortMaster folder
  for (std::string p : {std::string("/opt/system/Tools/PortMaster.sh"), std::string("/opt/system/Tools/PortMaster/PortMaster.sh"),
                        r + "/tools/PortMaster/PortMaster.sh", r + "/ports/PortMaster/PortMaster.sh", r + "/tools/PortMaster.sh",
                        r + "/ports/PortMaster.sh"})
    if (fileExists(p)) return p;
  return "";
}
Screen* PortMasterApp() {
  std::string pm = portMasterScript(), inst = romsRoot() + "/tools/Install.PortMaster.sh";
  if (pm.empty() && !fileExists(inst)) return NotInstalled("PortMaster", "box", tl("Chưa có PortMaster.\nChép Install.PortMaster.sh vào thư mục tools trên thẻ EASYROMS."));
  // the official installer ends with "systemctl restart emulationstation" unless ~/no_es_restart exists
  std::string cmd = pm.empty() ? "touch \"$HOME/no_es_restart\"; bash '" + inst + "' && for p in /opt/system/Tools/PortMaster.sh " +
                                     romsRoot() + "/tools/PortMaster/PortMaster.sh " + romsRoot() +
                                     "/ports/PortMaster/PortMaster.sh; do [ -f \"$p\" ] && exec bash \"$p\"; done"
                               : "bash '" + pm + "'";
  return new ExtLaunch("PortMaster", "sudo perfmax ondemand >/dev/null 2>&1; " + cmd + "; sudo perfnorm >/dev/null 2>&1", "box", false);
}
// ================= power =================
Screen* PowerScreen() {
  return new ListScreen(tl("Nguồn"), [] {
    Items v;
    auto add = [&](const char* l, const char* s, const char* ic, const char* c, std::function<void()> f) { Item it; it.label = l; it.sub = s; it.icon = ic; it.color = hex(c); it.ok = f; v.push_back(it); };
    add(tl("Ngủ"), tl("Tắt màn hình, đồng hồ trên nền đen"), "moon", "3949ab", [] { push(SleepScreen()); });
    add(tl("Ngủ sâu"), tl("Tạm dừng máy, tiết kiệm pin"), "moon", "283593", [] { sys::suspend(); });
    add(tl("Khởi động lại"), tl("Khởi động lại dArkOS"), "restart", "ef6c00", [] { confirmBox(tl("Khởi động lại máy?"), [] { sys::reboot(); }, "restart"); });
    add(tl("Tắt máy"), tl("Tắt hoàn toàn thiết bị"), "power", "c62828", [] { confirmBox(tl("Tắt máy ngay bây giờ?"), [] { sys::poweroff(); }, "power"); });
    if (fileExists("/usr/bin/emulationstation/emulationstation.sh")) {
      add(tl("Mở EmulationStation một lần"), tl("Thoát ES để quay lại giao diện này"), "game", "37474f", [] { confirmBox(tl("Mở EmulationStation?\nThoát ES để quay lại giao diện này."), [] { sys::launchExternal("/usr/bin/emulationstation/emulationstation.sh"); }, "game"); });
      if (fileExists("/usr/local/bin/switch-launcher.sh"))
        add(tl("Đặt EmulationStation làm giao diện mặc định"), tl("Các lần bật máy sau dùng EmulationStation"), "game", "5d4037", [] { confirmBox(tl("Đổi giao diện mặc định sang EmulationStation?\nĐổi lại bằng mục Tools trong ES."), [] { run("sudo /usr/local/bin/switch-launcher.sh es >/dev/null 2>&1 &"); }, "game"); });
    }
    return v;
  }, "power");
}

// ================= main menu (3x3 grid, scrolls) =================
struct MenuEntry { const char* label; const char* ic; const char* col; std::function<Screen*()> open; };
Screen* MainMenu();
static std::vector<MenuEntry> MENU() {
  std::vector<MenuEntry> v = {
      {tl("Trò chơi"), "game", "e53935", [] { return SystemsScreen(); }},
      {"RetroArch", "chip", "424242", [] { return new ExtLaunch("RetroArch", "sudo perfmax ondemand >/dev/null 2>&1; /usr/local/bin/retroarch --menu; sudo perfnorm >/dev/null 2>&1", "chip", false); }},
      {"RetroArch 32", "chip", "5d4037", [] { return new ExtLaunch("RetroArch 32", "sudo perfmax ondemand >/dev/null 2>&1; /usr/local/bin/retroarch32 --menu; sudo perfnorm >/dev/null 2>&1", "chip", false); }},
      {tl("Nhạc"), "music", "d81b60", [] { return MusicScreen(); }},
      {tl("Ảnh"), "photo", "43a047", [] { return PhotosScreen(); }},
      {tl("Video"), "video", "8e24aa", [] { return VideoScreen(); }},
      {tl("Trình duyệt"), "web", "0277bd", [] { return BrowserApp(); }},
      {tl("Video trực tuyến"), "video", "c62828", [] { return OnlineVideoApp(); }},
      {tl("Xem TV"), "tv", "283593", [] { return TvScreen(); }},
      {tl("Radio"), "radio", "6a1b9a", [] { return GroupedStreams(tl("Radio"), "radio/dai.m3u", "radio"); }},
      {"PortMaster", "box", "6d4c41", [] { return PortMasterApp(); }},
      {tl("Thời tiết"), "sun", "f9a825", [] { return WeatherApp(); }},
      {"m.facebook.com", "globe", "546e7a", [] { return WebApp("m.facebook.com", "globe", "https://m.facebook.com/"); }},
      {"web.telegram.org", "globe", "607d8b", [] { return WebApp("web.telegram.org", "globe", "https://web.telegram.org/a/"); }},
      {tl("Quản lý tệp"), "folder", "f9a825", [] { return FileManagerApp(); }},
      {tl("Terminal"), "term", "37474f", [] { return TerminalApp(); }},
      {tl("Yêu thích"), "star", "f5a300", [] { return FavScreen(); }},
      {tl("Gần đây"), "recent", "00897b", [] { return RecentScreen(); }},
      {tl("Ứng dụng"), "tools", "1e88e5", [] { return AppsScreen(); }},
      {tl("Cài đặt"), "settings", "546e7a", [] { return SettingsScreen(); }},
      {tl("Nguồn"), "power", "c62828", [] { return PowerScreen(); }},
  };
  // the 32-bit RetroArch is optional (dArkOS has it, lean builds may not)
  if (!fileExists("/opt/retroarch/bin/retroarch32") || !fileExists("/usr/local/bin/retroarch32"))
    v.erase(std::remove_if(v.begin(), v.end(), [](const MenuEntry& e) { return std::string(e.label) == "RetroArch 32"; }), v.end());
  return v;
}
struct Menu : Screen {
  static constexpr int COLS = 4, ROWS = 3;  // 4 x 3 icons visible, scrolls by row
  int sel = 0, row0 = 0;
  std::vector<MenuEntry> m = MENU();
  std::string title() override { return S.getb("menuList") ? tl("Menu") : m[sel].label; }
  std::string info() override { return std::to_string(sel + 1) + "/" + std::to_string(m.size()); }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    int N = (int)m.size();
    if (S.getb("menuList")) {
      Items v; for (auto& e : m) { Item it; it.label = e.label; it.icon = e.ic; it.color = hex(e.col); v.push_back(it); }
      drawItems(v, sel, row0, a.h / 45, 45, a, false);
      return;
    }
    const int C = COLS, R = ROWS;
    int r = sel / C;
    if (r < row0) row0 = r;
    if (r >= row0 + R) row0 = r - R + 1;
    int cw = (a.w - 16) / C, ch = (a.h - 16) / R;
    for (int i = row0 * C; i < std::min(N, (row0 + R) * C); i++) {
      int x = a.x + 8 + (i % C) * cw, y = a.y + 8 + (i / C - row0) * ch;
      bool s = i == sel;
      if (s) G.grad(x + 2, y + 2, cw - 4, ch - 4, t.hl1, t.hl2, 12);
      int is = s ? 64 : 58, ix = x + cw / 2 - is / 2, iy = y + 10 - (s ? 3 : 0);
      Col c = hex(m[i].col);
      G.grad(ix, iy, is, is, lighten(c), c, 16);
      G.icon(m[i].ic, ix + is / 2 - 18, iy + is / 2 - 18, 36, {255, 255, 255});
      G.text(m[i].label, x + cw / 2, y + ch - 28, 14, s ? Col{255, 255, 255} : t.txt, true, 1, cw - 6);
    }
    int TR = (N + C - 1) / C;
    if (TR > R) { int sh2 = a.h - 16; G.round(a.x + a.w - 9, a.y + 8, 5, sh2, {0, 0, 0, 30}, 3); G.round(a.x + a.w - 9, a.y + 8 + sh2 * row0 / TR, 5, sh2 * R / TR, t.a2, 3); }
  }
  Soft soft() override { return {tl("Tùy chọn"), tl("Chọn"), tl("Quay lại")}; }
  void key(Key k) override {
    const int C = COLS;
    int N = (int)m.size(), TR = (N + C - 1) / C, s = sel;
    if (S.getb("menuList")) { if (k == Key::Up) sel = (s - 1 + N) % N; else if (k == Key::Down) sel = (s + 1) % N; }
    else {
      if (k == Key::Left) sel = (s - 1 + N) % N;
      else if (k == Key::Right) sel = (s + 1) % N;
      else if (k == Key::Up) sel = s - C >= 0 ? s - C : std::min(N - 1, (TR - 1) * C + s % C);
      else if (k == Key::Down) sel = s + C < N ? s + C : (s / C < TR - 1 ? N - 1 : s % C);
    }
    if (k == Key::OK) push(m[sel].open());
    else if (k == Key::A) openOptions({{tl("Mở"), "check", [this] { push(m[sel].open()); }}, {S.getb("menuList") ? tl("Xem dạng lưới") : tl("Xem dạng danh sách"), "list", [] { S.setb("menuList", !S.getb("menuList")); }}});
    else if (k == Key::B) pop();
  }
};
Screen* MainMenu() { return new Menu; }

}  // namespace s40
