// Small built-in apps: calendar (with lunar), calculator, stopwatch, notes, flashlight, key test.
#include "s40.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <tuple>

namespace s40 {

// ================= calendar =================
static int daysIn(int m, int y) { static const int D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; return m == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0) ? 29 : D[m - 1]; }
static int weekday(int d, int m, int y) { struct tm t{}; t.tm_mday = d; t.tm_mon = m - 1; t.tm_year = y - 1900; t.tm_hour = 12; mktime(&t); return t.tm_wday; }
static std::string solarHoliday(int d, int m) {
  static const std::map<std::pair<int, int>, const char*> H = {{{1, 1}, tl("Tết Dương lịch")}, {{14, 2}, tl("Lễ Tình nhân")}, {{8, 3}, tl("Quốc tế Phụ nữ")}, {{30, 4}, tl("Giải phóng miền Nam")},
                                                                {{1, 5}, tl("Quốc tế Lao động")}, {{1, 6}, tl("Quốc tế Thiếu nhi")}, {{2, 9}, tl("Quốc khánh")}, {{20, 10}, tl("Phụ nữ Việt Nam")},
                                                                {{20, 11}, tl("Nhà giáo Việt Nam")}, {{24, 12}, tl("Đêm Giáng sinh")}, {{25, 12}, tl("Giáng sinh")}};
  auto it = H.find({d, m});
  return it == H.end() ? "" : it->second;
}
struct Calendar : Screen {
  int d, m, y;
  Calendar() { struct tm t = nowTm(); d = t.tm_mday; m = t.tm_mon + 1; y = t.tm_year + 1900; ic = "calendar"; }
  std::string title() override { return tl("Tháng ") + std::to_string(m) + "/" + std::to_string(y); }
  std::string info() override { return tl("Năm ") + lunarYearName(toLunar(d, m, y).y); }
  void move(int days) {
    struct tm t{}; t.tm_mday = d + days; t.tm_mon = m - 1; t.tm_year = y - 1900; t.tm_hour = 12; mktime(&t);
    d = t.tm_mday; m = t.tm_mon + 1; y = t.tm_year + 1900;
  }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    static const char* WD[] = {"T2", "T3", "T4", "T5", "T6", "T7", "CN"};
    int cw = (a.w - 40) / 7, x0 = a.x + 20, y0 = a.y + 4, chh = 41;
    for (int i = 0; i < 7; i++) G.text(WD[i], x0 + i * cw + cw / 2, y0, 14, t.sub, true, 1);
    int start = (weekday(1, m, y) + 6) % 7, n = daysIn(m, y);
    struct tm now = nowTm();
    bool lun = S.getb("showLunar", true);
    for (int dd = 1; dd <= n; dd++) {
      int idx = start + dd - 1, cx = x0 + (idx % 7) * cw, cy = y0 + 22 + (idx / 7) * (chh + 2);
      Lunar l = toLunar(dd, m, y);
      bool sel = dd == d, today = dd == now.tm_mday && m == now.tm_mon + 1 && y == now.tm_year + 1900;
      bool hol = !solarHoliday(dd, m).empty() || !lunarHoliday(l).empty();
      if (sel) G.grad(cx + 1, cy, cw - 2, chh, t.hl1, t.hl2, 7);
      else if (today) { G.round(cx + 1, cy, cw - 2, chh, t.a2, 7); G.round(cx + 3, cy + 2, cw - 6, chh - 4, t.bg1, 6); }
      Col fc = sel ? Col{255, 255, 255} : (hol || idx % 7 == 6) ? hex("d32f2f") : t.txt;
      G.text(std::to_string(dd), cx + cw / 2, cy + (lun ? 1 : 8), 16, fc, true, 1);
      if (lun) {
        std::string ls = l.d == 1 ? std::to_string(l.d) + "/" + std::to_string(l.m) : std::to_string(l.d);
        Col lc = sel ? Col{255, 255, 255} : (l.d == 1 || l.d == 15) ? hex("d32f2f") : t.sub;
        G.text(ls, cx + cw / 2, cy + 22, 11, lc, l.d == 1 || l.d == 15, 1);
      }
    }
    Lunar l = toLunar(d, m, y);
    std::string hs = solarHoliday(d, m), lh = lunarHoliday(l);
    if (lh.empty() && l.d == 15) lh = tl("Rằm"); else if (lh.empty() && l.d == 1) lh = tl("Mùng 1");
    if (!lh.empty()) hs += (hs.empty() ? "" : " · ") + lh;
    int iy = a.y + a.h - 50;
    G.text(std::string(DAYS[weekday(d, m, y)]) + ", " + dateNum(d, m, y) + (hs.empty() ? "" : " · " + hs), a.x + a.w / 2, iy, 15, t.txt, true, 1, a.w - 20);
    G.text(tl("Âm lịch: ") + std::to_string(l.d) + "/" + std::to_string(l.m) + (l.leap ? tl(" nhuận") : "") + tl(" · Ngày ") + lunarDayName(l.jd) + tl(" · Tháng ") + lunarMonthName(l.m, l.y),
           a.x + a.w / 2, iy + 24, 14, t.sub, false, 1, a.w - 20);
  }
  Soft soft() override { return {tl("Tùy chọn"), tl("Hôm nay"), tl("Quay lại")}; }
  void key(Key k) override {
    if (k == Key::Left) move(-1); else if (k == Key::Right) move(1);
    else if (k == Key::Up) move(-7); else if (k == Key::Down) move(7);
    else if (k == Key::OK) { struct tm t = nowTm(); d = t.tm_mday; m = t.tm_mon + 1; y = t.tm_year + 1900; }
    else if (k == Key::A) openOptions({{tl("Thêm nhắc việc ngày này"), "bell", [this] { void addReminder(int, int, int); addReminder(d, m, y); }, hex("ef6c00")},
                                       {tl("Tháng trước"), "prev", [this] { move(-daysIn(m == 1 ? 12 : m - 1, m == 1 ? y - 1 : y)); }},
                                       {tl("Tháng sau"), "next", [this] { move(daysIn(m, y)); }},
                                       {tl("Đến Tết Nguyên đán"), "star", [this] { for (int i = 1; i < 420; i++) { move(1); Lunar l = toLunar(d, m, y); if (l.d == 1 && l.m == 1 && !l.leap) break; } }}});
    else if (k == Key::B) pop();
  }
};
Screen* CalendarApp() { return new Calendar; }

// ================= calculator =================
struct Calc : Screen {
  const char* K[5][4] = {{"C", "⌫", "%", "÷"}, {"7", "8", "9", "×"}, {"4", "5", "6", "−"}, {"1", "2", "3", "+"}, {"±", "0", ",", "="}};
  int r = 1, c = 0; std::string cur = "0", op; double acc = 0; bool hasAcc = false, fresh = false;
  Calc() { ic = "calc"; }
  std::string title() override { return tl("Máy tính"); }
  static std::string show(double v) {
    if (!std::isfinite(v)) return tl("Lỗi");
    char b[32]; snprintf(b, sizeof b, "%.10g", v);
    std::string s = b; for (auto& ch : s) if (ch == '.') ch = ',';
    return s;
  }
  double num() { std::string s = cur; for (auto& ch : s) if (ch == ',') ch = '.'; return atof(s.c_str()); }
  static double ap(double a, const std::string& o, double b) { return o == "+" ? a + b : o == "−" ? a - b : o == "×" ? a * b : o == "÷" ? (b == 0 ? NAN : a / b) : b; }
  void press(const std::string& k) {
    if (k.size() == 1 && isdigit((unsigned char)k[0])) { cur = (fresh || cur == "0") ? k : cur + k; fresh = false; }
    else if (k == ",") { if (fresh) { cur = "0"; fresh = false; } if (cur.find(',') == std::string::npos) cur += ","; }
    else if (k == "C") { cur = "0"; hasAcc = false; op.clear(); }
    else if (k == "⌫") { cur = cur.size() > 1 ? cur.substr(0, cur.size() - 1) : "0"; }
    else if (k == "±") { cur = cur[0] == '-' ? cur.substr(1) : (cur == "0" ? cur : "-" + cur); }
    else if (k == "%") cur = show(num() / 100);
    else if (k == "=") { if (!op.empty()) { cur = show(ap(acc, op, num())); hasAcc = false; op.clear(); fresh = true; } }
    else { if (!op.empty() && !fresh) acc = ap(acc, op, num()); else if (!hasAcc) acc = num(); hasAcc = true; op = k; fresh = true; cur = show(acc); }
  }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    int x = a.x + 40, w = a.w - 80, y = a.y + 10;
    G.round(x, y, w, 70, hex("dfe8d0"), 10);
    if (hasAcc) G.text(show(acc) + " " + op, x + w - 14, y + 4, 14, hex("4a5a30"), false, 2);
    G.text(cur, x + w - 14, y + 24, 32, hex("1b2a10"), true, 2, w - 28);
    y += 78;
    int kw = (w - 18) / 4, kh = 44;
    for (int ri = 0; ri < 5; ri++)
      for (int ci = 0; ci < 4; ci++) {
        int kx = x + ci * (kw + 6), ky = y + ri * (kh + 6);
        bool s = ri == r && ci == c;
        if (s) G.grad(kx, ky, kw, kh, t.hl1, t.hl2, 8); else G.round(kx, ky, kw, kh, {0, 0, 0, 22}, 8);
        bool isOp = std::string("÷×−+=").find(K[ri][ci]) != std::string::npos;
        G.text(K[ri][ci], kx + kw / 2, ky + 9, 20, s ? Col{255, 255, 255} : isOp ? t.a2 : t.txt, true, 1);
      }
  }
  Soft soft() override { return {tl("Xóa hết"), tl("Nhấn"), tl("Quay lại")}; }
  void key(Key k) override {
    if (k == Key::Up) r = (r + 4) % 5; else if (k == Key::Down) r = (r + 1) % 5;
    else if (k == Key::Left) c = (c + 3) % 4; else if (k == Key::Right) c = (c + 1) % 4;
    else if (k == Key::OK) press(K[r][c]); else if (k == Key::A) press("C"); else if (k == Key::B) pop();
  }
};
Screen* CalcApp() { return new Calc; }

// ================= stopwatch =================
struct Stopwatch : Screen {
  bool runng = false; double el = 0; std::vector<double> laps;
  Stopwatch() { ic = "timer"; live = true; }
  std::string title() override { return tl("Đồng hồ bấm giờ"); }
  void tick(double dt) override { if (runng) el += dt; }
  static std::string f(double s) { char b[32]; int cs = (int)(s * 100) % 100, ss = (int)s % 60, mm = (int)s / 60; snprintf(b, sizeof b, "%02d:%02d.%02d", mm, ss, cs); return b; }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    G.text(runng ? tl("Đang chạy") : el > 0 ? tl("Đã dừng") : tl("Sẵn sàng"), a.x + a.w / 2, a.y + 24, 16, t.sub, false, 1);
    G.text(f(el), a.x + a.w / 2, a.y + 50, 72, t.txt, true, 1);
    int y = a.y + 160;
    for (int i = (int)laps.size() - 1; i >= 0 && y < a.y + a.h - 30; i--, y += 34) {
      G.text(tl("Vòng ") + std::to_string(i + 1), a.x + 160, y, 16, t.txt, true);
      G.text(f(laps[i]), a.x + a.w - 160, y, 16, t.sub, false, 2);
    }
  }
  Soft soft() override { return {runng ? tl("Vòng") : el > 0 ? tl("Đặt lại") : "", runng ? tl("Dừng") : el > 0 ? tl("Tiếp tục") : tl("Bắt đầu"), tl("Quay lại")}; }
  void key(Key k) override {
    if (k == Key::OK) runng = !runng;
    else if (k == Key::A) { if (runng) laps.push_back(el); else { el = 0; laps.clear(); } }
    else if (k == Key::B) pop();
  }
};
Screen* StopwatchApp() { return new Stopwatch; }

// ================= notes =================
static std::vector<std::string> notes() { std::vector<std::string> o; for (auto& s : split(S.get("notes"), '\x1f')) if (!s.empty()) o.push_back(s); return o; }
static void saveNotes(const std::vector<std::string>& v) { std::string s; for (auto& x : v) { if (!s.empty()) s += '\x1f'; s += x; } S.set("notes", s); }
Screen* NotesApp() {
  auto* l = new ListScreen(tl("Ghi chú"), [] {
    Items v; auto n = notes();
    for (int i = 0; i < (int)n.size(); i++) {
      Item it; it.label = n[i]; it.sub = tl("Ghi chú ") + std::to_string(i + 1); it.icon = "note"; it.color = hex("f5a300"); it.okLabel = tl("Sửa");
      it.ok = [i] { auto n2 = notes(); push(OSK(tl("Sửa ghi chú"), n2[i], false, [i](const std::string& t) { auto v2 = notes(); if (t.empty()) v2.erase(v2.begin() + i); else v2[i] = t; saveNotes(v2); })); };
      v.push_back(it);
    }
    return v;
  }, "note");
  l->empty = tl("Chưa có ghi chú.\nNhấn Tùy chọn › Thêm ghi chú.");
  l->opts = [](const Item&, int i) {
    std::vector<Opt> o{{tl("Thêm ghi chú"), "plus", [] { push(OSK(tl("Ghi chú mới"), "", false, [](const std::string& t) { if (!t.empty()) { auto v = notes(); v.push_back(t); saveNotes(v); } })); }}};
    if (i >= 0) o.push_back({tl("Xóa ghi chú này"), "trash", [i] { auto v = notes(); if (i < (int)v.size()) { v.erase(v.begin() + i); saveNotes(v); } }, hex("c62828")});
    return o;
  };
  return l;
}

// ================= flashlight =================
struct Flash : Screen {
  int c = 0; bool sos = false; double t = 0; int prevBri;
  Flash() { full = true; live = true; capture = true; prevBri = S.geti("bri", 8); }
  void onEnter() override { sys::setBrightness(10); }
  void onExit() override { sys::setBrightness(prevBri); }
  void tick(double dt) override { t += dt; }
  void draw(const SDL_Rect&) override {
    static const Col C[] = {{255, 255, 255}, {255, 244, 214}, {255, 42, 42}};
    static const int P[] = {1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0};
    bool on = !sos || P[(int)(t / 0.2) % 30];
    G.rect(0, 0, W, H, on ? C[c] : Col{0, 0, 0});
    G.text(tl("OK đổi màu · Tùy chọn: SOS · B tắt"), W / 2, H - 30, 14, {150, 150, 150}, false, 1);
  }
  void key(Key k) override { if (k == Key::OK) c = (c + 1) % 3; else if (k == Key::A) { sos = !sos; t = 0; } else if (k == Key::B) pop(); }
};
Screen* FlashlightApp() { return new Flash; }

// ================= date / time pickers =================
struct DateTimePicker : Screen {
  std::string ttl; bool isTime; int v[3]; int f = 0;
  std::function<void(int, int, int)> done;
  std::string title() override { return ttl; }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    G.text(isTime ? tl("← → chọn giờ/phút · ↑↓ chỉnh") : tl("← → chọn ngày/tháng/năm · ↑↓ chỉnh"), a.x + a.w / 2, a.y + 30, 15, t.sub, false, 1);
    char b[3][8];
    int n = isTime ? 2 : 3;
    for (int i = 0; i < n; i++) snprintf(b[i], 8, i == 2 ? "%d" : "%02d", v[i]);
    int sz = isTime ? 80 : 60, gap = 14;
    std::vector<int> w(n); int total = 0;
    for (int i = 0; i < n; i++) { w[i] = G.textW(b[i], sz, true) + 20; total += w[i] + (i ? gap + 20 : 0); }
    int x = a.x + a.w / 2 - total / 2, y = a.y + 90;
    for (int i = 0; i < n; i++) {
      if (i) { G.text(isTime ? ":" : "/", x + 10, y, sz, t.txt, true, 1); x += gap + 20; }
      if (i == f) G.grad(x, y - 4, w[i], sz + 26, t.hl1, t.hl2, 10);
      G.text(b[i], x + w[i] / 2, y, sz, i == f ? Col{255, 255, 255} : t.txt, true, 1);
      x += w[i];
    }
    if (!isTime) {
      Lunar l = toLunar(v[0], v[1], v[2]);
      G.text(tl("Âm lịch: ") + std::to_string(l.d) + "/" + std::to_string(l.m) + (l.leap ? tl(" nhuận") : "") + tl(" năm ") + lunarYearName(l.y), a.x + a.w / 2, a.y + 210, 16, t.sub, false, 1);
    }
  }
  Soft soft() override { return {tl("Xong"), tl("Xong"), tl("Hủy")}; }
  void key(Key k) override {
    int n = isTime ? 2 : 3;
    if (k == Key::Left) f = (f + n - 1) % n;
    else if (k == Key::Right) f = (f + 1) % n;
    else if (k == Key::Up || k == Key::Down) {
      int s = k == Key::Up ? 1 : -1;
      if (isTime) { if (f == 0) v[0] = (v[0] + s + 24) % 24; else v[1] = (v[1] + s * 5 + 60) % 60; }
      else {
        if (f == 0) { int dm = daysIn(v[1], v[2]); v[0] = (v[0] - 1 + s + dm) % dm + 1; }
        else if (f == 1) v[1] = (v[1] - 1 + s + 12) % 12 + 1;
        else v[2] = std::max(1950, std::min(2100, v[2] + s));
        v[0] = std::min(v[0], daysIn(v[1], v[2]));
      }
    } else if (k == Key::OK || k == Key::A) { auto d = done; int a0 = v[0], a1 = v[1], a2 = v[2]; pop(); d(a0, a1, a2); }
    else if (k == Key::B) pop();
  }
};
static Screen* pickDate(const std::string& t, int d, int m, int y, std::function<void(int, int, int)> done) { auto* p = new DateTimePicker; p->ttl = t; p->isTime = false; p->v[0] = d; p->v[1] = m; p->v[2] = y; p->done = done; p->ic = "calendar"; return p; }
static Screen* pickTime(const std::string& t, int h, int mi, std::function<void(int, int)> done) { auto* p = new DateTimePicker; p->ttl = t; p->isTime = true; p->v[0] = h; p->v[1] = mi; p->v[2] = 0; p->done = [done](int a, int b, int) { done(a, b); }; p->ic = "recent"; return p; }

// ================= reminders (stored as "y-m-d-h-mi-done|text" entries) =================
struct Rem { int y, m, d, h, mi; bool done; std::string text; };
static std::vector<Rem> rems() {
  std::vector<Rem> o;
  for (auto& e : split(S.get("reminders"), '\x1f')) {
    auto bar = e.find('|');
    if (bar == std::string::npos) continue;
    auto p = split(e.substr(0, bar), '-');
    if (p.size() < 6) continue;
    o.push_back({atoi(p[0].c_str()), atoi(p[1].c_str()), atoi(p[2].c_str()), atoi(p[3].c_str()), atoi(p[4].c_str()), p[5] == "1", e.substr(bar + 1)});
  }
  std::sort(o.begin(), o.end(), [](const Rem& a, const Rem& b) { return std::tie(a.y, a.m, a.d, a.h, a.mi) < std::tie(b.y, b.m, b.d, b.h, b.mi); });
  return o;
}
static void saveRems(const std::vector<Rem>& v) {
  std::string s;
  for (auto& r : v) { char b[48]; snprintf(b, sizeof b, "%d-%d-%d-%d-%d-%d|", r.y, r.m, r.d, r.h, r.mi, r.done ? 1 : 0); if (!s.empty()) s += '\x1f'; s += b + r.text; }
  S.set("reminders", s);
}
static std::string remWhen(const Rem& r) { char b[16]; snprintf(b, sizeof b, "%02d:%02d", r.h, r.mi); return std::string(b) + " · " + DAYS[weekday(r.d, r.m, r.y)] + ", " + dateNum(r.d, r.m, r.y); }
void addReminder(int d, int m, int y) {
  push(OSK(tl("Nội dung nhắc việc"), "", false, [d, m, y](const std::string& text) {
    if (text.empty()) return;
    push(pickTime(tl("Giờ nhắc"), 9, 0, [d, m, y, text](int h, int mi) {
      auto v = rems(); v.push_back({y, m, d, h, mi, false, text}); saveRems(v);
      char b[8]; snprintf(b, sizeof b, "%02d:%02d", h, mi);
      toast(std::string(tl("Sẽ nhắc lúc ")) + b + tl(" ngày ") + std::to_string(d) + "/" + std::to_string(m), "bell");
    }));
  }));
}
Screen* RemindersApp() {
  auto* l = new ListScreen(tl("Nhắc việc"), [] {
    Items v; auto rs = rems();
    for (int i = 0; i < (int)rs.size(); i++) {
      Item it; it.label = rs[i].text; it.sub = remWhen(rs[i]) + (rs[i].done ? tl(" · đã nhắc") : ""); it.icon = "bell"; it.color = hex(rs[i].done ? "90a4ae" : "f9a825"); it.okLabel = "";
      v.push_back(it);
    }
    return v;
  }, "bell");
  l->empty = tl("Chưa có nhắc việc.\nNhấn Tùy chọn › Thêm nhắc việc.");
  l->opts = [](const Item&, int i) {
    struct tm t = nowTm();
    std::vector<Opt> o{{tl("Thêm nhắc việc hôm nay"), "plus", [t] { addReminder(t.tm_mday, t.tm_mon + 1, t.tm_year + 1900); }},
                       {tl("Thêm nhắc việc ngày khác"), "calendar", [t] { push(pickDate(tl("Ngày nhắc"), t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, [](int d, int m, int y) { addReminder(d, m, y); })); }}};
    auto rs = rems();
    if (i >= 0 && i < (int)rs.size()) o.push_back({tl("Xóa nhắc việc này"), "trash", [i] { auto v = rems(); if (i < (int)v.size()) { v.erase(v.begin() + i); saveRems(v); } }, hex("c62828")});
    if (std::any_of(rs.begin(), rs.end(), [](const Rem& r) { return r.done; })) o.push_back({tl("Xóa các nhắc việc đã xong"), "trash", [] { auto v = rems(); v.erase(std::remove_if(v.begin(), v.end(), [](const Rem& r) { return r.done; }), v.end()); saveRems(v); }, hex("c62828")});
    return o;
  };
  return l;
}
struct Ring : Screen {
  std::string text, when; double t = 0, nextBell = 0;
  Ring() { full = true; live = true; capture = true; }
  void tick(double dt) override {
    t += dt;
    if (t >= nextBell && t < 60 && isDevice()) { nextBell = t + 2.5; run("aplay -q " + G.assetDir + "/bell.wav >/dev/null 2>&1 &"); }
  }
  void draw(const SDL_Rect&) override {
    const Theme& th = theme();
    bool on = (int)(t * 2) % 2;
    G.grad(0, 0, W, H, on ? th.a2 : th.a1, th.a1);
    G.icon("bell", W / 2 - 40, 90, 80, {255, 255, 255});
    G.text(tl("Nhắc việc · ") + when, W / 2, 190, 17, {255, 255, 255, 220}, false, 1);
    G.text(text, W / 2, 222, 30, {255, 255, 255}, true, 1, W - 60);
    G.text(tl("OK: Đã xong · Tùy chọn: nhắc lại sau 10 phút"), W / 2, 300, 16, {255, 255, 255, 220}, false, 1);
  }
  void key(Key k) override {
    if (k == Key::A) {
      time_t s = time(nullptr) + 600; struct tm t2; localtime_r(&s, &t2);
      auto v = rems(); v.push_back({t2.tm_year + 1900, t2.tm_mon + 1, t2.tm_mday, t2.tm_hour, t2.tm_min, false, text}); saveRems(v);
      pop(); toast(tl("Nhắc lại sau 10 phút"), "bell");
    } else if (k == Key::OK || k == Key::B) pop();
  }
};
// called about once a second from the main loop: fire due reminders
extern bool g_booted;
void checkReminders() {
  if (!g_booted) return;
  static time_t last = 0;
  time_t now = time(nullptr);
  if (now - last < 5) return;
  last = now;
  struct tm t = nowTm();
  auto v = rems();
  bool changed = false;
  for (auto& r : v) {
    if (r.done) continue;
    if (std::tie(r.y, r.m, r.d, r.h, r.mi) <= std::make_tuple(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min)) {
      r.done = true; changed = true;
      auto* ring = new Ring; ring->text = r.text; char b[8]; snprintf(b, sizeof b, "%02d:%02d", r.h, r.mi); ring->when = b;
      push(ring);
      break;
    }
  }
  if (changed) saveRems(v);
}

// ================= day countdown =================
static int dayNumber(int d, int m, int y) { struct tm t{}; t.tm_mday = d; t.tm_mon = m - 1; t.tm_year = y - 1900; t.tm_hour = 12; return (int)(mktime(&t) / 86400); }
struct Ev { std::string name; int d, m, y; bool lunar, yearly, builtin; };
static std::vector<Ev> events() {
  std::vector<Ev> o{{tl("Tết Nguyên đán"), 1, 1, 0, true, true, true}, {tl("Tết Trung thu"), 15, 8, 0, true, true, true}, {tl("Quốc khánh"), 2, 9, 0, false, true, true},
                    {tl("Giáng sinh"), 25, 12, 0, false, true, true}, {tl("Năm mới"), 1, 1, 0, false, true, true}};
  for (auto& e : split(S.get("events"), '\x1f')) {
    auto p = split(e, '|');
    if (p.size() < 6) continue;
    o.push_back({p[0], atoi(p[1].c_str()), atoi(p[2].c_str()), atoi(p[3].c_str()), p[4] == "1", p[5] == "1", false});
  }
  return o;
}
static void saveEvents(const std::vector<Ev>& v) {
  std::string s;
  for (auto& e : v) { if (e.builtin) continue; if (!s.empty()) s += '\x1f'; s += e.name + "|" + std::to_string(e.d) + "|" + std::to_string(e.m) + "|" + std::to_string(e.y) + "|" + (e.lunar ? "1" : "0") + "|" + (e.yearly ? "1" : "0"); }
  S.set("events", s);
}
// next occurrence as (d, m, y), or false when a one-time date has passed
static bool nextOccur(const Ev& e, int& od, int& om, int& oy) {
  struct tm t = nowTm();
  int d = t.tm_mday, m = t.tm_mon + 1, y = t.tm_year + 1900;
  if (e.lunar) {
    for (int i = 0; i < 800; i++) {
      Lunar l = toLunar(d, m, y);
      if (!l.leap && l.d == e.d && l.m == e.m && (e.yearly || l.y == e.y)) { od = d; om = m; oy = y; return true; }
      if (++d > daysIn(m, y)) { d = 1; if (++m > 12) { m = 1; y++; } }
    }
    return false;
  }
  int yy = e.yearly ? y : e.y;
  if (e.yearly && dayNumber(e.d, e.m, yy) < dayNumber(d, m, y)) yy++;
  if (!e.yearly && dayNumber(e.d, e.m, yy) < dayNumber(d, m, y)) return false;
  od = e.d; om = e.m; oy = yy;
  return true;
}
Screen* DayCountApp() {
  auto* l = new ListScreen(tl("Đếm ngày"), [] {
    struct tm t = nowTm();
    int today = dayNumber(t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    std::vector<std::pair<int, Item>> rows;
    for (auto& e : events()) {
      int d, m, y;
      bool ok = nextOccur(e, d, m, y);
      int diff = ok ? dayNumber(d, m, y) - today : 99999;
      Item it; it.label = e.name; it.icon = e.builtin ? "star" : "calendar"; it.color = hex(e.builtin ? "c62828" : "00897b");
      it.sub = (ok ? std::string(DAYS[weekday(d, m, y)]) + ", " + dateNum(d, m, y) : tl("Đã qua")) + (e.lunar ? " · " + std::to_string(e.d) + "/" + std::to_string(e.m) + tl(" âm lịch") : "");
      it.right = !ok ? "—" : diff == 0 ? tl("Hôm nay!") : std::to_string(diff) + tl(" ngày");
      rows.push_back({diff, it});
    }
    std::stable_sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.first < b.first; });
    Items v; for (auto& r : rows) v.push_back(r.second);
    return v;
  }, "timer");
  l->opts = [](const Item& it, int i) {
    std::string name = i >= 0 ? it.label : "";
    return std::vector<Opt>{
        {tl("Thêm ngày dương lịch"), "plus", [] {
           push(OSK(tl("Tên sự kiện"), "", false, [](const std::string& n) {
             if (n.empty()) return;
             struct tm t = nowTm();
             push(pickDate(tl("Chọn ngày"), t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, [n](int d, int m, int y) { auto v = events(); v.push_back({n, d, m, y, false, true, false}); saveEvents(v); }));
           }));
         }},
        {tl("Thêm ngày âm lịch (sinh nhật, giỗ…)"), "moon", [] {
           push(OSK(tl("Tên sự kiện"), "", false, [](const std::string& n) {
             if (n.empty()) return;
             std::vector<std::string> ms, ds;
             for (int i = 1; i <= 12; i++) ms.push_back(tl("Tháng ") + std::to_string(i));
             for (int i = 1; i <= 30; i++) ds.push_back(tl("Ngày ") + std::to_string(i));
             openPicker(tl("Tháng âm lịch"), ms, 0, [n, ds](int mi) {
               openPicker(tl("Ngày âm lịch"), ds, 0, [n, mi](int di) { auto v = events(); v.push_back({n, di + 1, mi + 1, 0, true, true, false}); saveEvents(v); toast(tl("Đã thêm ") + n, "check"); });
             });
           }));
         }},
        {tl("Xóa sự kiện này"), "trash", [name] { auto v = events(); v.erase(std::remove_if(v.begin(), v.end(), [&](const Ev& e) { return !e.builtin && e.name == name; }), v.end()); saveEvents(v); }, hex("c62828")}};
  };
  return l;
}

// ================= key test =================
struct KeyTest : Screen {
  std::map<int, Uint32> hit; Uint32 lastB = 0; Key last = Key::None;
  KeyTest() { ic = "key"; live = true; capture = true; }
  std::string title() override { return tl("Kiểm tra phím"); }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    struct P { Key k; int x, y, w, h; const char* n; };
    P ps[] = {{Key::L, 40, 20, 110, 44, "L"}, {Key::R, 490, 20, 110, 44, "R"}, {Key::Up, 110, 100, 60, 56, "▲"}, {Key::Down, 110, 220, 60, 56, "▼"},
              {Key::Left, 50, 160, 60, 56, "◀"}, {Key::Right, 170, 160, 60, 56, "▶"}, {Key::A, 400, 120, 90, 56, tl("Tùy chọn")}, {Key::OK, 300, 180, 90, 56, "OK"}, {Key::B, 500, 180, 90, 56, "B"}};
    Uint32 now = SDL_GetTicks();
    for (auto& p : ps) {
      bool on = hit.count((int)p.k) && now - hit[(int)p.k] < 250;
      if (on) G.grad(a.x + p.x, a.y + p.y, p.w, p.h, t.hl1, t.hl2, 10); else G.round(a.x + p.x, a.y + p.y, p.w, p.h, {0, 0, 0, 26}, 10);
      G.text(p.n, a.x + p.x + p.w / 2, a.y + p.y + p.h / 2 - 11, 16, on ? Col{255, 255, 255} : t.sub, true, 1);
    }
    G.text(tl("Nhấn B hai lần liên tiếp để thoát"), a.x + a.w / 2, a.y + a.h - 34, 15, t.sub, false, 1);
  }
  Soft soft() override { return {"", "", tl("Thoát (B×2)")}; }
  void key(Key k) override {
    Uint32 now = SDL_GetTicks(); hit[(int)k] = now; last = k;
    if (k == Key::B) { if (now - lastB < 600) { pop(); return; } lastB = now; }
  }
};
Screen* KeyTestApp() { return new KeyTest; }

}  // namespace s40
