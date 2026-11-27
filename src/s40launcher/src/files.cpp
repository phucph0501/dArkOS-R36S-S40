// File manager ("Quản lý tệp"): browse the card and the system, copy / cut / paste, rename, delete,
// new folder, show hidden files, extract zip / rar / 7z (password protected too) and compress to
// zip / 7z (optionally with a password), view text files, file info.
// Archives use the static 7-Zip of the bundled 7-Zip (/opt/s40/bin/7zzs, handles RAR5 too) or 7zz/7z on PATH.
#include <sys/stat.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "s40.h"

namespace fs = std::filesystem;

namespace s40 {

static std::string romsDir() { return romsBase(); }
static std::string q(const std::string& s) {  // single-quote for the shell
  std::string o = "'";
  for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
  return o + "'";
}
static std::string ext(const std::string& n) {
  size_t d = n.find_last_of('.');
  return d == std::string::npos || d == 0 ? "" : lower(n.substr(d + 1));
}
static std::string human(uintmax_t b) {
  char t[32];
  if (b >= 1073741824ull) snprintf(t, sizeof t, "%.1f GB", b / 1073741824.0);
  else if (b >= 1048576) snprintf(t, sizeof t, "%.1f MB", b / 1048576.0);
  else if (b >= 1024) snprintf(t, sizeof t, "%.0f KB", b / 1024.0);
  else snprintf(t, sizeof t, "%u B", (unsigned)b);
  return t;
}
static std::string sevenZip() {
  if (fileExists("/opt/s40/bin/7zzs")) return "/opt/s40/bin/7zzs";
  for (const char* b : {"7zz", "7z", "7za"})
    if (run(std::string("command -v ") + b + " >/dev/null 2>&1") == 0) return b;
  return "";
}
static bool isArchive(const std::string& e) { return e == "zip" || e == "rar" || e == "7z" || e == "tar" || e == "gz" || e == "tgz" || e == "xz" || e == "bz2"; }
static bool isText(const std::string& e) {
  for (const char* t : {"txt", "cfg", "conf", "ini", "log", "m3u", "m3u8", "json", "sh", "md", "xml", "csv", "srt", "nfo", "info", "py", "lua"})
    if (e == t) return true;
  return false;
}
static std::string iconFor(const std::string& e, bool dir) {
  if (dir) return "folder";
  if (isArchive(e)) return "download";
  if (e == "mp3" || e == "ogg" || e == "flac" || e == "wav" || e == "m4a" || e == "opus") return "music";
  if (e == "mp4" || e == "mkv" || e == "avi" || e == "webm" || e == "mov") return "video";
  if (e == "png" || e == "jpg" || e == "jpeg" || e == "gif" || e == "bmp" || e == "webp") return "photo";
  if (e == "jar" || e == "jad") return "game";
  if (isText(e)) return "note";
  return "list";
}
static const char* colorFor(const std::string& icon) {
  if (icon == "folder") return "f9a825";
  if (icon == "download") return "6d4c41";
  if (icon == "music") return "d81b60";
  if (icon == "video") return "8e24aa";
  if (icon == "photo") return "43a047";
  if (icon == "game") return "e53935";
  if (icon == "note") return "1e88e5";
  return "607d8b";
}
// "name" -> "name (2)" ... while it exists in dir (keeps the extension)
static std::string uniquePath(const std::string& dir, const std::string& name) {
  std::string p = dir + "/" + name;
  if (!fs::exists(fs::symlink_status(p))) return p;
  std::string e = ext(name), stem = e.empty() ? name : name.substr(0, name.size() - e.size() - 1), dot = e.empty() ? "" : name.substr(name.size() - e.size() - 1);
  for (int i = 2; i < 1000; i++) {
    p = dir + "/" + stem + " (" + std::to_string(i) + ")" + dot;
    if (!fs::exists(fs::symlink_status(p))) return p;
  }
  return p;
}

// ---------- text viewer ----------
struct TextView : Screen {
  std::string ttl;
  std::vector<std::string> lines;
  int top = 0;
  TextView(const std::string& path) {
    ttl = fs::path(path).filename().string();
    ic = "note";
    std::ifstream f(path, std::ios::binary);
    std::string l;
    size_t total = 0;
    while (std::getline(f, l) && total < 256 * 1024) {
      total += l.size();
      if (!l.empty() && l.back() == '\r') l.pop_back();
      for (char& c : l) if (c == '\t') c = ' ';
      lines.push_back(l);
    }
    if (lines.empty()) lines.push_back(tl("(Tệp trống)"));
  }
  std::string title() override { return ttl; }
  std::string info() override { return std::to_string(top + 1) + "/" + std::to_string(lines.size()); }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    int lh = 22, n = a.h / lh;
    for (int i = 0; i < n && top + i < (int)lines.size(); i++)
      G.text(lines[top + i], a.x + 10, a.y + 4 + i * lh, 15, t.txt, false, 0, a.w - 20);
  }
  Soft soft() override { return {"", "", tl("Quay lại")}; }
  void key(Key k) override {
    int n = (int)lines.size(), page = 14;
    if (k == Key::Up) top = std::max(0, top - 1);
    else if (k == Key::Down) top = std::min(std::max(0, n - 1), top + 1);
    else if (k == Key::Left || k == Key::L) top = std::max(0, top - page);
    else if (k == Key::Right || k == Key::R) top = std::min(std::max(0, n - 1), top + page);
    else if (k == Key::B) pop();
  }
};

// ---------- the file manager ----------
struct Clip { std::string path; bool cut = false; };
static Clip g_clip;

struct FileMgr : ListScreen {
  struct Ent { std::string name; bool dir = false, hidden = false, link = false; uintmax_t size = 0; time_t mtime = 0; };
  std::string cwd;
  std::vector<Ent> ents;
  Items cache;
  bool dirty = true;
  std::vector<std::pair<std::string, int>> history;  // parent dirs and the selection to restore

  explicit FileMgr(const std::string& start) : ListScreen(tl("Quản lý tệp"), nullptr, "folder") {
    cwd = start;
    build = [this] { if (dirty) reload(); return cache; };
    opts = [this](const Item&, int i) { return options(i); };
    empty = tl("Thư mục trống.\nNhấn Tùy chọn để dán hoặc tạo thư mục.");
  }
  std::string title() override {
    std::string n = fs::path(cwd).filename().string();
    return n.empty() ? "/" : n;
  }
  std::string info() override { return ents.empty() ? "" : std::to_string(sel + 1) + "/" + std::to_string(ents.size()); }

  void reload() {
    dirty = false;
    ents.clear();
    bool hidden = S.getb("fmHidden");
    std::error_code ec;
    for (fs::directory_iterator it(cwd, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
      Ent e;
      e.name = it->path().filename().string();
      e.hidden = !e.name.empty() && e.name[0] == '.';
      if (e.hidden && !hidden) continue;
      std::error_code e2;
      e.link = it->is_symlink(e2);
      e.dir = it->is_directory(e2);
      if (!e.dir) e.size = it->file_size(e2);
      struct stat st;
      if (stat(it->path().c_str(), &st) == 0) e.mtime = st.st_mtime;
      ents.push_back(e);
    }
    int mode = S.geti("fmSort", 0);
    std::sort(ents.begin(), ents.end(), [mode](const Ent& a, const Ent& b) {
      if (a.dir != b.dir) return a.dir;
      if (mode == 1 && a.size != b.size) return a.size > b.size;
      if (mode == 2 && a.mtime != b.mtime) return a.mtime > b.mtime;
      return lower(a.name) < lower(b.name);
    });
    cache.clear();
    for (auto& e : ents) {
      Item it;
      it.label = e.name;
      std::string x = ext(e.name);
      it.icon = iconFor(x, e.dir);
      it.color = hex(colorFor(it.icon));
      if (e.hidden) it.color.a = 150;
      char d[32];
      struct tm tm;
      localtime_r(&e.mtime, &tm);
      strftime(d, sizeof d, "%d/%m/%Y %H:%M", &tm);
      it.sub = (e.dir ? std::string(tl("Thư mục")) : human(e.size)) + " · " + d + (e.link ? tl(" · liên kết") : "");
      if (g_clip.path == cwd + "/" + e.name) it.right = g_clip.cut ? tl("Đã cắt") : tl("Đã chép");
      it.okLabel = e.dir ? tl("Mở") : isArchive(x) ? tl("Giải nén") : isText(x) ? tl("Xem") : tl("Tùy chọn");
      it.ok = [this] { activate(); };
      cache.push_back(it);
    }
    if (sel >= (int)cache.size()) sel = std::max(0, (int)cache.size() - 1);
  }
  std::string cur() const { return sel < (int)ents.size() ? cwd + "/" + ents[sel].name : ""; }
  void refresh() { dirty = true; }
  void enter(const std::string& dir) {
    history.push_back({cwd, sel});
    cwd = dir;
    sel = topRow = 0;
    refresh();
  }
  bool up() {
    if (!history.empty()) { cwd = history.back().first; sel = history.back().second; history.pop_back(); refresh(); return true; }
    std::string parent = fs::path(cwd).parent_path().string();
    if (cwd == "/" || parent.empty() || parent == cwd) return false;
    std::string from = fs::path(cwd).filename().string();
    cwd = parent;
    reload();
    for (size_t i = 0; i < ents.size(); i++) if (ents[i].name == from) sel = (int)i;
    return true;
  }
  void key(Key k) override {
    if (k == Key::B) { if (!up()) pop(); return; }
    if (k == Key::L || k == Key::R) {  // jump a page
      int n = (int)cache.size();
      if (n) sel = k == Key::L ? std::max(0, sel - vis) : std::min(n - 1, sel + vis);
      return;
    }
    ListScreen::key(k);
  }

  // OK on an entry
  void activate() {
    if (sel >= (int)ents.size()) return;
    const Ent& e = ents[sel];
    std::string p = cur(), x = ext(e.name);
    if (e.dir) { enter(p); return; }
    if (isArchive(x)) { openOptions(archiveOpts(p), e.name); return; }
    if (isText(x)) { push(new TextView(p)); return; }
    openOptions(options(sel), e.name);
  }

  std::vector<Opt> archiveOpts(const std::string& p) {
    std::string stem = fs::path(p).stem().string();
    if (ext(p) == "gz" && lower(fs::path(stem).extension().string()) == ".tar") stem = fs::path(stem).stem().string();
    return {{tl("Giải nén vào thư mục \"") + stem + "\"", "folder", [this, p, stem] { extract(p, uniquePath(cwd, stem), ""); }},
            {tl("Giải nén tại đây"), "download", [this, p] { extract(p, cwd, ""); }},
            {tl("Xem nội dung"), "list", [p] { listArchive(p); }}};
  }

  std::vector<Opt> options(int i) {
    std::vector<Opt> o;
    bool has = i >= 0 && i < (int)ents.size();
    std::string p = has ? cur() : "";
    if (has) {
      const Ent& e = ents[i];
      std::string x = ext(e.name);
      if (e.dir) o.push_back({tl("Mở"), "folder", [this, p] { enter(p); }});
      else if (isArchive(x)) for (auto& a : archiveOpts(p)) o.push_back(a);
      else if (isText(x)) o.push_back({tl("Xem nội dung"), "note", [p] { push(new TextView(p)); }});
      o.push_back({tl("Sao chép"), "plus", [this, p] { g_clip = {p, false}; refresh(); toast(tl("Đã chép, mở thư mục khác rồi Dán"), "check"); }});
      o.push_back({tl("Cắt (di chuyển)"), "next", [this, p] { g_clip = {p, true}; refresh(); toast(tl("Đã cắt, mở thư mục khác rồi Dán"), "check"); }});
    }
    if (!g_clip.path.empty())
      o.push_back({tl("Dán vào đây: ") + fs::path(g_clip.path).filename().string(), "download", [this] { paste(); }});
    if (has) {
      std::string name = ents[i].name;
      o.push_back({tl("Đổi tên"), "note", [this, p, name] {
                     push(OSK(tl("Tên mới"), name, false, [this, p](const std::string& nn) { renameTo(p, nn); }));
                   }});
      o.push_back({tl("Nén thành .zip"), "download", [this, p] { compress(p, "zip", ""); }});
      o.push_back({tl("Nén .7z có mật khẩu"), "lock", [this, p] {
                     push(OSK(tl("Mật khẩu cho tệp nén"), "", true, [this, p](const std::string& pw) { compress(p, "7z", pw); }));
                   }});
      o.push_back({tl("Xóa"), "trash", [this, p, name] {
                     confirmBox(tl("Xóa \"") + name + tl("\"?\nKhông thể hoàn tác."), [this, p] { removePath(p); });
                   }, {229, 57, 53}});
      o.push_back({tl("Thông tin"), "info", [this, p] { showInfo(p); }});
    }
    o.push_back({tl("Tạo thư mục mới"), "plus", [this] {
                   push(OSK(tl("Tên thư mục"), "Thu muc moi", false, [this](const std::string& n) { makeDir(n); }));
                 }});
    o.push_back({std::string(S.getb("fmHidden") ? tl("Ẩn") : tl("Hiện")) + tl(" tệp ẩn"), "key", [this] {
                   S.setb("fmHidden", !S.getb("fmHidden")); refresh();
                   toast(S.getb("fmHidden") ? tl("Đang hiện tệp ẩn") : tl("Đã ẩn tệp ẩn"), "check");
                 }});
    static const char* sorts[] = {tl("tên"), tl("kích thước"), tl("ngày sửa")};
    o.push_back({std::string(tl("Sắp xếp theo: ")) + sorts[S.geti("fmSort", 0) % 3], "sort", [this] {
                   openPicker(tl("Sắp xếp theo"), {tl("Tên"), tl("Kích thước (lớn trước)"), tl("Ngày sửa (mới trước)")}, S.geti("fmSort", 0),
                              [this](int s) { S.seti("fmSort", s); refresh(); });
                 }});
    o.push_back({tl("Đi tới…"), "folder", [this] { goTo(); }});
    return o;
  }

  void goTo() {
    std::vector<std::pair<std::string, std::string>> places = {{tl("Thẻ trò chơi (EASYROMS)"), romsDir()}};
    const char* home = getenv("HOME");
    places.push_back({tl("Thư mục người dùng"), home ? home : "/home/ark"});
    for (const char* u : {"/roms2", "/media/usb", "/mnt/usbdrive", "/media"})
      if (fs::is_directory(u)) places.push_back({std::string(tl("Ổ ngoài ")) + u, u});
    places.push_back({tl("Gốc hệ thống (/)"), "/"});
    std::vector<std::string> labels;
    for (auto& p : places) labels.push_back(p.first);
    openPicker(tl("Đi tới"), labels, -1, [this, places](int i) { history.clear(); cwd = places[i].second; sel = topRow = 0; refresh(); });
  }

  // ---- operations (heavy ones run as background tasks) ----
  void paste() {
    Clip c = g_clip;
    std::string name = fs::path(c.path).filename().string();
    if (!fs::exists(fs::symlink_status(c.path))) { toast(tl("Tệp gốc không còn"), "warn"); g_clip = {}; return; }
    std::string dst = uniquePath(cwd, name);
    std::string srcAbs = fs::weakly_canonical(c.path).string(), dstDir = fs::weakly_canonical(cwd).string();
    if (fs::is_directory(c.path) && (dstDir + "/").rfind(srcAbs + "/", 0) == 0) { toast(tl("Không thể chép thư mục vào chính nó"), "warn"); return; }
    runTask(c.cut ? tl("Di chuyển") : tl("Sao chép"), {name}, [c, dst](std::string& err) {
      std::error_code ec;
      if (c.cut) {
        fs::rename(c.path, dst, ec);
        if (!ec) return true;
        ec.clear();  // other filesystem: copy, then remove the source
      }
      fs::copy(c.path, dst, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
      if (ec) { err = ec.message(); return false; }
      if (c.cut) fs::remove_all(c.path, ec);
      return true;
    }, [this, c] { if (c.cut) g_clip = {}; refresh(); });
  }
  void renameTo(const std::string& p, const std::string& nn) {
    std::string n = trim(nn);
    if (n.empty() || n.find('/') != std::string::npos || n == "." || n == "..") { toast(tl("Tên không hợp lệ"), "warn"); return; }
    std::string dst = cwd + "/" + n;
    if (dst == p) return;
    if (fs::exists(fs::symlink_status(dst))) { toast(tl("Đã có tệp trùng tên"), "warn"); return; }
    std::error_code ec;
    fs::rename(p, dst, ec);
    if (ec) toast(tl("Không đổi tên được: ") + ec.message(), "warn");
    else { if (g_clip.path == p) g_clip.path = dst; toast(tl("Đã đổi tên"), "check"); }
    refresh();
  }
  void makeDir(const std::string& nn) {
    std::string n = trim(nn);
    if (n.empty() || n.find('/') != std::string::npos) { toast(tl("Tên không hợp lệ"), "warn"); return; }
    std::error_code ec;
    if (!fs::create_directory(cwd + "/" + n, ec)) toast(ec ? tl("Không tạo được: ") + ec.message() : tl("Đã có thư mục này"), "warn");
    else toast(tl("Đã tạo thư mục"), "check");
    refresh();
  }
  void removePath(const std::string& p) {
    std::string name = fs::path(p).filename().string();
    runTask(tl("Xóa"), {name}, [p](std::string& err) {
      std::error_code ec;
      fs::remove_all(p, ec);
      if (ec) { err = ec.message(); return false; }
      return true;
    }, [this, p] { if (g_clip.path == p) g_clip = {}; refresh(); });
  }
  void showInfo(const std::string& p) {
    struct stat st;
    if (lstat(p.c_str(), &st) != 0) return;
    bool dir = S_ISDIR(st.st_mode);
    std::string size = dir ? trim(sh("du -sh " + q(p) + " 2>/dev/null | cut -f1")) : human((uintmax_t)st.st_size);
    char d[32], perm[11] = "----------";
    struct tm tm;
    localtime_r(&st.st_mtime, &tm);
    strftime(d, sizeof d, "%d/%m/%Y %H:%M", &tm);
    const char* r = "rwxrwxrwx";
    if (dir) perm[0] = 'd'; else if (S_ISLNK(st.st_mode)) perm[0] = 'l';
    for (int i = 0; i < 9; i++) if (st.st_mode & (0400 >> i)) perm[i + 1] = r[i];
    std::string free = trim(sh("df -h " + q(dir ? p : cwd) + " 2>/dev/null | awk 'NR==2{print $4\" trống / \"$2}'"));
    infoBox(fs::path(p).filename().string(), {{tl("Loại"), dir ? tl("Thư mục") : (ext(p).empty() ? tl("Tệp") : tl("Tệp .") + ext(p))},
                                              {tl("Dung lượng"), size}, {tl("Sửa lần cuối"), d}, {tl("Quyền"), perm},
                                              {tl("Đường dẫn"), p}, {tl("Ổ chứa"), free}});
  }

  // ---- archives ----
  static void listArchive(const std::string& p) {
    std::string z = sevenZip();
    if (z.empty()) { toast(tl("Thiếu 7-Zip để đọc tệp nén"), "warn"); return; }
    std::string out = sh(z + " l -ba -p__s40_nopass__ " + q(p) + " 2>&1 | head -400");
    std::string tmp = "/tmp/s40-archive-list.txt";
    std::ofstream(tmp) << (out.empty() ? tl("(Không đọc được, có thể cần mật khẩu)") : out);
    auto* v = new TextView(tmp);
    v->ttl = fs::path(p).filename().string();
    push(v);
  }
  // encrypted? (content or header encryption) -> ask for the password first
  static bool needsPassword(const std::string& z, const std::string& p) {
    std::string out = sh(z + " l -slt -p__s40_nopass__ " + q(p) + " 2>&1");
    return out.find("Encrypted = +") != std::string::npos || out.find("Wrong password") != std::string::npos ||
           out.find("Can not open encrypted archive") != std::string::npos;
  }
  void extract(const std::string& p, const std::string& dst, const std::string& pw) {
    std::string z = sevenZip();
    if (z.empty()) { toast(tl("Thiếu 7-Zip để giải nén"), "warn"); return; }
    if (pw.empty() && needsPassword(z, p)) {
      push(OSK(tl("Mật khẩu giải nén"), "", true, [this, p, dst](const std::string& w) { if (!w.empty()) extract(p, dst, w); }));
      return;
    }
    std::string name = fs::path(p).filename().string();
    auto wrong = std::make_shared<bool>(false);
    runTask(tl("Giải nén"), {name}, [z, p, dst, pw, wrong](std::string& err) {
      std::string log = "/tmp/s40-7z.log";
      // -p always given so 7-Zip never waits for a password on stdin; it ignores it for plain archives
      std::string cmd = z + " x -y -bd -o" + q(dst) + " -p" + q(pw.empty() ? "__s40_nopass__" : pw) + " " + q(p) + " > " + log + " 2>&1";
      int rc = run(cmd);
      std::ifstream f(log);
      std::string out((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      if (out.find("Wrong password") != std::string::npos || out.find("Data Error in encrypted") != std::string::npos) {
        *wrong = true; err = tl("Sai mật khẩu"); return false;
      }
      if (rc > 1) {  // 1 = warnings (some files skipped)
        size_t e = out.find("ERROR");
        err = e != std::string::npos ? trim(out.substr(e, out.find('\n', e) - e)) : tl("7-Zip lỗi ") + std::to_string(rc);
        return false;
      }
      return true;
    }, [this, p, dst, wrong] {
      refresh();
      if (*wrong) push(OSK(tl("Sai mật khẩu, nhập lại"), "", true, [this, p, dst](const std::string& w) { if (!w.empty()) extract(p, dst, w); }));
    });
  }
  void compress(const std::string& p, const std::string& type, const std::string& pw) {
    std::string z = sevenZip();
    if (z.empty()) { toast(tl("Thiếu 7-Zip để nén"), "warn"); return; }
    std::string name = fs::path(p).filename().string();
    std::string out = uniquePath(cwd, name + "." + type);
    std::string dir = cwd;
    runTask(tl("Nén"), {fs::path(out).filename().string()}, [z, dir, name, out, type, pw](std::string& err) {
      std::string cmd = "cd " + q(dir) + " && " + z + " a -bd -t" + type + " " + q(out) + " " + q(name);
      if (!pw.empty()) cmd += " -p" + q(pw) + (type == "7z" ? " -mhe=on" : "");  // 7z: hide file names too
      int rc = run(cmd + " > /tmp/s40-7z.log 2>&1");
      if (rc > 1) { err = tl("7-Zip lỗi ") + std::to_string(rc); return false; }
      return true;
    }, [this] { refresh(); });
  }
};

Screen* FileManagerApp() {
  std::string start = romsDir();
  if (!fs::is_directory(start)) { const char* h = getenv("HOME"); start = h ? h : "/"; }
  return new FileMgr(start);
}

}  // namespace s40
