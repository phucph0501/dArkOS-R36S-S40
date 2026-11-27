// Shared widgets: list, options popup, pickers, dialogs, task runner, on-screen keyboard.
#include "s40.h"
#include "s40kb.h"
#include <algorithm>
#include <atomic>
#include <thread>

namespace s40 {

// ================= list =================
void drawItems(const Items& its, int& sel, int& topRow, int vis, int rowH, const SDL_Rect& a, bool inPopup) {
  const Theme& t = theme();
  int n = (int)its.size();
  if (sel >= n) sel = std::max(0, n - 1);
  if (sel < topRow) topRow = sel;
  if (sel >= topRow + vis) topRow = sel - vis + 1;
  topRow = std::max(0, std::min(topRow, std::max(0, n - vis)));
  int x0 = a.x + (inPopup ? 4 : 6), w = a.w - (inPopup ? 8 : 22);
  for (int i = topRow; i < std::min(n, topRow + vis); i++) {
    const Item& it = its[i];
    int y = a.y + (i - topRow) * rowH;
    bool s = i == sel;
    if (s) G.grad(x0, y + 1, w, rowH - 2, t.hl1, t.hl2, 8);
    else G.rect(x0 + 8, y + rowH - 1, w - 16, 1, {0, 0, 0, 16});
    Col fg = s ? Col{255, 255, 255} : t.txt, sub = s ? Col{255, 255, 255, 225} : t.sub;
    if (it.dis) { fg.a = 110; sub.a = 110; }
    int x = x0 + 10;
    int bs = std::min(32, rowH - 12);
    if (!it.icon.empty()) { G.badge(it.icon, x, y + (rowH - bs) / 2, bs, it.color); x += bs + 10; }
    // right side
    int rx = x0 + w - 10, rw = 0;
    if (it.radio || it.check) {
      Col c = fg;
      if (it.radio) { G.circle(rx - 9, y + rowH / 2, 9, c); G.circle(rx - 9, y + rowH / 2, 7, s ? t.hl2 : t.bg1); if (it.radioOn) G.circle(rx - 9, y + rowH / 2, 4, c); }
      else { G.round(rx - 18, y + rowH / 2 - 9, 18, 18, c, 4); G.round(rx - 16, y + rowH / 2 - 7, 14, 14, s ? t.hl2 : t.bg1, 3); if (it.checkOn) G.round(rx - 13, y + rowH / 2 - 4, 8, 8, c, 2); }
      rw = 26;
    } else if (it.bar >= 0) {
      for (int k = it.barMax - 1; k >= 0; k--) { Col c = fg; c.a = k < it.bar ? 255 : 70; G.round(rx - (it.barMax - k) * 9, y + rowH / 2 - 7, 7, 14, c, 2); }
      rw = it.barMax * 9 + 6;
      if (s && it.adj) { G.text("‹", rx - rw - 4, y + rowH / 2 - 12, 18, fg, true, 2); rw += 16; }
    } else if (!it.right.empty()) {
      std::string r = (s && it.adj) ? "‹ " + it.right + " ›" : it.right;
      rw = G.text(r, rx, y + (rowH - 22) / 2, 15, s ? fg : t.sub, false, 2, 220) + 8;
    }
    int tw = rx - rw - x - 4;
    std::string lab = it.label + (it.fav ? " ★" : "");
    if (!it.sub.empty()) {
      G.text(lab, x, y + rowH / 2 - 21, 18, fg, true, 0, tw);
      G.text(it.sub, x, y + rowH / 2 + 1, 13, sub, false, 0, tw);
    } else G.text(lab, x, y + (rowH - 25) / 2, 18, fg, true, 0, tw);
  }
  if (n > vis && !inPopup) {
    int sx = a.x + a.w - 10, sy = a.y + 8, sh2 = a.h - 16;
    G.round(sx, sy, 5, sh2, {0, 0, 0, 30}, 3);
    G.round(sx, sy + sh2 * topRow / n, 5, std::max(12, sh2 * vis / n), t.a2, 3);
  }
}

ListScreen::ListScreen(std::string t, std::function<Items()> b, std::string icon) : ttl(std::move(t)), build(std::move(b)) { ic = icon; }
std::string ListScreen::info() { int n = (int)build().size(); return n ? std::to_string(sel + 1) + "/" + std::to_string(n) : ""; }
void ListScreen::drawList(const Items& its, const SDL_Rect& a) {
  if (its.empty()) {
    const Theme& t = theme();
    G.icon(ic.empty() ? "folder" : ic, a.x + a.w / 2 - 24, a.y + a.h / 2 - 60, 48, {t.sub.r, t.sub.g, t.sub.b, 120});
    int y = a.y + a.h / 2;
    for (auto& l : split(empty, '\n')) { G.text(l, a.x + a.w / 2, y, 17, t.sub, false, 1, a.w - 40); y += 24; }
    return;
  }
  vis = a.h / rowH;
  drawItems(its, sel, topRow, vis, rowH, a, false);
}
void ListScreen::draw(const SDL_Rect& a) { drawList(build(), a); }
Soft ListScreen::soft() {
  Items its = build();
  if (its.empty()) return {opts ? tl("Tùy chọn") : "", "", tl("Quay lại")};
  const Item& it = its[std::min(sel, (int)its.size() - 1)];
  return {opts ? tl("Tùy chọn") : "", it.okLabel.empty() ? (it.ok ? okDefault : "") : it.okLabel, tl("Quay lại")};
}
void ListScreen::key(Key k) {
  Items its = build();
  int n = (int)its.size();
  if (k == Key::B) { pop(); return; }
  if (!n) { if (k == Key::A && opts) openOptions(opts(Item{}, -1)); return; }
  sel = std::min(sel, n - 1);
  Item it = its[sel];
  switch (k) {
    case Key::Up: sel = (sel - 1 + n) % n; break;
    case Key::Down: sel = (sel + 1) % n; break;
    case Key::Left: case Key::Right:
      if (it.adj && !it.dis) it.adj(k == Key::Left ? -1 : 1);
      else sel = k == Key::Left ? std::max(0, sel - vis) : std::min(n - 1, sel + vis);
      break;
    case Key::OK: if (it.ok) it.ok(); break;
    case Key::A: if (opts) openOptions(opts(it, sel)); break;
    default: break;
  }
}

// ================= popup list (options / picker / multi) =================
struct PopupList : Screen {
  std::string ttl;
  std::function<Items()> build;
  std::function<void(int)> choose;
  std::function<Soft()> sk;
  std::function<bool(Key, int)> extra;
  int sel = 0, topRow = 0;
  PopupList() { overlay = true; }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    G.rect(a.x, a.y, a.w, a.h, {0, 0, 0, 100});
    Items its = build();
    int vis = std::min((int)its.size(), 6), rowH = 40;
    int h = vis * rowH + 44, x = a.x + 28, w = a.w - 56, y = a.y + a.h - h - 8;
    G.round(x - 2, y - 2, w + 4, h + 4, t.a1, 12);
    G.grad(x, y, w, h, t.bg1, t.bg2, 10);
    G.grad(x, y, w, 36, t.a2, t.a1, 10);
    G.rect(x, y + 26, w, 10, t.a1);
    G.text(ttl, x + 14, y + 6, 17, {255, 255, 255}, true, 0, w - 28);
    SDL_Rect la{x, y + 40, w, vis * rowH};
    drawItems(its, sel, topRow, vis, rowH, la, true);
    if ((int)its.size() > vis) {
      int sy = la.y + 2, sh2 = la.h - 4, n = (int)its.size();
      G.round(x + w - 6, sy + sh2 * topRow / n, 3, std::max(10, sh2 * vis / n), t.a2, 2);
    }
  }
  Soft soft() override { return sk ? sk() : Soft{tl("Chọn"), tl("Chọn"), tl("Quay lại")}; }
  void key(Key k) override {
    int n = (int)build().size();
    if (extra && extra(k, sel)) return;
    if (k == Key::Up && n) sel = (sel - 1 + n) % n;
    else if (k == Key::Down && n) sel = (sel + 1) % n;
    else if ((k == Key::OK || k == Key::A) && n) { int s = sel; auto c = choose; pop(); c(s); }
    else if (k == Key::B) pop();
  }
};

void openOptions(std::vector<Opt> opts, const std::string& title) {
  opts.erase(std::remove_if(opts.begin(), opts.end(), [](const Opt& o) { return o.label.empty(); }), opts.end());
  if (opts.empty()) return;
  auto* p = new PopupList;
  p->ttl = title;
  p->build = [opts] { Items v; for (auto& o : opts) { Item it; it.label = o.label; it.icon = o.icon.empty() ? "check" : o.icon; it.color = o.color; v.push_back(it); } return v; };
  p->choose = [opts](int i) { if (opts[i].fn) opts[i].fn(); };
  push(p);
}
void openPicker(const std::string& title, std::vector<std::string> labels, int cur, std::function<void(int)> pick) {
  auto* p = new PopupList;
  p->ttl = title;
  p->sel = std::max(0, cur);
  p->build = [labels, cur] { Items v; for (int i = 0; i < (int)labels.size(); i++) { Item it; it.label = labels[i]; it.radio = true; it.radioOn = i == cur; v.push_back(it); } return v; };
  p->choose = pick;
  push(p);
}
void openMulti(const std::string& title, std::vector<std::string> labels, std::vector<int> chosen, std::function<void(std::vector<int>)> done) {
  auto* p = new PopupList;
  auto set = std::make_shared<std::vector<int>>(chosen);
  p->ttl = title;
  p->build = [labels, set] { Items v; for (int i = 0; i < (int)labels.size(); i++) { Item it; it.label = labels[i]; it.check = true; it.checkOn = std::count(set->begin(), set->end(), i) > 0; v.push_back(it); } return v; };
  p->sk = [set, p] { bool on = std::count(set->begin(), set->end(), p->sel) > 0; return Soft{tl("Xong"), on ? tl("Bỏ chọn") : tl("Đánh dấu"), tl("Hủy")}; };
  p->extra = [set, done](Key k, int sel) {
    if (k == Key::OK) { auto it = std::find(set->begin(), set->end(), sel); if (it != set->end()) set->erase(it); else set->push_back(sel); return true; }
    if (k == Key::A) { auto v = *set; pop(); done(v); return true; }
    return false;
  };
  push(p);
}

// ================= dialogs =================
struct Dialog : Screen {
  std::string text, icn; std::function<void()> yes;
  Dialog() { overlay = true; }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    G.rect(a.x, a.y, a.w, a.h, {0, 0, 0, 100});
    auto lines = split(text, '\n');
    int h = 60 + (int)lines.size() * 26, x = a.x + 60, w = a.w - 120, y = a.y + (a.h - h) / 2;
    G.round(x - 2, y - 2, w + 4, h + 4, t.a1, 12);
    G.grad(x, y, w, h, t.bg1, t.bg2, 10);
    G.grad(x, y, w, 32, t.a2, t.a1, 10);
    G.rect(x, y + 22, w, 10, t.a1);
    G.text(tl("Xác nhận"), x + 14, y + 4, 16, {255, 255, 255}, true);
    G.icon(icn, x + 16, y + 46, 36, t.a2);
    int ly = y + 44;
    for (auto& l : lines) { G.text(l, x + 64, ly, 17, t.txt, false, 0, w - 80); ly += 26; }
  }
  Soft soft() override { return {tl("Có"), tl("Có"), tl("Không")}; }
  void key(Key k) override {
    if (k == Key::A || k == Key::OK) { auto f = yes; pop(); f(); }
    else if (k == Key::B) pop();
  }
};
void confirmBox(const std::string& text, std::function<void()> yes, const std::string& icon) {
  auto* d = new Dialog; d->text = text; d->yes = std::move(yes); d->icn = icon; push(d);
}

struct InfoPop : Screen {
  std::string ttl; std::vector<std::pair<std::string, std::string>> rows;
  InfoPop() { overlay = true; }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    G.rect(a.x, a.y, a.w, a.h, {0, 0, 0, 100});
    int h = 44 + (int)rows.size() * 34, x = a.x + 28, w = a.w - 56, y = a.y + a.h - h - 8;
    G.round(x - 2, y - 2, w + 4, h + 4, t.a1, 12);
    G.grad(x, y, w, h, t.bg1, t.bg2, 10);
    G.grad(x, y, w, 36, t.a2, t.a1, 10);
    G.rect(x, y + 26, w, 10, t.a1);
    G.text(ttl, x + 14, y + 6, 17, {255, 255, 255}, true, 0, w - 28);
    int ry = y + 42;
    for (auto& r : rows) {
      int lw = G.text(r.first, x + 14, ry + 4, 16, t.txt, true, 0, w / 2);
      G.text(r.second, x + w - 14, ry + 4, 16, t.sub, false, 2, w - lw - 50);
      G.rect(x + 10, ry + 33, w - 20, 1, {0, 0, 0, 18});
      ry += 34;
    }
  }
  Soft soft() override { return {"", tl("Đóng"), tl("Quay lại")}; }
  void key(Key k) override { if (k == Key::OK || k == Key::B || k == Key::A) pop(); }
};
void infoBox(const std::string& title, std::vector<std::pair<std::string, std::string>> rows) {
  auto* p = new InfoPop; p->ttl = title; p->rows = std::move(rows); push(p);
}

// ================= task runner (real work in a background thread) =================
struct Task : Screen {
  std::string ttl; std::vector<std::string> steps;
  std::function<bool(std::string&)> work; std::function<void()> done;
  std::atomic<int> state{0};  // 0 running, 1 ok, 2 failed
  std::string err; double t = 0; std::thread th; bool doneCalled = false;
  Task() { live = true; ic = "tools"; noIdle = true; }
  ~Task() override { if (th.joinable()) th.join(); }
  std::string title() override { return ttl; }
  void onEnter() override {
    th = std::thread([this] { std::string e; bool ok = work ? work(e) : true; err = e; state = ok ? 1 : 2; });
  }
  void tick(double dt) override {
    t += dt;
    if (state != 0 && !doneCalled) { doneCalled = true; if (th.joinable()) th.join(); if (state == 1 && done) done(); }
  }
  void draw(const SDL_Rect& a) override {
    const Theme& th2 = theme();
    int x = a.x + 28, y = a.y + 20, w = a.w - 56;
    G.text(state == 0 ? tl("Đang thực hiện…") : state == 1 ? tl("Đã xong") : tl("Không thành công"), x, y, 20, th2.txt, true);
    y += 36;
    G.round(x, y, w, 10, {0, 0, 0, 40}, 5);
    if (state == 0) { int bw = w / 4, off = (int)(t * 220) % (w + bw) - bw; G.clip(nullptr); G.round(std::max(x, x + off), y, std::min(bw, x + w - std::max(x, x + off)), 10, th2.a2, 5); }
    else G.grad(x, y, w, 10, th2.hl1, th2.hl2, 5);
    y += 26;
    int shown = state == 0 ? std::min((int)steps.size(), 1 + (int)(t / 0.8)) : (int)steps.size();
    for (int i = 0; i < shown; i++) { G.text("› " + steps[i], x, y, 15, th2.sub); y += 24; }
    if (state == 1) G.text(tl("✓ Hoàn tất"), x, y + 6, 18, hex("2e7d32"), true);
    if (state == 2) G.text("✗ " + (err.empty() ? std::string(tl("Có lỗi xảy ra")) : err), x, y + 6, 16, hex("c62828"), true, 0, w);
  }
  Soft soft() override { return state == 0 ? Soft{"", "", ""} : Soft{"", "OK", tl("Quay lại")}; }
  void key(Key k) override { if (state != 0 && (k == Key::OK || k == Key::B)) pop(); }
};
void runTask(const std::string& title, std::vector<std::string> steps, std::function<bool(std::string&)> work, std::function<void()> done) {
  auto* t = new Task; t->ttl = title; t->steps = std::move(steps); t->work = std::move(work); t->done = std::move(done); push(t);
}

// ================= on-screen keyboard =================
// Engine (layouts, Vietnamese Telex/VNI, English, Chinese Pinyin, symbols, settings) is shared with
// the browser: common/s40kb.h. Keys: D-pad moves, A types, X = done, B = backspace (back when empty),
// L = next language, R = symbols / letters.
static s40kb::Pinyin g_pinyin;
struct Osk : Screen {
  std::string ttl;
  s40kb::Keyboard kb;
  std::function<void(const std::string&)> done;
  std::string title() override { return ttl; }
  void ensurePinyin() { if (kb.lang() == "zh" && !g_pinyin.loaded) g_pinyin.load(G.assetDir + "/kb/pinyin.txt"); }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    ensurePinyin();
    int x = a.x + 10, w = a.w - 20, y = a.y + 8;
    // input field: text, pinyin being composed (underlined), cursor, language badge
    G.round(x, y, w, 42, t.a2, 8);
    G.round(x + 2, y + 2, w - 4, 38, {255, 255, 255}, 7);
    std::string badge = s40kb::langLabel(kb.lang()) + (kb.lang() == "vi" ? (kb.cfg.viMethod == "vni" ? " VNI" : " Telex") : "");
    int bw = G.textW(badge, 13, true) + 14;
    G.round(x + w - bw - 8, y + 10, bw, 22, t.a2, 11);
    G.text(badge, x + w - 8 - bw / 2, y + 12, 13, {255, 255, 255}, true, 1);
    int maxw = w - bw - 40;
    std::string shown = kb.display();
    int cw = kb.comp.empty() ? 0 : G.textW(kb.comp, 19, false) + 4;
    int tw = G.textW(shown, 19, false);
    if (tw + cw > maxw) {  // keep the end of long text visible
      std::u32string u = s40kb::toU32(shown);
      while (!u.empty() && G.textW(s40kb::toU8(u), 19, false) + cw > maxw - 14) u.erase(0, 1);
      shown = "…" + s40kb::toU8(u);
      tw = G.textW(shown, 19, false);
    }
    G.text(shown, x + 12, y + 8, 19, {17, 17, 17}, false);
    if (cw) { G.text(kb.comp, x + 12 + tw + 2, y + 8, 19, t.a1, false); G.rect(x + 14 + tw, y + 33, cw - 4, 2, t.a1); }
    G.rect(x + 13 + tw + cw, y + 9, 2, 24, t.a2);
    y += 48;
    // candidate bar (Chinese)
    bool zh = kb.lang() == "zh";
    if (zh) {
      auto cs = kb.candidates();
      G.round(x, y, w, 34, {0, 0, 0, 18}, 8);
      if (cs.empty()) G.text(kb.comp.empty() ? tl("Gõ pinyin, ví dụ: nihao") : tl("Không có chữ cho \"") + kb.comp + "\"", x + 12, y + 7, 15, t.sub, false);
      else {
        int cellW = 40, fit = (w - 8) / cellW;
        if (kb.cand < kb.candTop) kb.candTop = kb.cand;
        if (kb.cand >= kb.candTop + fit) kb.candTop = kb.cand - fit + 1;
        for (int i = kb.candTop; i < (int)cs.size() && i < kb.candTop + fit; i++) {
          int cx = x + 4 + (i - kb.candTop) * cellW;
          bool s = kb.row < 0 && i == kb.cand;
          if (s) G.grad(cx, y + 2, cellW - 4, 30, t.hl1, t.hl2, 6);
          G.text(cs[i], cx + (cellW - 4) / 2, y + 5, 19, s ? Col{255, 255, 255} : (i == 0 ? t.a1 : t.txt), i == 0, 1);
        }
      }
      y += 38;
    }
    // keys
    auto rows = kb.rows();
    int gap = 4, rh = std::min(44, (a.y + a.h - y - 6 - gap * 4) / 5);
    for (int ri = 0; ri < (int)rows.size(); ri++) {
      auto& row = rows[ri];
      int units = 0;
      for (auto& k : row) units += k.width;
      float uw = (w - gap * (row.size() - 1)) / (float)units;
      float kx = x;
      for (int ci = 0; ci < (int)row.size(); ci++) {
        const auto& k = row[ci];
        int kw = (int)(uw * k.width + gap * (k.width - 1));
        bool s = kb.row == ri && kb.col == ci;
        bool on = (k.kind == s40kb::Key::Shift && (kb.shift || kb.caps));
        if (s) G.grad((int)kx, y, kw, rh, t.hl1, t.hl2, 7);
        else if (on) G.round((int)kx, y, kw, rh, t.a2, 7);
        else G.round((int)kx, y, kw, rh, k.kind == s40kb::Key::Char ? Col{0, 0, 0, 22} : Col{0, 0, 0, 38}, 7);
        int fs = k.kind == s40kb::Key::Char ? 18 : 15;
        G.text(k.label, (int)kx + kw / 2, y + (rh - fs) / 2 - 3, fs, s || on ? Col{255, 255, 255} : t.txt, true, 1, kw - 4);
        kx += kw + gap;
      }
      y += rh + gap;
    }
  }
  Soft soft() override { return {tl("Xong"), tl("Nhập"), kb.text.empty() && kb.comp.empty() ? tl("Quay lại") : tl("Xóa")}; }
  void finish() { kb.finish(); kb.cfg.save(); auto d = done; auto tx = kb.text; pop(); d(tx); }
  void key(Key k) override {
    ensurePinyin();
    switch (k) {
      case Key::Up: kb.move(-1, 0); break;
      case Key::Down: kb.move(1, 0); break;
      case Key::Left: kb.move(0, -1); break;
      case Key::Right: kb.move(0, 1); break;
      case Key::OK:
        kb.press();
        if (kb.done) finish();
        break;
      case Key::A: finish(); break;
      case Key::B:
        if (kb.text.empty() && kb.comp.empty()) pop();
        else kb.backspace();
        break;
      case Key::L: {  // next language
        auto ls = kb.cfg.langs();
        if (!kb.secret && ls.size() > 1) {
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
  }
};
// test automation: fill the on-screen keyboard on top (then key:A confirms)
bool oskSetText(const std::string& t) {
  if (auto* o = dynamic_cast<Osk*>(top())) { o->kb.text = t; return true; }
  return false;
}
// test automation: type key by key through the engine (Telex / VNI / Pinyin transformations apply)
bool oskTypeKeys(const std::string& keys) {
  auto* o = dynamic_cast<Osk*>(top());
  if (!o) return false;
  o->ensurePinyin();
  for (char c : keys) {
    if (c == ' ') { if (o->kb.lang() == "zh" && !o->kb.comp.empty()) o->kb.commitCandidate(0); else o->kb.text += " "; }
    else o->kb.typeChar(std::string(1, c));
  }
  return true;
}
Screen* OSK(const std::string& title, const std::string& init, bool secret, std::function<void(const std::string&)> done) {
  auto* o = new Osk;
  o->ttl = title;
  o->kb.py = &g_pinyin;
  o->kb.begin(init, secret);
  o->done = std::move(done);
  o->ic = "key";
  o->capture = true;  // L / R switch language / symbols here instead of controlling music
  return o;
}

}  // namespace s40
