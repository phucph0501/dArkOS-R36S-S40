// Per-game options (emulator/core choice + RetroArch per-game overrides) and RetroAchievements.
//
// Per-game core: stored in the launcher settings as gcore.<system>|<rom> = "<emulator>|<core>" and
// used by launchGame(). RetroArch options are written as RetroArch's own game override file,
// <config dir>/<core name>/<rom name>.cfg, which RetroArch loads by itself for that game only.
// RetroAchievements: login exchanges the password for a token (the password is not stored), then
// cheevos_* keys go into retroarch.cfg.
#include "s40.h"
#include <sys/stat.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <fstream>
#include <sstream>

namespace s40 {

static std::string homeDir() {
  if (isDevice()) return "/home/ark";
  const char* h = getenv("S40_RA_HOME");  // desktop testing
  return h ? h : "/tmp/s40ra";
}
static std::string slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static void mkdirs(const std::string& p) {
  for (size_t i = 1; i <= p.size(); i++)
    if (i == p.size() || p[i] == '/') mkdir(p.substr(0, i).c_str(), 0755);
}

// ---- retroarch.cfg ----
static std::string raCfg(const std::string& emu = "retroarch") { return homeDir() + "/.config/" + emu + "/retroarch.cfg"; }
std::string raGet(const std::string& key, const std::string& emu = "retroarch") {
  for (auto& l : split(slurp(raCfg(emu)), '\n')) {
    auto eq = l.find('=');
    if (eq == std::string::npos || trim(l.substr(0, eq)) != key) continue;
    std::string v = trim(l.substr(eq + 1));
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return v;
  }
  return "";
}
void raSet(const std::vector<std::pair<std::string, std::string>>& kv, const std::string& emu = "retroarch") {
  // the global settings (achievements, ...) apply to both RetroArch builds when the 32-bit one is installed
  if (emu == "retroarch" && fileExists(raCfg("retroarch32"))) raSet(kv, "retroarch32");
  std::string path = raCfg(emu);
  auto lines = split(slurp(path), '\n');
  for (auto& [k, v] : kv) {
    std::string line = k + " = \"" + v + "\"";
    bool done = false;
    for (auto& l : lines) {
      auto eq = l.find('=');
      if (eq != std::string::npos && trim(l.substr(0, eq)) == k) { l = line; done = true; break; }
    }
    if (!done) {
      if (!lines.empty() && lines.back().empty()) lines.back() = line; else lines.push_back(line);
    }
  }
  mkdirs(path.substr(0, path.find_last_of('/')));
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  for (size_t i = 0; i < lines.size(); i++) f << lines[i] << (i + 1 < lines.size() || !lines[i].empty() ? "\n" : "");
}

// ---- per-game emulator/core ----
static std::string gkey(const GameSys& s, const std::string& rom) { return s.name + "|" + rom; }
// effective emulator/core for a game: per-game choice if still installed, else the system default
std::pair<std::string, std::string> gameCore(const GameSys& s, const std::string& rom) {
  std::string v = S.get("gcore." + gkey(s, rom));
  if (!v.empty()) {
    auto p = split(v, '|');
    std::string emu = p[0], core = p.size() > 1 ? p[1] : "";
    for (auto& c : s.choices) if (c.first == emu && c.second == core) return c;
  }
  return {s.emulator, s.core};
}
static std::string coreLabel(const std::pair<std::string, std::string>& c) {
  if (c.second.empty()) return c.first + tl(" (độc lập)");
  return c.second + (c.first == "retroarch" ? "" : " · " + c.first);
}
// RetroArch's core name ("Snes9x", "PCSX-ReARMed" ...) names the per-core override folder
static std::string coreName(const std::string& emu, const std::string& core) {
  std::string info = slurp(homeDir() + "/.config/" + emu + "/cores/" + core + "_libretro.info");
  for (auto& l : split(info, '\n')) {
    auto eq = l.find('=');
    if (eq == std::string::npos || trim(l.substr(0, eq)) != "corename") continue;
    std::string v = trim(l.substr(eq + 1));
    if (v.size() >= 2 && v.front() == '"') v = v.substr(1, v.size() - 2);
    return v;
  }
  return "";
}

// RetroArch override options: label, settings suffix, values shown, cfg lines per value
struct GOpt { const char* label; const char* id; const char* icon; std::vector<std::string> vals; std::vector<std::vector<std::pair<std::string, std::string>>> cfg; };
static const std::vector<GOpt>& gopts() {
  static const std::vector<GOpt> v = {
      {tl("Tỉ lệ khung hình"), "aspect", "photo", {tl("Mặc định"), tl("Theo lõi"), "4:3", tl("Toàn màn hình")},
       {{}, {{"aspect_ratio_index", "22"}}, {{"aspect_ratio_index", "0"}}, {{"aspect_ratio_index", "24"}}}},
      {tl("Làm mịn ảnh"), "smooth", "sun", {tl("Mặc định"), tl("Bật"), tl("Tắt")}, {{}, {{"video_smooth", "true"}}, {{"video_smooth", "false"}}}},
      {tl("Phóng số nguyên (điểm ảnh sắc)"), "integer", "sort", {tl("Mặc định"), tl("Bật"), tl("Tắt")},
       {{}, {{"video_scale_integer", "true"}}, {{"video_scale_integer", "false"}}}},
      {tl("Hiện FPS"), "fps", "chip", {tl("Mặc định"), tl("Bật"), tl("Tắt")}, {{}, {{"fps_show", "true"}}, {{"fps_show", "false"}}}},
      {tl("Tự lưu & nạp trạng thái"), "autosave", "download", {tl("Mặc định"), tl("Bật"), tl("Tắt")},
       {{}, {{"savestate_auto_save", "true"}, {"savestate_auto_load", "true"}}, {{"savestate_auto_save", "false"}, {"savestate_auto_load", "false"}}}},
      {tl("Giảm độ trễ (run-ahead, tốn CPU)"), "runahead", "flash", {tl("Mặc định"), tl("Bật"), tl("Tắt")},
       {{}, {{"run_ahead_enabled", "true"}, {"run_ahead_frames", "1"}, {"run_ahead_secondary_instance", "false"}}, {{"run_ahead_enabled", "false"}}}},
  };
  return v;
}
static std::string overridePath(const GameSys& s, const std::string& rom) {
  auto c = gameCore(s, rom);
  if (c.second.empty()) return "";
  std::string cn = coreName(c.first, c.second);
  if (cn.empty()) return "";
  std::string base = rom.substr(0, rom.find_last_of('.'));
  return homeDir() + "/.config/" + c.first + "/config/" + cn + "/" + base + ".cfg";
}
// rewrite the game's override file from the stored choices (removed when nothing is overridden)
static void writeOverride(const GameSys& s, const std::string& rom) {
  std::string path = overridePath(s, rom);
  if (path.empty()) return;
  std::string out;
  for (auto& o : gopts()) {
    std::string v = S.get("gopt." + gkey(s, rom) + "." + o.id, tl("Mặc định"));
    for (size_t i = 0; i < o.vals.size(); i++)
      if (o.vals[i] == v) for (auto& [k, val] : o.cfg[i]) out += k + " = \"" + val + "\"\n";
  }
  if (out.empty()) { remove(path.c_str()); return; }
  mkdirs(path.substr(0, path.find_last_of('/')));
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f << tl("# S40: thiết lập riêng cho ") << rom << "\n" << out;
}

Screen* GameOptionsScreen(const GameSys& sys, const std::string& rom) {
  GameSys s = sys;  // copied: the list may be rescanned while the page is open
  std::string title = rom.substr(0, rom.find_last_of('.'));
  return new ListScreen(tl("Thiết lập: ") + title, [s, rom] {
    Items v;
    auto cur = gameCore(s, rom);
    bool custom = !S.get("gcore." + gkey(s, rom)).empty();
    {
      Item it; it.label = tl("Trình giả lập / lõi"); it.icon = "chip"; it.color = hex("e65100");
      it.right = coreLabel(cur); it.sub = custom ? tl("Riêng cho game này") : tl("Theo mặc định của hệ máy"); it.okLabel = tl("Chọn");
      it.ok = [s, rom] {
        std::vector<std::string> labels{tl("Mặc định của hệ (") + coreLabel({s.emulator, s.core}) + ")"};
        for (auto& c : s.choices) labels.push_back(coreLabel(c));
        std::string now = S.get("gcore." + gkey(s, rom));
        int cur = 0;
        for (size_t i = 0; i < s.choices.size(); i++) if (now == s.choices[i].first + "|" + s.choices[i].second) cur = (int)i + 1;
        openPicker(tl("Trình giả lập cho game"), labels, cur, [s, rom](int k) {
          if (k == 0) S.set("gcore." + gkey(s, rom), "");
          else S.set("gcore." + gkey(s, rom), s.choices[k - 1].first + "|" + s.choices[k - 1].second);
          writeOverride(s, rom);  // the override lives under the (new) core's folder
        });
      };
      v.push_back(it);
    }
    if (cur.second.empty()) {
      Item it; it.label = tl("Thiết lập hình ảnh chỉ có với lõi RetroArch"); it.icon = "info"; it.dis = true; v.push_back(it);
      return v;
    }
    for (auto& o : gopts()) {
      std::string key = "gopt." + gkey(s, rom) + "." + o.id;
      Item it; it.label = o.label; it.icon = o.icon; it.color = hex("1e88e5");
      it.right = S.get(key, tl("Mặc định")); it.sub = tl("◀ ▶ để thay đổi"); it.okLabel = tl("Chọn");
      auto vals = o.vals;
      it.adj = [s, rom, key, vals](int d) {
        int n = (int)vals.size(), i = (int)(std::find(vals.begin(), vals.end(), S.get(key, tl("Mặc định"))) - vals.begin());
        if (i >= n) i = 0;
        S.set(key, vals[(i + d + n) % n]);
        writeOverride(s, rom);
      };
      it.ok = [s, rom, key, vals, label = std::string(o.label)] {
        int i = (int)(std::find(vals.begin(), vals.end(), S.get(key, tl("Mặc định"))) - vals.begin());
        openPicker(label, vals, i, [s, rom, key, vals](int k) { S.set(key, vals[k]); writeOverride(s, rom); });
      };
      v.push_back(it);
    }
    Item clr; clr.label = tl("Bỏ mọi thiết lập riêng"); clr.icon = "trash"; clr.color = hex("c62828"); clr.okLabel = tl("Bỏ");
    clr.ok = [s, rom] {
      std::string path = overridePath(s, rom);
      if (!path.empty()) remove(path.c_str());
      for (auto& o : gopts()) S.set("gopt." + gkey(s, rom) + "." + o.id, "");
      S.set("gcore." + gkey(s, rom), "");
      toast(tl("Đã về thiết lập mặc định"), "check");
    };
    v.push_back(clr);
    return v;
  }, "chip");
}

// ================= RetroAchievements =================
static std::string urlenc(const std::string& s) {
  std::string o; char b[4];
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { snprintf(b, sizeof b, "%%%02X", c); o += b; }
  }
  return o;
}
static std::string jsonStr(const std::string& j, const std::string& k) {
  auto p = j.find("\"" + k + "\"");
  if (p == std::string::npos) return "";
  p = j.find(':', p);
  if (p == std::string::npos) return "";
  p++;
  while (p < j.size() && j[p] == ' ') p++;
  if (p < j.size() && j[p] == '"') { auto e = j.find('"', p + 1); return j.substr(p + 1, e - p - 1); }
  auto e = j.find_first_of(",}", p);
  return trim(j.substr(p, e - p));
}
// password -> token through RetroAchievements' login API; the password only lives in a 0600 temp
// file for the request and is never written to retroarch.cfg
static bool raLogin(const std::string& user, const std::string& pass, std::string& err) {
  std::string tmp = "/tmp/s40-ra-login";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) { err = tl("Không tạo được tệp tạm"); return false; }
  chmod(tmp.c_str(), 0600);
  std::string body = "r=login2&u=" + urlenc(user) + "&p=" + urlenc(pass);
  fwrite(body.data(), 1, body.size(), f);
  fclose(f);
  std::string resp = sh("curl -s -m 20 -A 'S40Launcher/1.0 (R36S dArkOS)' --data-binary @" + tmp + " https://retroachievements.org/dorequest.php");
  remove(tmp.c_str());
  if (resp.empty()) { err = tl("Không kết nối được (kiểm tra Wi-Fi)"); return false; }
  if (jsonStr(resp, "Success") != "true") {
    std::string e = jsonStr(resp, "Error");
    err = e.empty() ? tl("Sai tên hoặc mật khẩu") : e.substr(0, 60);
    return false;
  }
  std::string token = jsonStr(resp, "Token");
  if (token.empty()) { err = tl("Máy chủ không trả về token"); return false; }
  raSet({{"cheevos_enable", "true"}, {"cheevos_username", jsonStr(resp, "User").empty() ? user : jsonStr(resp, "User")},
         {"cheevos_token", token}, {"cheevos_password", ""}});
  S.set("raScore", jsonStr(resp, "Score"));
  return true;
}

static Item raToggle(const std::string& label, const std::string& key, const std::string& icon, const std::string& sub) {
  Item it; it.label = label; it.icon = icon; it.color = hex("f5a300"); it.sub = sub; it.okLabel = tl("Đổi");
  bool on = raGet(key) == "true";
  it.right = on ? tl("Bật") : tl("Tắt");
  it.ok = [key] { raSet({{key, raGet(key) == "true" ? "false" : "true"}}); };
  it.adj = [key](int) { raSet({{key, raGet(key) == "true" ? "false" : "true"}}); };
  return it;
}

Screen* RetroAchievementsPage() {
  return new ListScreen("RetroAchievements", [] {
    Items v;
    std::string user = raGet("cheevos_username");
    bool logged = !user.empty() && !raGet("cheevos_token").empty();
    {
      Item it; it.icon = "star"; it.color = hex("f5a300");
      if (logged) {
        it.label = tl("Tài khoản: ") + user;
        it.sub = S.get("raScore").empty() ? tl("Đã đăng nhập") : tl("Đã đăng nhập · ") + S.get("raScore") + tl(" điểm");
        it.right = tl("Đăng xuất"); it.okLabel = tl("Đăng xuất");
        it.ok = [] {
          confirmBox(tl("Đăng xuất RetroAchievements?"), [] {
            raSet({{"cheevos_enable", "false"}, {"cheevos_username", ""}, {"cheevos_token", ""}, {"cheevos_password", ""}});
            S.set("raScore", "");
          }, "star");
        };
      } else {
        it.label = tl("Đăng nhập"); it.sub = tl("Tài khoản retroachievements.org (cần Wi-Fi)"); it.right = "›"; it.okLabel = tl("Mở");
        it.ok = [] {
          push(OSK(tl("Tên đăng nhập RetroAchievements"), raGet("cheevos_username"), false, [](const std::string& u) {
            if (trim(u).empty()) return;
            std::string user = trim(u);
            push(OSK(tl("Mật khẩu"), "", true, [user](const std::string& p) {
              if (p.empty()) return;
              auto pass = std::make_shared<std::string>(p);
              runTask("RetroAchievements", {tl("Đăng nhập ") + user}, [user, pass](std::string& err) {
                bool ok = raLogin(user, *pass, err);
                pass->assign(pass->size(), '\0'); pass->clear();
                return ok;
              }, [] { toast(tl("Đã đăng nhập RetroAchievements"), "star"); });
            }));
          }));
        };
      }
      v.push_back(it);
    }
    if (!logged) return v;
    v.push_back(raToggle(tl("Bật thành tích"), "cheevos_enable", "check", tl("Mở khóa thành tích khi chơi game có hỗ trợ")));
    v.push_back(raToggle(tl("Chế độ Hardcore"), "cheevos_hardcore_mode_enable", "flash", tl("Gấp đôi điểm; tắt lưu/nạp trạng thái, tua nhanh, cheat")));
    v.push_back(raToggle(tl("Bảng xếp hạng"), "cheevos_leaderboards_enable", "list", tl("Gửi điểm số lên bảng xếp hạng")));
    v.push_back(raToggle(tl("Âm báo khi mở khóa"), "cheevos_unlock_sound_enable", "bell", ""));
    v.push_back(raToggle(tl("Ảnh huy hiệu"), "cheevos_badges_enable", "photo", tl("Tải ảnh huy hiệu (tốn mạng)")));
    v.push_back(raToggle(tl("Tự chụp màn hình khi mở khóa"), "cheevos_auto_screenshot", "photo", ""));
    v.push_back(raToggle(tl("Thông báo chi tiết"), "cheevos_verbose_enable", "info", tl("Hiện thông tin game và tiến độ khi bắt đầu chơi")));
    return v;
  }, "star");
}

}  // namespace s40
