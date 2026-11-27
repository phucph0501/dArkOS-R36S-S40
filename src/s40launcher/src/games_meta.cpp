// Game browser with names, short descriptions and cover art.
//
// Metadata comes from <system dir>/gamelist.xml (EmulationStation format, also what Skyscraper /
// ScreenScraper produce), images from the paths it names or the usual folders (media/covers,
// images, boxart). "Tải ảnh bìa & mô tả" fills them in without any account:
//   - cover art: libretro-thumbnails (thumbnails.libretro.com), matched against the ROM file name
//   - arcade (short ROM names like sf2.zip): Arcade Database (adb.arcadeitalia.net) for the full
//     title, maker, year, genre and a title-screen image
//   - description: Wikipedia summary (Vietnamese first, then English)
// Results are written back to gamelist.xml and media/covers/, so they work offline afterwards and
// EmulationStation sees them too.
#include <SDL2/SDL_image.h>
#include <zlib.h>
#include <sys/stat.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#include "s40.h"

namespace s40 {

void addRecent(const std::string& key);
bool isFav(const std::string& key);
void toggleFav(const std::string& key);
std::string favLabel(const std::string& key);
std::string romTitle(const std::string& file);
Col sysColor(const std::string& sys);
Screen* J2meConfig(const std::string& rom);

namespace meta {

struct Meta { std::string name, desc, image, year, genre, players, dev; };
using Table = std::map<std::string, Meta>;  // rom file name -> meta
static std::map<std::string, Table> tables;  // system dir -> table
static std::mutex mu;

static std::string dirOf(const GameSys& s) { return s.path.back() == '/' ? s.path.substr(0, s.path.size() - 1) : s.path; }
static std::string stemOf(const std::string& f) { size_t d = f.find_last_of('.'); return d == std::string::npos ? f : f.substr(0, d); }

// ---------- XML (gamelist.xml) ----------
static std::string unesc(std::string s) {
  static const std::pair<const char*, const char*> R[] = {{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&#39;", "'"}, {"&amp;", "&"}};
  for (auto& r : R) for (size_t p; (p = s.find(r.first)) != std::string::npos;) s.replace(p, strlen(r.first), r.second);
  return s;
}
static std::string esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else o += c;
  }
  return o;
}
static std::string tag(const std::string& b, const std::string& t) {
  size_t a = b.find("<" + t + ">");
  if (a == std::string::npos) return "";
  a += t.size() + 2;
  size_t e = b.find("</" + t + ">", a);
  return e == std::string::npos ? "" : trim(unesc(b.substr(a, e - a)));
}
static std::string relFile(std::string p) {  // "./Contra (USA).zip" -> "Contra (USA).zip"
  if (p.rfind("./", 0) == 0) p = p.substr(2);
  size_t s = p.find_last_of('/');
  return s == std::string::npos ? p : p.substr(s + 1);
}
static Table& load(const std::string& dir) {
  std::lock_guard<std::mutex> l(mu);
  auto it = tables.find(dir);
  if (it != tables.end()) return it->second;
  Table& t = tables[dir];
  std::ifstream f(dir + "/gamelist.xml");
  std::string x((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  for (size_t p = 0; (p = x.find("<game", p)) != std::string::npos;) {
    size_t e = x.find("</game>", p);
    if (e == std::string::npos) break;
    std::string b = x.substr(p, e - p);
    p = e;
    std::string path = tag(b, "path");
    if (path.empty()) continue;
    Meta m{tag(b, "name"), tag(b, "desc"), tag(b, "image"), tag(b, "releasedate"), tag(b, "genre"), tag(b, "players"), tag(b, "developer")};
    if (m.image.empty()) m.image = tag(b, "thumbnail");
    if (m.year.size() >= 4) m.year = m.year.substr(0, 4);  // 19910101T000000
    t[relFile(path)] = m;
  }
  return t;
}
static void save(const std::string& dir) {
  Table t;
  { std::lock_guard<std::mutex> l(mu); t = tables[dir]; }
  std::ostringstream o;
  o << "<?xml version=\"1.0\"?>\n<gameList>\n";
  for (auto& kv : t) {
    const Meta& m = kv.second;
    o << "\t<game>\n\t\t<path>./" << esc(kv.first) << "</path>\n";
    if (!m.name.empty()) o << "\t\t<name>" << esc(m.name) << "</name>\n";
    if (!m.desc.empty()) o << "\t\t<desc>" << esc(m.desc) << "</desc>\n";
    if (!m.image.empty()) o << "\t\t<image>" << esc(m.image) << "</image>\n";
    if (!m.year.empty()) o << "\t\t<releasedate>" << esc(m.year) << "0101T000000</releasedate>\n";
    if (!m.dev.empty()) o << "\t\t<developer>" << esc(m.dev) << "</developer>\n";
    if (!m.genre.empty()) o << "\t\t<genre>" << esc(m.genre) << "</genre>\n";
    if (!m.players.empty()) o << "\t\t<players>" << esc(m.players) << "</players>\n";
    o << "\t</game>\n";
  }
  o << "</gameList>\n";
  std::ofstream(dir + "/gamelist.xml.tmp", std::ios::trunc) << o.str();
  rename((dir + "/gamelist.xml.tmp").c_str(), (dir + "/gamelist.xml").c_str());
}
Meta get(const GameSys& s, const std::string& rom) {
  Table& t = load(dirOf(s));
  std::lock_guard<std::mutex> l(mu);
  auto it = t.find(rom);
  return it == t.end() ? Meta{} : it->second;
}
std::string imagePath(const GameSys& s, const std::string& rom) {
  std::string d = dirOf(s), st = stemOf(rom);
  Meta m = get(s, rom);
  if (!m.image.empty()) {
    std::string p = m.image[0] == '/' ? m.image : d + "/" + (m.image.rfind("./", 0) == 0 ? m.image.substr(2) : m.image);
    if (fileExists(p)) return p;
  }
  for (const char* f : {"/media/covers/%s.png", "/media/covers/%s.jpg", "/images/%s-image.png", "/images/%s-image.jpg", "/images/%s.png",
                        "/images/%s.jpg", "/boxart/%s.png", "/media/images/%s.png", "/media/box2dfront/%s.png", "/media/screenshots/%s.png"}) {
    std::string p = d + f;
    p.replace(p.find("%s"), 2, st);
    if (fileExists(p)) return p;
  }
  return "";
}

// ---------- online sources ----------
static std::string q(const std::string& s) { std::string o = "'"; for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; } return o + "'"; }
static std::string urlEnc(const std::string& s) {
  std::string o;
  for (unsigned char c : s) { if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c; else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); o += b; } }
  return o;
}
static std::string fetch(const std::string& url) { return sh("curl -sL -m 25 -A 's40-launcher/1.0' " + q(url) + " 2>/dev/null"); }
static bool download(const std::string& url, const std::string& dst) {
  std::string tmp = dst + ".part";
  int rc = run("curl -sLf -m 40 -A 's40-launcher/1.0' -o " + q(tmp) + " " + q(url) + " 2>/dev/null");
  struct stat st;
  if (rc != 0 || stat(tmp.c_str(), &st) != 0 || st.st_size < 1024) { remove(tmp.c_str()); return false; }
  return rename(tmp.c_str(), dst.c_str()) == 0;
}
// "Street Fighter II (USA) [!]" -> "streetfighterii"
static std::string norm(const std::string& s) {
  std::string o;
  int depth = 0;
  for (unsigned char c : s) {
    if (c == '(' || c == '[') depth++;
    else if ((c == ')' || c == ']') && depth) depth--;
    else if (!depth && isalnum(c)) o += (char)tolower(c);
  }
  if (o.size() > 3 && o.compare(0, 3, "the") == 0) o = o.substr(3);
  return o;
}
static std::string clean(const std::string& s) {  // drop (region) [flags]
  std::string o;
  int depth = 0;
  for (char c : s) {
    if (c == '(' || c == '[') depth++;
    else if ((c == ')' || c == ']') && depth) depth--;
    else if (!depth) o += c;
  }
  return trim(o);
}
// JSON string value of "key" (first occurrence), with \" \\ \n \uXXXX handled
static std::string jstr(const std::string& j, const std::string& key, size_t from = 0) {
  size_t p = j.find("\"" + key + "\":", from);
  if (p == std::string::npos) return "";
  p += key.size() + 3;
  while (p < j.size() && j[p] == ' ') p++;
  if (p >= j.size()) return "";
  if (j[p] != '"') {  // number
    size_t e = j.find_first_of(",}", p);
    return trim(j.substr(p, e - p));
  }
  std::string o;
  for (size_t i = p + 1; i < j.size(); i++) {
    char c = j[i];
    if (c == '"') break;
    if (c != '\\') { o += c; continue; }
    char n = j[++i];
    if (n == 'n') o += ' ';
    else if (n == 'u' && i + 4 < j.size()) {
      unsigned cp = (unsigned)strtol(j.substr(i + 1, 4).c_str(), nullptr, 16);
      i += 4;
      if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < j.size() && j[i + 1] == '\\' && j[i + 2] == 'u') {
        unsigned lo = (unsigned)strtol(j.substr(i + 3, 4).c_str(), nullptr, 16);
        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        i += 6;
      }
      if (cp < 0x80) o += (char)cp;
      else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
      else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
      else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    } else o += n;
  }
  return o;
}

static const char* libretroSystem(const std::string& sys) {
  static const std::map<std::string, const char*> m = {
      {"nes", "Nintendo - Nintendo Entertainment System"}, {"famicom", "Nintendo - Nintendo Entertainment System"},
      {"snes", "Nintendo - Super Nintendo Entertainment System"}, {"sfc", "Nintendo - Super Nintendo Entertainment System"},
      {"gb", "Nintendo - Game Boy"}, {"gbc", "Nintendo - Game Boy Color"}, {"gba", "Nintendo - Game Boy Advance"},
      {"nds", "Nintendo - Nintendo DS"}, {"n64", "Nintendo - Nintendo 64"},
      {"mastersystem", "Sega - Master System - Mark III"}, {"gamegear", "Sega - Game Gear"},
      {"megadrive", "Sega - Mega Drive - Genesis"}, {"genesis", "Sega - Mega Drive - Genesis"},
      {"psx", "Sony - PlayStation"}, {"ppsspp", "Sony - PlayStation Portable"}, {"psp", "Sony - PlayStation Portable"},
      {"pcengine", "NEC - PC Engine - TurboGrafx 16"}};
  auto it = m.find(sys);
  return it == m.end() ? nullptr : it->second;
}
static bool isArcade(const std::string& sys) { return sys == "arcade" || sys == "neogeo" || sys == "cps1" || sys == "cps2" || sys == "cps3" || sys == "mame2003"; }

// libretro-thumbnails repository of a system: "Sega - Master System - Mark III" -> "Sega_-_Master_System_-_Mark_III"
static std::string thumbRepo(const std::string& lsys) { std::string r = lsys; for (char& c : r) if (c == ' ') c = '_'; return r; }
static std::string readBin(const std::string& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }

// all cover names of a libretro system, from the GitHub repository's Named_Boxarts tree (two small
// API calls: the top tree for the folder's id, then that folder), cached for a week
static std::vector<std::string> thumbIndex(const std::string& lsys) {
  const char* h = getenv("HOME");
  std::string cdir = std::string(h ? h : "/tmp") + "/.cache/s40thumbs";
  run("mkdir -p " + q(cdir));
  std::string cache = cdir + "/" + lsys + ".txt";
  struct stat st;
  std::vector<std::string> v;
  if (stat(cache.c_str(), &st) != 0 || st.st_size == 0 || time(nullptr) - st.st_mtime > 7 * 86400) {
    std::string api = "https://api.github.com/repos/libretro-thumbnails/" + thumbRepo(lsys) + "/git/trees/";
    std::string top = fetch(api + "master"), sha;
    size_t p = top.find("\"Named_Boxarts\"");
    if (p != std::string::npos) sha = jstr(top, "sha", p);
    if (sha.size() == 40) {
      std::string tmp = cache + ".json";
      run("curl -sL -m 90 -A 's40-launcher/1.0' -o " + q(tmp) + " " + q(api + sha));
      std::string js = readBin(tmp);
      remove(tmp.c_str());
      std::ofstream f(cache, std::ios::trunc);
      for (size_t i = 0; (i = js.find("\"path\":", i)) != std::string::npos; i += 7) {
        std::string n = jstr(js, "path", i);
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".png") == 0) f << n.substr(0, n.size() - 4) << "\n";
      }
    }
  }
  std::ifstream f(cache);
  for (std::string l; std::getline(f, l);) if (!l.empty()) v.push_back(l);
  return v;
}
// covers are stored at most 240 px wide (originals are ~500 KB at 512 px)
static bool shrinkCover(const std::string& src, const std::string& dst) {
  SDL_Surface* s = IMG_Load(src.c_str());
  if (!s) return false;
  bool ok;
  if (s->w <= 240) ok = rename(src.c_str(), dst.c_str()) == 0;
  else {
    int w = 240, hgt = s->h * 240 / s->w;
    SDL_Surface* c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_Surface* d = SDL_CreateRGBSurfaceWithFormat(0, w, hgt, 32, SDL_PIXELFORMAT_ARGB8888);
    ok = c && d && SDL_BlitScaled(c, nullptr, d, nullptr) == 0 && IMG_SavePNG(d, dst.c_str()) == 0;
    if (c) SDL_FreeSurface(c);
    if (d) SDL_FreeSurface(d);
    remove(src.c_str());
  }
  SDL_FreeSurface(s);
  return ok;
}
// Arcade Database answers with a zip holding one JSON file: unpack the first entry with zlib
static std::string unzipFirst(const std::string& z) {
  if (z.size() > 18 && (unsigned char)z[0] == 0x1f && (unsigned char)z[1] == 0x8b) {  // gzip (what ADB actually sends)
    std::string out;
    z_stream zs{};
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) return "";
    zs.next_in = (Bytef*)z.data();
    zs.avail_in = (uInt)z.size();
    char buf[16384];
    int rc;
    do {
      zs.next_out = (Bytef*)buf;
      zs.avail_out = sizeof buf;
      rc = inflate(&zs, Z_NO_FLUSH);
      out.append(buf, sizeof buf - zs.avail_out);
    } while (rc == Z_OK);
    inflateEnd(&zs);
    return out;
  }
  if (z.size() < 30 || z.compare(0, 4, "PK\x03\x04") != 0) return z;  // not a zip: plain JSON
  auto u16 = [&](size_t o) { return (unsigned)(unsigned char)z[o] | ((unsigned)(unsigned char)z[o + 1] << 8); };
  unsigned method = u16(8), nlen = u16(26), elen = u16(28);
  size_t off = 30 + nlen + elen;
  if (off >= z.size()) return "";
  if (method == 0) return z.substr(off);
  std::string out;
  z_stream zs{};
  if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return "";
  zs.next_in = (Bytef*)z.data() + off;
  zs.avail_in = (uInt)(z.size() - off);
  char buf[16384];
  int rc;
  do {
    zs.next_out = (Bytef*)buf;
    zs.avail_out = sizeof buf;
    rc = inflate(&zs, Z_NO_FLUSH);
    out.append(buf, sizeof buf - zs.avail_out);
  } while (rc == Z_OK);
  inflateEnd(&zs);
  return out;
}
// best cover name for a ROM: exact file name, else same title preferring USA / World / Europe / Japan
static std::string matchThumb(const std::vector<std::string>& idx, const std::string& rom) {
  std::string st = stemOf(rom), n = norm(st);
  for (auto& x : idx) if (x == st) return x;
  std::string best;
  int bestScore = -1;
  for (auto& x : idx) {
    if (norm(x) != n) continue;
    int s = x.find("(USA") != std::string::npos ? 4 : x.find("(World") != std::string::npos ? 3 : x.find("(Europe") != std::string::npos ? 2 : x.find("(Japan") != std::string::npos ? 1 : 0;
    if (x.find("Beta") != std::string::npos || x.find("Proto") != std::string::npos) s -= 5;
    if (s > bestScore) { bestScore = s; best = x; }
  }
  return best;
}
// libretro file names replace &*/:`<>?\| with _
static std::string thumbFile(std::string n) { for (char& c : n) if (strchr("&*/:`<>?\\|\"", c)) c = '_'; return n; }

// first sentences of the Wikipedia article about the game (vi, then en); checks the article title
// really is about that game
static std::string wikiSummary(const std::string& title) {
  std::string want = norm(title);
  if (want.size() < 3) return "";
  for (auto lang : {std::string("vi"), std::string("en")}) {
    std::string extra = lang == "vi" ? tl(" trò chơi điện tử") : " video game";
    std::string js = fetch("https://" + lang + ".wikipedia.org/w/api.php?action=query&list=search&srlimit=3&format=json&srsearch=" + urlEnc(title + extra));
    for (size_t p = 0; (p = js.find("\"title\":", p)) != std::string::npos; p += 8) {
      std::string t = jstr(js, "title", p);
      std::string nt = norm(t);
      if (nt.find(want) == std::string::npos && want.find(nt) == std::string::npos) continue;
      std::string sum = fetch("https://" + lang + ".wikipedia.org/api/rest_v1/page/summary/" + urlEnc(t));
      std::string ex = jstr(sum, "extract");
      if (ex.size() < 40) continue;
      // at most two sentences, ~260 bytes
      size_t cut = std::string::npos, dots = 0;
      for (size_t i = 0; i + 1 < ex.size(); i++)
        if (ex[i] == '.' && ex[i + 1] == ' ' && ++dots == 2) { cut = i + 1; break; }
      if (cut != std::string::npos) ex = ex.substr(0, cut);
      if (ex.size() > 300) { ex = ex.substr(0, 300); while (!ex.empty() && (ex.back() & 0xC0) == 0x80) ex.pop_back(); ex.pop_back(); ex += "…"; }
      return ex;
    }
  }
  return "";
}

// Vietnamese one-liner from what is known when no encyclopedia article was found:
// "Game đối kháng của Capcom (1991), 2 người chơi."
static std::string autoDesc(const Meta& m, const std::string& system) {
  if (m.genre.empty() && m.dev.empty()) return "";
  static const std::pair<const char*, const char*> G[] = {
      {"Fighter", tl("đối kháng")}, {"Beat", tl("đánh nhau màn hình ngang")}, {"Shooter", tl("bắn súng")}, {"Shoot", tl("bắn súng")},
      {"Platform", tl("đi cảnh")}, {"Racing", tl("đua xe")}, {"Driving", tl("đua xe")}, {"Sports", tl("thể thao")}, {"Puzzle", tl("giải đố")},
      {"Maze", tl("mê cung")}, {"Quiz", tl("đố vui")}, {"Casino", tl("sòng bài")}, {"Role", tl("nhập vai")}, {"Adventure", tl("phiêu lưu")}};
  std::string kind;
  for (auto& g : G) if (m.genre.find(g.first) != std::string::npos) { kind = g.second; break; }
  std::string d = "Game " + (kind.empty() ? system : kind);
  if (!m.dev.empty()) d += tl(" của ") + m.dev;
  if (!m.year.empty()) d += " (" + m.year + ")";
  if (!m.players.empty() && m.players != "0") d += ", " + m.players + tl(" người chơi");
  return d + ".";
}

// ---------- scraping screen ----------
struct Scraper : Screen {
  int si;
  std::vector<std::string> roms;
  std::thread th;
  std::atomic<int> done{0}, covers{0}, descs{0};
  std::atomic<bool> stop{false}, finished{false};
  std::string current, err;
  std::mutex cm;
  Scraper(int s, std::vector<std::string> list) : si(s), roms(std::move(list)) { ic = "photo"; live = true; noIdle = true; }
  ~Scraper() override { stop = true; if (th.joinable()) th.join(); }
  std::string title() override { return tl("Tải ảnh bìa & mô tả"); }
  void onEnter() override { th = std::thread([this] { work(); }); }
  void work() {
    GameSys s = systems()[si];
    std::string dir = dirOf(s);
    run("mkdir -p " + q(dir + "/media/covers"));
    const char* lsys = libretroSystem(s.name);
    std::vector<std::string> idx;
    if (lsys) { { std::lock_guard<std::mutex> l(cm); current = tl("Tải danh mục ảnh bìa…"); } idx = thumbIndex(lsys); }
    bool arcade = isArcade(s.name);
    if (!lsys && !arcade) { std::lock_guard<std::mutex> l(cm); err = tl("Hệ máy này chưa có nguồn ảnh bìa"); finished = true; return; }
    if (lsys && idx.empty() && !arcade) { std::lock_guard<std::mutex> l(cm); err = tl("Không tải được danh mục (mạng?)"); finished = true; return; }
    for (auto& rom : roms) {
      if (stop) break;
      { std::lock_guard<std::mutex> l(cm); current = romTitle(rom); }
      Meta m = get(s, rom);
      std::string cover = dir + "/media/covers/" + stemOf(rom) + ".png", title;
      if (arcade) {
        std::string zf = dir + "/media/covers/.adb.tmp";
        run("curl -sL -m 25 -A 's40-launcher/1.0' -o " + q(zf) + " " + q("http://adb.arcadeitalia.net/service_scraper.php?ajax=query_mame&use_parent=1&game_name=" + urlEnc(stemOf(rom))));
        std::string js = unzipFirst(readBin(zf));
        remove(zf.c_str());
        std::string full = jstr(js, "title");
        if (!full.empty()) {
          title = jstr(js, "short_title");
          if (title.empty()) title = clean(full);
          size_t alt = title.find(" / ");  // "The King of Fighters '98 - The Slugfest / King of Fighters '98 - ..."
          if (alt != std::string::npos) title = trim(title.substr(0, alt));
          if (m.name.empty()) m.name = title;
          if (m.year.empty()) m.year = jstr(js, "year");
          if (m.dev.empty()) m.dev = jstr(js, "manufacturer");
          if (m.genre.empty()) m.genre = jstr(js, "genre");
          if (m.players.empty()) m.players = jstr(js, "players");
          if (!fileExists(cover)) {
            std::string img = jstr(js, "url_image_title");
            if (img.empty()) img = jstr(js, "url_image_ingame");
            if (!img.empty() && download(img, cover + ".orig") && shrinkCover(cover + ".orig", cover)) covers++;
          }
        }
      } else {
        std::string name = matchThumb(idx, rom);
        title = clean(name.empty() ? stemOf(rom) : name);
        if (m.name.empty() && !name.empty()) m.name = title;
        if (!name.empty() && !fileExists(cover) &&
            download("https://raw.githubusercontent.com/libretro-thumbnails/" + thumbRepo(lsys) + "/master/Named_Boxarts/" + urlEnc(thumbFile(name)) + ".png",
                     cover + ".orig") &&
            shrinkCover(cover + ".orig", cover))
          covers++;
      }
      if (fileExists(cover)) m.image = "./media/covers/" + stemOf(rom) + ".png";
      if (m.desc.empty() && !title.empty()) {
        m.desc = wikiSummary(title);
        // second try with the main title only: "Street Fighter II: The World Warrior" -> "Street Fighter II"
        size_t cut = title.find_first_of(":-");
        if (m.desc.empty() && cut != std::string::npos && cut > 3) m.desc = wikiSummary(trim(title.substr(0, cut)));
        if (m.desc.empty()) m.desc = autoDesc(m, s.fullname);
        if (!m.desc.empty()) descs++;
      }
      { std::lock_guard<std::mutex> l(mu); tables[dir][rom] = m; }
      done++;
      if (done % 10 == 0) save(dir);
    }
    save(dir);
    finished = true;
  }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    int x = a.x + 28, y = a.y + 18, w = a.w - 56, n = (int)roms.size();
    G.text(finished ? (stop ? tl("Đã dừng") : tl("Đã xong")) : tl("Đang tải…"), x, y, 20, t.txt, true);
    y += 34;
    G.round(x, y, w, 12, {0, 0, 0, 40}, 6);
    if (n) G.grad(x, y, std::max(12, w * done / n), 12, t.hl1, t.hl2, 6);
    y += 24;
    std::string cur, e;
    { std::lock_guard<std::mutex> l(mu); }
    { std::lock_guard<std::mutex> l(cm); cur = current; e = err; }
    G.text(std::to_string(done) + " / " + std::to_string(n) + tl(" trò chơi"), x, y, 16, t.txt, true);
    y += 28;
    G.text(tl("Ảnh bìa mới: ") + std::to_string(covers) + tl(" · Mô tả mới: ") + std::to_string(descs), x, y, 15, t.sub);
    y += 26;
    if (!finished) G.text("› " + cur, x, y, 15, t.sub, false, 0, w);
    if (!e.empty()) G.text("✗ " + e, x, y + 30, 16, hex("c62828"), true, 0, w);
    G.text(tl("Nguồn: libretro-thumbnails (GitHub), Arcade Database, Wikipedia"), x, a.y + a.h - 28, 13, t.sub, false, 0, w);
  }
  Soft soft() override { return finished ? Soft{"", "OK", tl("Quay lại")} : Soft{"", "", tl("Dừng")}; }
  void key(Key k) override {
    if (!finished) { if (k == Key::B) stop = true; return; }
    if (k == Key::OK || k == Key::B) pop();
  }
};

// ---------- the game browser ----------
static std::vector<std::string> wrap(const std::string& s, int size, int maxw, int maxLines) {
  std::vector<std::string> lines;
  std::string line;
  std::stringstream ss(s);
  for (std::string w; ss >> w;) {
    std::string t = line.empty() ? w : line + " " + w;
    if (G.textW(t, size, false) <= maxw) { line = t; continue; }
    if (!line.empty()) lines.push_back(line);
    line = w;
    if ((int)lines.size() == maxLines) break;
  }
  if ((int)lines.size() < maxLines && !line.empty()) lines.push_back(line);
  if ((int)lines.size() == maxLines && ss.rdbuf()->in_avail() > 0) lines.back() += "…";
  return lines;
}

struct Browser : Screen {
  int si, sel = 0, top = 0;
  std::vector<int> order;
  std::map<std::string, SDL_Texture*> tex;
  SDL_Renderer* texRen = nullptr;
  explicit Browser(int s) : si(s) { ic = "game"; refresh(); }
  ~Browser() override { if (texRen == G.ren) for (auto& kv : tex) if (kv.second) SDL_DestroyTexture(kv.second); }
  GameSys& sys() { return systems()[si]; }
  void refresh() {
    auto& s = sys();
    order.resize(s.roms.size());
    for (int i = 0; i < (int)order.size(); i++) order[i] = i;
    std::vector<std::string> names(s.roms.size());
    for (int i = 0; i < (int)names.size(); i++) names[i] = lower(displayName(i));
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return names[a] < names[b]; });
    if (S.getb("favFirst"))
      std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return isFav(s.name + "|" + s.roms[a]) > isFav(s.name + "|" + s.roms[b]); });
    if (sel >= (int)order.size()) sel = std::max(0, (int)order.size() - 1);
  }
  std::string displayName(int i) { Meta m = get(sys(), sys().roms[i]); return m.name.empty() ? romTitle(sys().roms[i]) : m.name; }
  std::string title() override { return sys().fullname.empty() ? sys().name : sys().fullname; }
  std::string info() override { return order.empty() ? "" : std::to_string(sel + 1) + "/" + std::to_string(order.size()); }
  SDL_Texture* image(const std::string& p) {
    if (texRen != G.ren) { tex.clear(); texRen = G.ren; }  // the renderer is recreated after a game ran
    auto it = tex.find(p);
    if (it != tex.end()) return it->second;
    if (tex.size() > 24) { for (auto& kv : tex) if (kv.second) SDL_DestroyTexture(kv.second); tex.clear(); }
    SDL_Texture* t = p.empty() ? nullptr : IMG_LoadTexture(G.ren, p.c_str());
    tex[p] = t;
    return t;
  }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    auto& s = sys();
    if (order.empty()) { G.text(tl("Chưa có trò chơi"), a.x + a.w / 2, a.y + a.h / 2, 17, t.sub, false, 1); return; }
    // list (left)
    int lw = a.w * 47 / 100, rh = 34, vis = a.h / rh;
    if (sel < top) top = sel;
    if (sel >= top + vis) top = sel - vis + 1;
    for (int r = 0; r < vis && top + r < (int)order.size(); r++) {
      int i = order[top + r], y = a.y + r * rh;
      bool on = top + r == sel;
      std::string key = s.name + "|" + s.roms[i];
      if (on) G.grad(a.x + 4, y + 1, lw - 8, rh - 2, t.hl1, t.hl2, 8);
      else G.rect(a.x + 12, y + rh - 1, lw - 24, 1, {0, 0, 0, 14});
      int x = a.x + 14;
      if (isFav(key)) { G.text("★", x, y + 6, 15, on ? Col{255, 255, 255} : hex("f5a300"), true); x += 18; }
      G.text(displayName(i), x, y + 6, 16, on ? Col{255, 255, 255} : t.txt, on, 0, a.x + lw - x - 10);
    }
    // detail panel (right)
    int i = order[sel], px = a.x + lw + 6, pw = a.w - lw - 14, y = a.y + 6;
    G.round(px, a.y + 4, pw, a.h - 8, {0, 0, 0, 16}, 10);
    Meta m = get(s, s.roms[i]);
    SDL_Texture* img = image(imagePath(s, s.roms[i]));
    int boxH = 176;
    if (img) {
      int w, h;
      SDL_QueryTexture(img, nullptr, nullptr, &w, &h);
      float sc = std::min((pw - 16) / (float)w, boxH / (float)h);
      SDL_Rect d{px + (pw - (int)(w * sc)) / 2, y + 4 + (boxH - (int)(h * sc)) / 2, (int)(w * sc), (int)(h * sc)};
      SDL_RenderCopy(G.ren, img, nullptr, &d);
    } else {
      Col c = sysColor(s.name);
      G.grad(px + pw / 2 - 52, y + 30, 104, 104, {(Uint8)std::min(255, c.r + 40), (Uint8)std::min(255, c.g + 40), (Uint8)std::min(255, c.b + 40)}, c, 22);
      G.icon("game", px + pw / 2 - 30, y + 52, 60, {255, 255, 255});
    }
    y += boxH + 12;
    auto nl = wrap(displayName(i), 17, pw - 20, 2);
    for (auto& l : nl) { G.text(l, px + 10, y, 17, t.txt, true); y += 22; }
    std::string info;
    for (auto& part : {m.year, m.genre, m.players.empty() ? std::string() : m.players + tl(" người"), m.dev})
      if (!part.empty()) info += (info.empty() ? "" : " · ") + part;
    if (!info.empty()) { G.text(info, px + 10, y, 13, t.a2, true, 0, pw - 20); y += 20; }
    if (!m.desc.empty()) {
      int lines = std::max(1, (a.y + a.h - 10 - y) / 19);
      for (auto& l : wrap(m.desc, 14, pw - 20, lines)) { G.text(l, px + 10, y, 14, t.sub); y += 19; }
    } else if (!img) {
      for (auto& l : wrap(tl("Chưa có ảnh bìa và mô tả. Nhấn Tùy chọn › Tải ảnh bìa & mô tả."), 14, pw - 20, 3)) { G.text(l, px + 10, y, 14, t.sub); y += 19; }
    }
  }
  Soft soft() override { return {tl("Tùy chọn"), order.empty() ? "" : tl("Chơi"), tl("Quay lại")}; }
  void play() {
    auto& s = sys();
    int i = order[sel];
    addRecent(s.name + "|" + s.roms[i]);
    launchGame(s, s.roms[i]);
  }
  void key(Key k) override {
    int n = (int)order.size();
    if (k == Key::B) { pop(); return; }
    if (k == Key::A) { options(); return; }
    if (!n) return;
    int page = 8;
    if (k == Key::Up) sel = (sel - 1 + n) % n;
    else if (k == Key::Down) sel = (sel + 1) % n;
    else if (k == Key::Left) sel = std::max(0, sel - page);
    else if (k == Key::Right) sel = std::min(n - 1, sel + page);
    else if (k == Key::OK) play();
  }
  void options() {
    auto& s = sys();
    std::vector<Opt> o;
    if (!order.empty()) {
      int i = order[sel];
      std::string rom = s.roms[i], key = s.name + "|" + rom;
      o.push_back({tl("Chơi"), "play", [this] { play(); }});
      o.push_back({favLabel(key), "star", [this, key] { toggleFav(key); refresh(); }, hex("f5a300")});
      if (s.name == "j2me") o.push_back({tl("Cấu hình Java (lõi, độ phân giải…)"), "chip", [rom] { push(J2meConfig(rom)); }, hex("e65100")});
      else o.push_back({tl("Thiết lập riêng (lõi, hình ảnh…)"), "chip", [this, rom] { push(GameOptionsScreen(sys(), rom)); }, hex("e65100")});
      o.push_back({tl("Tải ảnh bìa & mô tả cho game này"), "photo", [this, rom] { push(new Scraper(si, {rom})); }, hex("43a047")});
      o.push_back({tl("Thông tin"), "info", [this, rom] {
                     auto& s2 = sys();
                     auto ec = gameCore(s2, rom);
                     infoBox(displayName(order[sel]), {{tl("Hệ máy"), s2.fullname}, {tl("Tệp"), rom}, {tl("Trình giả lập"), ec.second.empty() ? ec.first : ec.second}});
                   }});
    }
    std::vector<std::string> missing;
    for (auto& r : s.roms) if (imagePath(s, r).empty() || get(s, r).desc.empty()) missing.push_back(r);
    o.push_back({tl("Tải ảnh bìa & mô tả cả hệ (") + std::to_string(missing.size()) + tl(" game thiếu)"), "download",
                 [this, missing] { if (missing.empty()) toast(tl("Đã đủ ảnh bìa và mô tả"), "check"); else push(new Scraper(si, missing)); }, hex("1e88e5")});
    openOptions(o, title());
  }
  void onEnter() override { refresh(); }
};

}  // namespace meta

Screen* GameBrowser(int si) { return new meta::Browser(si); }

}  // namespace s40
