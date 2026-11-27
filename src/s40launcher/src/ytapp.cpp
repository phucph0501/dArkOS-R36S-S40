// YouTube for the S40 launcher (Shorts show up in the normal search results), without the web page: search/list through yt-dlp, play with
// mpv (/opt/ytdl/ytplay.sh, gamepad through gptokeyb). No page means no ads and far less RAM/CPU than the browser.
// Extraction: /opt/ytdl/ytdl.sh (yt-dlp + Node 22 for YouTube's JS challenge, ~20 s per video on the RK3326).
// The maximum video height is a setting ("ytmax": 240 / 360 / 480, default 360).
#include <atomic>
#include <map>
#include <memory>
#include <fstream>
#include <SDL2/SDL_image.h>
#include <sys/stat.h>

#include <thread>

#include "s40.h"
#include "s40kb.h"

namespace s40 {
namespace {

const char* YTDL = "/opt/ytdl/ytdl.sh";

bool ytReady() { return fileExists(YTDL) && fileExists("/opt/ytdl/node/bin/node") && fileExists("/opt/ytdl/ytplay.sh"); }

bool ytOnline() {
  if (!S.getb("wifi", true) || sys::wifiSsid().empty()) { toast(tl("Cần kết nối Wi-Fi"), "wifioff"); return false; }
  return true;
}

std::string shq(const std::string& s) {  // single-quote for sh
  std::string o = "'";
  for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
  return o + "'";
}

std::string urlEnc(const std::string& s) {  // percent-encode UTF-8 for a query string
  static const char* hx = "0123456789ABCDEF";
  std::string o;
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else if (c == ' ') o += '+';
    else { o += '%'; o += hx[c >> 4]; o += hx[c & 15]; }
  }
  return o;
}

int maxHeight() { int h = S.geti("ytmax", 360); return h == 240 || h == 480 ? h : 360; }

// format: the best H.264 up to the chosen height (video + m4a audio), else the best single file up to it
std::string formatFor(int h) {
  std::string H = std::to_string(h);
  return "bv*[height<=" + H + "][fps<=30][vcodec^=avc1]+ba[ext=m4a]/b[height<=" + H + "][vcodec^=avc1]/b[height<=" + H + "]";
}

std::string histPath() {
  const char* home = getenv("HOME");
  return std::string(home ? home : "/tmp") + "/.config/s40/yt_recent";
}

struct Vid { std::string id, title, chan; int dur = 0; };

std::vector<Vid> loadRecent() {
  std::vector<Vid> v;
  std::ifstream f(histPath());
  std::string l;
  while (std::getline(f, l)) {
    auto p = split(l, '\t');
    if (p.size() >= 2) v.push_back({p[0], p[1], p.size() > 2 ? p[2] : "", 0});
  }
  return v;
}

void addRecent(const Vid& x) {
  auto v = loadRecent();
  for (size_t i = 0; i < v.size(); i++) if (v[i].id == x.id) { v.erase(v.begin() + i); break; }
  v.insert(v.begin(), x);
  if (v.size() > 30) v.resize(30);
  std::ofstream f(histPath(), std::ios::trunc);
  for (auto& e : v) f << e.id << '\t' << e.title << '\t' << e.chan << '\n';
}

std::string fmtDur(int s) {
  if (s <= 0) return "";
  char b[16];
  if (s >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
  else snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
  return b;
}

// A loading screen: runs `work` on a thread (yt-dlp takes seconds), then `done(output)` on the UI thread.
// B cancels: the thread only touches a shared state, so it may finish after this screen is gone.
struct YtLoad : Screen {
  struct Shared { std::atomic<int> state{0}; std::string out; };  // 0 running, 1 finished
  std::string ttl, step;
  std::function<std::string()> work;
  std::function<bool(const std::string&, std::string&)> done;  // return false (+message) = failed
  std::shared_ptr<Shared> sh_ = std::make_shared<Shared>();
  bool failed = false, handled = false;
  std::string err;
  double t = 0;
  YtLoad(std::string title, std::string s, std::function<std::string()> w, std::function<bool(const std::string&, std::string&)> d)
      : ttl(std::move(title)), step(std::move(s)), work(std::move(w)), done(std::move(d)) {
    live = true; noIdle = true; ic = "video";
  }
  std::string title() override { return ttl; }
  void onEnter() override {
    auto st = sh_; auto w = work;
    std::thread([st, w] { st->out = w(); st->state = 1; }).detach();
  }
  void tick(double dt) override {
    t += dt;
    if (sh_->state == 1 && !handled) {
      handled = true;
      std::string e;
      if (done(sh_->out, e)) return;  // done() replaced this screen
      err = e.empty() ? tl("Có lỗi xảy ra") : e;
      failed = true;
    }
  }
  void draw(const SDL_Rect& a) override {
    const Theme& th2 = theme();
    int x = a.x + 28, y = a.y + 20, w = a.w - 56;
    G.text(failed ? tl("Không thành công") : tl("Đang thực hiện…"), x, y, 20, th2.txt, true);
    y += 36;
    G.round(x, y, w, 10, {0, 0, 0, 40}, 5);
    if (!failed) {
      int bw = w / 4, off = (int)(t * 220) % (w + bw) - bw;
      G.clip(nullptr);
      G.round(std::max(x, x + off), y, std::min(bw, x + w - std::max(x, x + off)), 10, th2.hl1, 5);
    }
    y += 26;
    if (!failed) {
      G.text("› " + step, x, y, 15, th2.sub);
      y += 26;
      G.text(tl("Lần đầu có thể mất khoảng 20 giây."), x, y, 14, th2.sub);
    } else {
      G.text("✗ " + err, x, y + 6, 16, hex("c62828"), true, 0, w);
    }
  }
  Soft soft() override { return {"", "", failed ? tl("Quay lại") : tl("Hủy")}; }
  void key(Key k) override {
    if (k != Key::B && !(failed && k == Key::OK)) return;
    if (!failed && sh_->state == 0) run("pkill -f /opt/ytdl/pkg >/dev/null 2>&1; pkill -f /opt/ytdl/node >/dev/null 2>&1");  // stop the work in progress
    pop();
  }
};

void playVideo(const Vid& v) {
  if (!ytReady() || !ytOnline()) return;
  addRecent(v);
  std::string cmd = std::string(YTDL) + " -f " + shq(formatFor(maxHeight())) + " -g " + shq("https://www.youtube.com/watch?v=" + v.id) + " 2>&1";
  push(new YtLoad(v.title, tl("Đang lấy video…"), [cmd] { return sh(cmd); },
                  [](const std::string& out, std::string& err) {
                    std::vector<std::string> urls;
                    for (auto& l : split(out, '\n')) { std::string t = trim(l); if (t.rfind("http", 0) == 0) urls.push_back(t); }
                    if (urls.empty()) {
                      err = tl("Không lấy được video. Thử lại, hoặc cập nhật yt-dlp trong menu YouTube.");
                      return false;
                    }
                    std::string c = "/opt/ytdl/ytplay.sh " + shq(urls[0]) + (urls.size() > 1 ? " " + shq(urls[1]) : "");
                    pop();                        // this loading screen
                    sys::launchExternal(c);       // returns when the player exits
                    return true;
                  }));
}

// Result list with thumbnails (like NewPipe). Thumbnails are small JPEGs (~10 KB) fetched in the
// background into /tmp/ytthumb; a texture exists only for the rows on screen and is freed when the screen closes.
struct VideoList : Screen {
  std::string ttl;
  std::vector<Vid> vs;
  int sel = 0, top = 0;
  std::map<std::string, SDL_Texture*> tex;
  SDL_Renderer* texRen = nullptr;  // renderer the textures belong to
  double acc = 0;
  static constexpr int ROW = 78, THW = 112, THH = 63;
  VideoList(std::string t, std::vector<Vid> v) : ttl(std::move(t)), vs(std::move(v)) {
    ic = "video";
    mkdir("/tmp/ytthumb", 0755);
    std::string ids;
    for (auto& x : vs) if (!fileExists("/tmp/ytthumb/" + x.id + ".jpg")) ids += x.id + '\n';
    if (!ids.empty()) {  // detached on purpose: it only writes files, so it can outlive this screen
      std::string cmd = "printf '%s' " + shq(ids) + " | xargs -P6 -I{} curl -s -m 10 -o /tmp/ytthumb/{}.jpg https://i.ytimg.com/vi/{}/mqdefault.jpg";
      std::thread([cmd] { run(cmd + " >/dev/null 2>&1"); }).detach();
    }
    live = true;
  }
  ~VideoList() override { if (texRen == G.ren) for (auto& kv : tex) if (kv.second) SDL_DestroyTexture(kv.second); }
  std::string title() override { return ttl; }
  std::string info() override { return std::to_string(vs.empty() ? 0 : sel + 1) + "/" + std::to_string(vs.size()); }
  SDL_Texture* thumb(const Vid& v) {
    if (texRen != G.ren) { tex.clear(); texRen = G.ren; }  // a player ran: the old renderer and its textures are gone
    auto it = tex.find(v.id);
    if (it != tex.end()) return it->second;
    std::string path = "/tmp/ytthumb/" + v.id + ".jpg";
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || st.st_size < 500) return nullptr;  // not downloaded yet: try again later
    SDL_Texture* t = IMG_LoadTexture(G.ren, path.c_str());
    if (t) tex[v.id] = t;
    return t;
  }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    if (vs.empty()) { G.text(tl("Không tìm thấy video nào"), a.x + a.w / 2, a.y + 120, 18, t.sub, false, 1); return; }
    int vis = std::max(1, a.h / ROW);
    if (sel < top) top = sel;
    if (sel >= top + vis) top = sel - vis + 1;
    for (int i = 0; i < vis && top + i < (int)vs.size(); i++) {
      const Vid& v = vs[top + i];
      int y = a.y + i * ROW;
      bool s = top + i == sel;
      if (s) G.grad(a.x + 4, y + 2, a.w - 8, ROW - 4, t.hl1, t.hl2, 8);
      Col fg = s ? Col{255, 255, 255} : t.txt, fg2 = s ? Col{225, 235, 250} : t.sub;
      SDL_Rect d{a.x + 12, y + (ROW - THH) / 2, THW, THH};
      G.rect(d.x, d.y, d.w, d.h, {40, 44, 52});
      if (SDL_Texture* tx = thumb(v)) SDL_RenderCopy(G.ren, tx, nullptr, &d);
      std::string dur = fmtDur(v.dur);
      if (!dur.empty()) {
        int w = G.textW(dur, 11, true) + 8;
        G.round(d.x + d.w - w - 3, d.y + d.h - 17, w, 14, {0, 0, 0, 200}, 3);
        G.text(dur, d.x + d.w - w / 2 - 3, d.y + d.h - 16, 11, {255, 255, 255}, true, 1);
      }
      int tx0 = d.x + d.w + 10, tw = a.x + a.w - 14 - tx0;
      // title on up to two lines
      std::u32string u = s40kb::toU32(v.title);
      std::string l1, l2;
      size_t k = 0;
      while (k < u.size() && G.textW(l1 + s40kb::toU8(u.substr(k, 1)), 15, true) <= tw) { l1 += s40kb::toU8(u.substr(k, 1)); k++; }
      while (k < u.size() && G.textW(l2 + s40kb::toU8(u.substr(k, 1)), 15, true) <= tw - 14) { l2 += s40kb::toU8(u.substr(k, 1)); k++; }
      if (k < u.size()) l2 += "…";
      G.text(l1, tx0, y + 8, 15, fg, true, 0, tw);
      if (!l2.empty()) G.text(l2, tx0, y + 27, 15, fg, true, 0, tw);
      G.text(v.chan, tx0, y + ROW - 24, 13, fg2, false, 0, tw);
    }
    // scrollbar
    int n = (int)vs.size();
    if (n > vis) {
      int bh = std::max(20, a.h * vis / n), by = a.y + (a.h - bh) * top / std::max(1, n - vis);
      G.round(a.x + a.w - 5, by, 3, bh, {0, 0, 0, 90}, 1);
    }
  }
  void tick(double dt) override { acc += dt; }
  Soft soft() override { return {"", vs.empty() ? "" : tl("Xem"), tl("Quay lại")}; }
  void key(Key k) override {
    int n = (int)vs.size();
    if (k == Key::B) { pop(); return; }
    if (!n) return;
    int vis = 4;
    switch (k) {
      case Key::Up: sel = (sel - 1 + n) % n; break;
      case Key::Down: sel = (sel + 1) % n; break;
      case Key::Left: case Key::L: sel = std::max(0, sel - vis); break;
      case Key::Right: case Key::R: sel = std::min(n - 1, sel + vis); break;
      case Key::OK: playVideo(vs[sel]); break;
      default: break;
    }
  }
};

void showResults(const std::string& title, std::vector<Vid> vs) { push(new VideoList(title, std::move(vs))); }

void search(const std::string& q) {
  if (!ytReady() || !ytOnline()) return;
  const int n = 15;
  std::string src = "ytsearch" + std::to_string(n) + ":" + q;
  std::string cmd = std::string(YTDL) + " --flat-playlist --playlist-end " + std::to_string(n) +
                    " --print " + shq("%(id)s\t%(title)s\t%(duration)s\t%(channel)s") + " " + shq(src) + " 2>/dev/null";
  std::string ttl = q;
  push(new YtLoad(ttl, tl("Đang tìm…"), [cmd] { return sh(cmd); },
                  [ttl](const std::string& out, std::string& err) {
                    std::vector<Vid> vs;
                    for (auto& l : split(out, '\n')) {
                      auto p = split(l, '\t');
                      if (p.size() < 2 || p[0].size() < 6) continue;
                      Vid x; x.id = p[0]; x.title = p[1];
                      x.dur = p.size() > 2 ? atoi(p[2].c_str()) : 0;
                      x.chan = p.size() > 3 && p[3] != "NA" ? p[3] : "";
                      vs.push_back(x);
                    }
                    if (vs.empty()) { err = tl("Không tìm thấy video nào"); return false; }
                    pop();
                    showResults(ttl, vs);
                    return true;
                  }));
}

void updateYtDlp() {
  if (!ytOnline()) return;
  push(new YtLoad(tl("Cập nhật yt-dlp"), tl("Đang tải bản mới…"), [] { return sh("/opt/ytdl/ytdl-update.sh 2>&1 | tail -3"); },
                  [](const std::string& out, std::string& err) {
                    std::string v = trim(out);
                    if (v.empty() || v.find("rror") != std::string::npos || v.find("Traceback") != std::string::npos) {
                      err = tl("Không cập nhật được. Kiểm tra Wi-Fi.");
                      return false;
                    }
                    pop();
                    toast(tl("yt-dlp: ") + v.substr(v.find_last_of('\n') == std::string::npos ? 0 : v.find_last_of('\n') + 1));
                    return true;
                  }));
}

Screen* home() {
  auto* l = new ListScreen("YouTube", [] {
    Items v;
    Item s; s.label = tl("Tìm kiếm"); s.sub = tl("Gõ từ khóa để tìm video"); s.icon = "web"; s.color = hex("c62828"); s.okLabel = tl("Tìm");
    s.ok = [] { push(OSK(tl("Tìm kiếm"), "", false, [](const std::string& q) { if (!trim(q).empty()) search(trim(q)); })); };
    v.push_back(s);
    struct P { const char* label; const char* query; };
    static const P vid[] = {{"Âm nhạc", "nhạc trẻ hay nhất"}, {"Tin tức 24h", "tin tức 24h"}, {"Bóng đá", "bóng đá highlights"},
                            {"Hoạt hình", "phim hoạt hình thiếu nhi"}, {"Ẩm thực", "nấu ăn món ngon"}, {"Công nghệ", "công nghệ mới"},
                            {"Du lịch", "du lịch Việt Nam"}};
    auto addP = [&](const P& p) {
      Item it; it.label = tl(p.label); it.sub = p.query; it.icon = "video"; it.color = hex("ad1457"); it.okLabel = tl("Xem");
      std::string q = p.query;
      it.ok = [q] { search(q); };
      v.push_back(it);
    };
    for (auto& p : vid) addP(p);
    Item r; r.label = tl("Gần đây"); r.sub = tl("Video đã xem"); r.icon = "recent"; r.color = hex("546e7a"); r.okLabel = tl("Mở");
    r.ok = [] {
      auto rec = loadRecent();
      showResults(tl("Gần đây"), rec);
    };
    v.push_back(r);
    Item q; q.label = tl("Độ phân giải tối đa: ") + std::to_string(maxHeight()) + "p"; q.sub = tl("Thấp hơn thì nhẹ máy và nhanh hơn"); q.icon = "tools"; q.color = hex("37474f"); q.okLabel = tl("Đổi");
    q.ok = [] {
      int cur = maxHeight() == 240 ? 0 : maxHeight() == 360 ? 1 : 2;
      openPicker(tl("Độ phân giải tối đa"), {"240p", "360p", "480p"}, cur, [](int i) { S.seti("ytmax", i == 0 ? 240 : i == 1 ? 360 : 480); S.save(); });
    };
    v.push_back(q);
    Item u; u.label = tl("Cập nhật yt-dlp"); u.sub = tl("Cần khi YouTube đổi cách bảo vệ"); u.icon = "download"; u.color = hex("1e88e5"); u.okLabel = tl("Cập nhật");
    u.ok = [] { updateYtDlp(); };
    v.push_back(u);
    return v;
  }, "video");
  l->rowH = 50;
  l->vis = 7;
  return l;
}

}  // namespace

bool ytAppInstalled() { return ytReady(); }
Screen* YouTubeApp() { return home(); }
}  // namespace s40
