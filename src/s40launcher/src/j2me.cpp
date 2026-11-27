// J2ME game runner (C++ port of the J2ME-Launcher run-game.sh / autoprofile.py / watcher.py).
//   s40 --j2me <game.jar>          run a game (used by es_systems.cfg and by the launcher)
//   s40 --j2me-dry <game.jar>      print the profile and the Java command, do not run
// Cores (only the cores are shipped, in /opt/j2me):
//   jlr   J2ME Loader R36S/ArkOS port (jlr/jlr.jar + jlr/lib/libjlr.so) - GPU 3D, default
//   plus  FreeJ2ME-Plus + AnbuPlus SDL frontend (engine/freej2me-plus-sdl.jar + engine/sdl_interface) - CPU 3D
// Per-game settings: <J2ME_DIR>/cfg/<game>.cfg (written by the launcher's "Cấu hình Java" screen).
#include <zlib.h>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <glob.h>
#include <map>
#include <regex>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <linux/joystick.h>

namespace s40 {
std::string trim(const std::string& s);
std::string sh(const std::string& cmd);

namespace j2me {

static std::string J2ME_DIR() { const char* d = getenv("S40_J2ME_DIR"); return d ? d : "/opt/j2me"; }

// ---------------- minimal ZIP reader (central directory + inflate) ----------------
struct ZipEntry { std::string name; uint32_t off, csize, usize; uint16_t method; };
static uint16_t u16(const unsigned char* p) { return p[0] | p[1] << 8; }
static uint32_t u32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static bool readZip(const std::string& path, std::vector<unsigned char>& buf, std::vector<ZipEntry>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  buf.assign(std::istreambuf_iterator<char>(f), {});
  if (buf.size() < 22) return false;
  size_t eocd = std::string::npos;
  for (size_t i = buf.size() - 22; i + 1 > 0 && buf.size() - i < 70000; i--) {
    if (u32(&buf[i]) == 0x06054b50) { eocd = i; break; }
    if (i == 0) break;
  }
  if (eocd == std::string::npos) return false;
  uint16_t n = u16(&buf[eocd + 10]);
  size_t p = u32(&buf[eocd + 16]);
  for (int i = 0; i < n && p + 46 <= buf.size(); i++) {
    if (u32(&buf[p]) != 0x02014b50) break;
    ZipEntry e;
    e.method = u16(&buf[p + 10]);
    e.csize = u32(&buf[p + 20]);
    e.usize = u32(&buf[p + 24]);
    uint16_t nl = u16(&buf[p + 28]), xl = u16(&buf[p + 30]), cl = u16(&buf[p + 32]);
    e.off = u32(&buf[p + 42]);
    e.name.assign((const char*)&buf[p + 46], nl);
    out.push_back(e);
    p += 46 + nl + xl + cl;
  }
  return true;
}
static bool zipData(const std::vector<unsigned char>& buf, const ZipEntry& e, std::string& data) {
  size_t lh = e.off;
  if (lh + 30 > buf.size() || u32(&buf[lh]) != 0x04034b50) return false;
  size_t start = lh + 30 + u16(&buf[lh + 26]) + u16(&buf[lh + 28]);
  if (start + e.csize > buf.size() || e.usize > (64u << 20)) return false;
  if (e.method == 0) { data.assign((const char*)&buf[start], e.csize); return true; }
  if (e.method != 8) return false;
  data.resize(e.usize);
  z_stream zs{};
  if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
  zs.next_in = (Bytef*)&buf[start]; zs.avail_in = e.csize;
  zs.next_out = (Bytef*)&data[0]; zs.avail_out = e.usize;
  int r = inflate(&zs, Z_FINISH);
  inflateEnd(&zs);
  return r == Z_STREAM_END || zs.total_out == e.usize;
}

// ---------------- auto profile (port of autoprofile.py) ----------------
struct Profile { std::string res = "auto", fast3d = "off", phone = "Standard", note; int fps = 30, jmem = 192; };

static std::string pickRes(const std::string& w, const std::string& h) {
  int a = atoi(w.c_str()), b = atoi(h.c_str());
  if (a >= 96 && a <= 320 && b >= 64 && b <= 320) return std::to_string(a) + "x" + std::to_string(b);
  return "";
}
static Profile scanJar(const std::string& jar) {
  Profile pr;
  static const char* PAT[] = {"javax/microedition/m3g", "com/mascotcapsule", "com/siemens/mp", "com/motorola", "com/nttdocomo", "com/kddi", "org/kddi", "com/samsung", "com/nokia/mid"};
  const int NP = 9;
  bool found[NP] = {};
  std::string mf;
  std::vector<unsigned char> buf;
  std::vector<ZipEntry> ents;
  if (!readZip(jar, buf, ents)) pr.note = "khong doc duoc jar";
  for (auto& e : ents) {
    std::string up = e.name; for (auto& c : up) c = (char)toupper((unsigned char)c);
    if (up == "META-INF/MANIFEST.MF") zipData(buf, e, mf);
    bool cls = e.name.size() > 6 && e.name.compare(e.name.size() - 6, 6, ".class") == 0;
    if (!cls || e.name.rfind("javax/", 0) == 0 || e.name.rfind("com/mascotcapsule/", 0) == 0 || e.name.rfind("org/recompile/", 0) == 0) continue;
    std::string d;
    if (!zipData(buf, e, d)) continue;
    int all = 0;
    for (int i = 0; i < NP; i++) { if (!found[i] && d.find(PAT[i]) != std::string::npos) found[i] = true; all += found[i]; }
    if (all == NP) break;
  }
  if (found[0] || found[1]) { pr.fast3d = "on"; pr.fps = 25; pr.jmem = 256; pr.note += " 3D"; }
  if (found[2]) pr.phone = "Siemens";
  else if (found[3]) pr.phone = "Motorola";
  else if (found[5] || found[6]) pr.phone = "KDDI";
  std::string base = jar.substr(jar.find_last_of('/') + 1);
  base = base.substr(0, base.find_last_of('.'));
  std::smatch m;
  if (std::regex_search(base, m, std::regex("(\\d{2,3})x(\\d{2,3})"))) { std::string r = pickRes(m[1], m[2]); if (!r.empty()) pr.res = r; }
  if (pr.res == "auto" && !mf.empty() &&
      std::regex_search(mf, m, std::regex("(?:Original-)?(?:Display|Screen)-Size[^:]*:\\s*(\\d+)\\s*[x, ]\\s*(\\d+)", std::regex::icase))) {
    std::string r = pickRes(m[1], m[2]); if (!r.empty()) pr.res = r;
  }
  return pr;
}
// cached by name + size, like autoprofile.sh
static Profile autoProfile(const std::string& jar) {
  struct stat st{};
  stat(jar.c_str(), &st);
  std::string base = jar.substr(jar.find_last_of('/') + 1);
  std::string cache = J2ME_DIR() + "/auto/" + base + "." + std::to_string((long long)st.st_size) + ".prof";
  Profile pr;
  std::ifstream in(cache);
  if (in) {
    std::string k, v;
    while (std::getline(in, k, '=') && std::getline(in, v)) {
      if (k == "res") pr.res = v; else if (k == "fast3d") pr.fast3d = v; else if (k == "phone") pr.phone = v;
      else if (k == "fps") pr.fps = atoi(v.c_str()); else if (k == "jmem") pr.jmem = atoi(v.c_str());
    }
    return pr;
  }
  pr = scanJar(jar);
  mkdir((J2ME_DIR() + "/auto").c_str(), 0755);
  std::ofstream o(cache);
  o << "res=" << pr.res << "\nfps=" << pr.fps << "\nfast3d=" << pr.fast3d << "\njmem=" << pr.jmem << "\nphone=" << pr.phone << "\n";
  return pr;
}

// ---------------- settings ----------------
struct Settings { std::string res = "auto", scale = "fit", filter = "sharp", rotate = "0", core = "jlr", fps = "30", fast3d = "off", phone = "Standard"; int jmem = 0; int w = 240, h = 320; };
static std::map<std::string, std::string> readCfg(const std::string& p) {
  std::map<std::string, std::string> m;
  std::ifstream f(p);
  std::string l;
  while (std::getline(f, l)) { auto e = l.find('='); if (e != std::string::npos && l[0] != '#') m[trim(l.substr(0, e))] = trim(l.substr(e + 1)); }
  return m;
}
static Settings prepare(const std::string& rom) {
  Settings s;
  std::string base = rom.substr(rom.find_last_of('/') + 1);
  std::string stem = base.substr(0, base.find_last_of('.'));
  Profile pr = autoProfile(rom);
  s.fps = std::to_string(pr.fps); s.fast3d = pr.fast3d; s.jmem = pr.jmem; s.phone = pr.phone; s.res = pr.res;
  auto c = readCfg(J2ME_DIR() + "/cfg/" + stem + ".cfg");
  auto set = [&](const char* k, std::string& dst, bool skipAuto) { auto it = c.find(k); if (it != c.end() && !it->second.empty() && !(skipAuto && it->second == "auto")) dst = it->second; };
  set("res", s.res, true); set("scale", s.scale, false); set("filter", s.filter, false); set("rotate", s.rotate, false);
  set("core", s.core, false); set("fps", s.fps, true); set("fast3d", s.fast3d, true); set("phone", s.phone, true);
  if (s.scale == "stretch") s.scale = "fit";
  std::smatch m;
  if (s.res == "auto") {
    if (std::regex_search(stem, m, std::regex("(\\d{3})x(\\d{3})"))) {
      int a = atoi(m[1].str().c_str()), b = atoi(m[2].str().c_str());
      if (a >= 120 && a <= 320 && b >= 120 && b <= 320) { s.w = a; s.h = b; }
    }
  } else if (s.res.find('x') != std::string::npos) {
    s.w = atoi(s.res.c_str()); s.h = atoi(s.res.substr(s.res.find('x') + 1).c_str());
  }
  return s;
}

// dArkOS exposes the R36S pad as "GO-Super Gamepad": D-pad 8-11, SELECT 12, START 13, L3 14, R3 15, FN 16
static bool goSuper() { return sh("cat /sys/class/input/js*/device/name 2>/dev/null").find("GO-Super") != std::string::npos; }

static std::string writePadmap() {
  std::string p = J2ME_DIR() + "/pad.map";
  std::ofstream o(p);
  if (goSuper())
    o << "1 13\n0 48\n2 8\n3 53\n4 49\n5 51\n6 101\n7 114\n13 113\n12 119\n14 55\n15 57\n8 1073741906\n9 1073741905\n10 1073741904\n11 1073741903\n";
  else
    o << "1 13\n0 48\n2 8\n3 53\n4 49\n5 51\n6 101\n7 114\n9 113\n8 119\n11 55\n12 57\n13 1073741906\n14 1073741905\n15 1073741904\n16 1073741903\n";
  return p;
}
static void writeJlrProps(const std::string& rom, const Settings& s) {
  std::string stem = rom.substr(rom.find_last_of('/') + 1);
  stem = stem.substr(0, stem.find_last_of('.'));
  std::string dir = J2ME_DIR() + "/jlr/config/games";
  sh("mkdir -p '" + dir + "'");
  std::ofstream o(dir + "/" + stem + ".properties");
  int w = 0, h = 0;
  if (s.res != "auto") { w = s.w; h = s.h; }
  int fl = (!s.fps.empty() && isdigit((unsigned char)s.fps[0])) ? atoi(s.fps.c_str()) : 0;
  std::string lay = s.phone == "Siemens" ? "siemens" : (s.phone.rfind("Moto", 0) == 0 || s.phone == "Motorola") ? "motorola" : "nokia";
  o << "screen.width=" << w << "\nscreen.height=" << h << "\ndisplay.rotation=" << s.rotate << "\ndisplay.scale=" << s.scale
    << "\ndisplay.filter=" << s.filter << "\nfps.limit=" << fl << "\ninput.layout=" << lay << "\nsound.enabled=true\n";
  // R36S buttons -> phone keys; FN/GUIDE opens jlr's own in-game menu (works on KMS/DRM, no compositor needed)
  static const char* MAP[][2] = {{"Y", "NUM5"}, {"A", "FIRE"}, {"X", "CLEAR"}, {"B", "NUM0"}, {"L1", "NUM1"}, {"R1", "NUM3"}, {"L2", "STAR"}, {"R2", "POUND"},
                                 {"START", "SOFT_LEFT"}, {"SELECT", "SOFT_RIGHT"}, {"L3", "NUM7"}, {"R3", "NUM9"}, {"UP", "UP"}, {"DOWN", "DOWN"},
                                 {"LEFT", "LEFT"}, {"RIGHT", "RIGHT"}, {"GUIDE", "MENU"}};
  for (auto& m : MAP) o << "input.map." << m[0] << "=" << m[1] << "\n";
}

static std::vector<std::string> buildCommand(const std::string& rom, const Settings& s, std::string& cwd, std::vector<std::string>& env) {
  std::string java = "/opt/j2me-jre/bin/java";
  if (access(java.c_str(), X_OK) != 0) java = trim(sh("command -v java"));
  std::string D = J2ME_DIR();
  std::vector<std::string> a;
  std::string gamesDir = rom.substr(0, rom.find_last_of('/'));
  if (s.core == "plus" && access((D + "/engine/freej2me-plus-sdl.jar").c_str(), R_OK) == 0) {
    std::string E = D + "/engine";
    cwd = E;
    env.push_back("HOME=" + E + "/home");
    std::string fil = s.filter == "sharp" ? "nearest" : s.filter;
    a = {java, "-Djava.awt.headless=true", "-Dfile.encoding=ISO_8859_1", "-Xms64m", "-Xmx" + std::to_string(s.jmem ? s.jmem : 192) + "m",
         "-XX:+UseSerialGC", "-XX:-UsePerfData", "-XX:+AutoCreateSharedArchive", "-XX:SharedArchiveFile=" + E + "/cds.jsa",
         "-Danbu.interface=" + E + "/sdl_interface", "-Dfreej2me.data=" + E + "/data", "-Dfreej2me.fps=" + std::string(s.fps == "native" ? "0" : s.fps),
         "-Dfreej2me.phone=" + s.phone, "-Dfreej2me.fast3d=" + s.fast3d, "-jar", E + "/freej2me-plus-sdl.jar", rom, std::to_string(s.w), std::to_string(s.h),
         "-m", s.scale, "-i", fil, "-r", s.rotate, "-k", writePadmap()};
  } else {
    std::string J = D + "/jlr";
    cwd = J;
    writeJlrProps(rom, s);
    a = {java, "-Xms32m", "-Xmx" + std::to_string(s.jmem ? std::max(s.jmem, 256) : 256) + "m", "-Xss1m", "-XX:+UseSerialGC", "-XX:+UnlockDiagnosticVMOptions",
         "-XX:-BytecodeVerificationRemote", "-XX:-UsePerfData", "-XX:+AutoCreateSharedArchive", "-XX:SharedArchiveFile=" + J + "/cds.jsa",
         "-Djava.awt.headless=true", "-Djlr.home=" + J, "-Djlr.lib.dir=" + J + "/lib", "-Djlr.games=" + gamesDir};
    for (const char* db : {"/opt/inttools/gamecontrollerdb.txt"}) if (access(db, R_OK) == 0) a.push_back(std::string("-Djlr.gamecontrollerdb=") + db);
    a.insert(a.end(), {"-cp", J + "/jlr.jar", "jlr.Main", rom});
  }
  env.push_back("SDL_VIDEODRIVER=kmsdrm");
  return a;
}

// ---------------- exit watcher: hold SELECT+START for 2 s quits the game ----------------
static void killTree(pid_t pid) {
  kill(-pid, SIGCONT);
  kill(-pid, SIGTERM);
  for (int i = 0; i < 20; i++) { if (waitpid(pid, nullptr, WNOHANG) == pid) return; usleep(100000); }
  kill(-pid, SIGKILL);
}
static int watchChild(pid_t pid) {
  bool gs = goSuper();
  int SEL = gs ? 12 : 8, STA = gs ? 13 : 9;
  std::map<std::string, int> fds;
  std::map<int, std::vector<bool>> down;
  double heldSince = 0;
  int status = 0, tick = 0;
  while (true) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) break;
    if (tick++ % 40 == 0) {
      glob_t g{};
      if (glob("/dev/input/js*", 0, nullptr, &g) == 0)
        for (size_t i = 0; i < g.gl_pathc; i++)
          if (!fds.count(g.gl_pathv[i])) { int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK); if (fd >= 0) fds[g.gl_pathv[i]] = fd; }
      globfree(&g);
    }
    bool combo = false;
    for (auto it = fds.begin(); it != fds.end();) {
      js_event ev;
      ssize_t n;
      auto& st = down[it->second];
      if (st.size() < 32) st.resize(32);
      while ((n = read(it->second, &ev, sizeof ev)) == sizeof ev)
        if ((ev.type & ~JS_EVENT_INIT) == JS_EVENT_BUTTON && ev.number < 32) st[ev.number] = ev.value != 0;
      if (n < 0 && errno != EAGAIN) { close(it->second); down.erase(it->second); it = fds.erase(it); continue; }
      if (st[SEL] && st[STA]) combo = true;
      ++it;
    }
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = ts.tv_sec + ts.tv_nsec / 1e9;
    if (combo) { if (heldSince == 0) heldSince = now; else if (now - heldSince > 2.0) { killTree(pid); waitpid(pid, &status, 0); break; } }
    else heldSince = 0;
    usleep(50000);
  }
  for (auto& f : fds) close(f.second);
  return WIFEXITED(status) ? WEXITSTATUS(status) : 0;
}

int run(const std::string& rom, bool dry) {
  if (access(rom.c_str(), R_OK) != 0) { fprintf(stderr, "Không thấy game: %s\n", rom.c_str()); return 1; }
  Settings s = prepare(rom);
  std::string cwd;
  std::vector<std::string> env;
  auto args = buildCommand(rom, s, cwd, env);
  if (dry) {
    printf("core=%s res=%s (%dx%d) fps=%s fast3d=%s phone=%s jmem=%d\ncwd=%s\n", s.core.c_str(), s.res.c_str(), s.w, s.h, s.fps.c_str(), s.fast3d.c_str(), s.phone.c_str(), s.jmem, cwd.c_str());
    for (auto& a : args) printf("%s ", a.c_str());
    printf("\n");
    return 0;
  }
  std::string log = J2ME_DIR() + "/last.log";
  sh("mkdir -p '" + J2ME_DIR() + "/engine/home' '" + J2ME_DIR() + "/engine/data' '" + J2ME_DIR() + "/jlr/data'");
  pid_t pid = fork();
  if (pid == 0) {
    setpgid(0, 0);
    int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
    if (chdir(cwd.c_str()) != 0) _exit(126);
    for (auto& e : env) putenv(strdup(e.c_str()));
    std::vector<char*> av;
    for (auto& a : args) av.push_back(const_cast<char*>(a.c_str()));
    av.push_back(nullptr);
    execv(av[0], av.data());
    _exit(127);
  }
  if (pid < 0) return 1;
  setpgid(pid, pid);
  return watchChild(pid);
}

}  // namespace j2me
}  // namespace s40
