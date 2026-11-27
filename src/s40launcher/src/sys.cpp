// System integration (dArkOS): power, audio, backlight, Wi-Fi, game launching, music, lunar calendar.
#include "s40.h"
#include <SDL2/SDL_mixer.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace s40 {

static std::string readFile(const std::string& p) {
  std::ifstream f(p);
  std::stringstream ss; ss << f.rdbuf();
  return ss.str();
}
static std::string q(const std::string& s) {  // single-quote for shell
  std::string o = "'";
  for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
  return o + "'";
}

namespace sys {
int battery() {
  static Uint32 at = 0; static int cached = 82;
  if (SDL_GetTicks() - at < 5000 && at) return cached;
  at = SDL_GetTicks();
  // s40-battery.sh (coulomb counting + voltage curve): the PMIC's own "capacity" jumps and is far off. Fallback: the kernel value.
  std::string v = trim(readFile("/run/s40/battery"));
  if (v.empty()) v = trim(readFile("/sys/class/power_supply/battery/capacity"));
  if (!v.empty()) cached = std::max(0, std::min(100, atoi(v.c_str())));
  return cached;
}
bool charging() {
  static Uint32 at = 0; static bool c = false;
  if (SDL_GetTicks() - at < 5000 && at) return c;
  at = SDL_GetTicks();
  c = trim(readFile("/sys/class/power_supply/battery/status")) == "Charging";
  return c;
}
bool headphone() {
  static Uint32 at = 0; static bool h = false;
  if (SDL_GetTicks() - at < 1500 && at) return h;
  at = SDL_GetTicks();
  h = false;
  // 3.5 mm jack: the codec's ALSA jack control ("Headphone Jack" on the rk817 card)
  { std::string a = sh("amixer -c0 cget iface=CARD,name='Headphone Jack' 2>/dev/null | grep -c ': values=on'"); h = atoi(a.c_str()) > 0; }
  // USB audio device (headphones / DAC on the OTG port)
  if (!h) h = trim(sh("grep -ci 'USB-Audio' /proc/asound/cards 2>/dev/null")) != "0";
  return h;
}
bool usbDevice() {
  static Uint32 at = 0; static bool u = false;
  if (SDL_GetTicks() - at < 2000 && at) return u;
  at = SDL_GetTicks();
  // any USB device that is not a hub, not USB audio, and not the board's own Wi-Fi/Bluetooth adapter
  std::string r = sh("n=0; for d in /sys/bus/usb/devices/[0-9]*-[0-9.]*; do [ -e $d/idVendor ] || continue; case $(cat $d/bDeviceClass) in 09) continue;; esac; "
                     "ls $d/*/net >/dev/null 2>&1 && continue; ls -d $d/*:*/sound >/dev/null 2>&1 && continue; ls -d $d/*:*/sound* >/dev/null 2>&1 && continue; "
                     "case \"$(cat $d/*:*/bInterfaceClass 2>/dev/null | head -1)\" in 01) continue;; e0) continue;; esac; n=$((n+1)); done; echo $n");
  u = atoi(r.c_str()) > 0;
  return u;
}
void setVolume(int v) {
  if (!isDevice()) return;
  run("amixer -q sset Playback " + std::to_string(v * 10) + "% >/dev/null 2>&1 || amixer -q sset Master " + std::to_string(v * 10) + "% >/dev/null 2>&1");
}
void setBrightness(int v) {
  if (!isDevice()) return;
  run("brightnessctl -q s " + std::to_string(std::max(5, v * 10)) + "% >/dev/null 2>&1");
}
void wifiPower(bool on) { if (isDevice()) run(std::string("sudo nmcli radio wifi ") + (on ? "on" : "off") + " >/dev/null 2>&1 &"); }
std::string wifiSsid() {
  if (!isDevice()) return S.getb("wifi", true) ? "Nha_Tro_5G" : "";
  static Uint32 at = 0; static std::string ssid;
  if (at && SDL_GetTicks() - at < 8000) return ssid;
  at = SDL_GetTicks();
  ssid.clear();
  // cheap check first: no link -> no SSID, and never let nmcli start a Wi-Fi scan from the UI thread
  if (trim(readFile("/sys/class/net/wlan0/operstate")) != "up") return ssid;
  for (auto& l : split(sh("nmcli -t -f ACTIVE,SSID dev wifi list --rescan no"), '\n')) if (l.rfind("yes:", 0) == 0) { ssid = l.substr(4); return ssid; }
  ssid.clear();
  return ssid;
}
std::vector<Net> wifiScan() {
  std::vector<Net> out;
  if (!isDevice()) return {{"Nha_Tro_5G", 80, true}, {tl("Cafe Mây"), 60, false}, {"VNPT_Ha", 45, true}};
  for (auto& l : split(sh("nmcli -t -f SSID,SIGNAL,SECURITY dev wifi list --rescan yes"), '\n')) {
    auto f = split(l, ':');
    if (f.size() < 3 || f[0].empty()) continue;
    if (std::any_of(out.begin(), out.end(), [&](const Net& n) { return n.ssid == f[0]; })) continue;
    out.push_back({f[0], atoi(f[1].c_str()), !f[2].empty() && f[2] != "--"});
  }
  std::sort(out.begin(), out.end(), [](const Net& a, const Net& b) { return a.signal > b.signal; });
  return out;
}
bool wifiConnect(const std::string& ssid, const std::string& pw, std::string& err) {
  if (!isDevice()) { SDL_Delay(1500); return true; }
  // security the access point advertises ("WPA2", "WPA2 WPA3", "WPA3", ...); nmcli -t escapes ':' in SSIDs
  std::string sec;
  for (auto& l : split(sh("nmcli -t -f SSID,SECURITY dev wifi list --rescan no"), '\n')) {
    size_t c = std::string::npos;
    for (size_t i = 0; i < l.size(); i++) { if (l[i] == '\\') { i++; continue; } if (l[i] == ':') c = i; }
    if (c == std::string::npos) continue;
    std::string s = l.substr(0, c);
    for (size_t p; (p = s.find("\\:")) != std::string::npos;) s.replace(p, 2, ":");
    if (s == ssid) { sec = l.substr(c + 1); break; }
  }
  auto ok = [](const std::string& o) { return o.find("successfully") != std::string::npos; };
  // 1. the normal way
  std::string o = sh("sudo nmcli -w 40 dev wifi connect " + q(ssid) + (pw.empty() ? "" : " password " + q(pw)) + " 2>&1");
  if (ok(o)) return true;
  std::string first = o;
  // New routers run WPA2/WPA3 "transition" mode (or require PMF). NetworkManager then picks WPA3-SAE
  // whenever the driver claims it, but the USB Wi-Fi drivers these handhelds use (Realtek vendor
  // drivers, kernel 4.4) fail the SAE handshake -> "Secrets were required". Retry with an explicit
  // profile: 2. WPA2-PSK with PMF off, 3. WPA3-SAE with PMF required (WPA3-only routers).
  auto profile = [&](const std::string& sec_args) {
    run("sudo nmcli con delete id " + q(ssid) + " >/dev/null 2>&1");
    return sh("sudo nmcli con add type wifi ifname '*' con-name " + q(ssid) + " ssid " + q(ssid) + sec_args +
              " >/dev/null 2>&1 && sudo nmcli -w 40 con up id " + q(ssid) + " 2>&1");
  };
  if (!pw.empty()) {
    bool wpa3 = sec.find("WPA3") != std::string::npos, wpa2 = sec.find("WPA2") != std::string::npos || sec.find("WPA1") != std::string::npos;
    if (!wpa3 || wpa2) { o = profile(" wifi-sec.key-mgmt wpa-psk wifi-sec.psk " + q(pw) + " wifi-sec.pmf 1"); if (ok(o)) return true; }
    if (wpa3) { o = profile(" wifi-sec.key-mgmt sae wifi-sec.psk " + q(pw) + " wifi-sec.pmf 3"); if (ok(o)) return true; }
    run("sudo nmcli con delete id " + q(ssid) + " >/dev/null 2>&1");  // no broken profile left behind
    if (wpa3 && !wpa2) { err = tl("Router chỉ dùng WPA3, USB Wi-Fi này không hỗ trợ. Hãy bật chế độ WPA2/WPA3 trên router"); return false; }
  }
  std::string all = first + o;
  if (all.find("No network with SSID") != std::string::npos) err = tl("Không thấy mạng này, hãy quét lại");
  else if (all.find("Secrets were required") != std::string::npos || all.find("psk") != std::string::npos ||
           all.find("password") != std::string::npos)
    err = tl("Sai mật khẩu Wi-Fi (hoặc router chặn thiết bị lạ)");
  else if (all.find("No Wi-Fi device") != std::string::npos || all.find("not available") != std::string::npos)
    err = tl("Không thấy USB Wi-Fi, hãy cắm lại");
  else if (all.find("Timeout") != std::string::npos || all.find("timed out") != std::string::npos)
    err = tl("Router không phản hồi, hãy lại gần router hơn");
  else err = tl("Không kết nối được: ") + trim(first).substr(0, 60);
  return false;
}
std::string ipAddr() { std::string o = trim(sh("hostname -I")); return o.empty() ? "—" : split(o, ' ')[0]; }
void suspend() { if (isDevice()) run("sudo systemctl suspend"); }
void reboot() { if (isDevice()) run("sudo reboot"); else quitApp(0); }
void poweroff() { if (isDevice()) run("sudo poweroff"); else quitApp(0); }

int launchExternal(const std::string& cmd) {
  music::suspend(true);
  std::string assets = G.assetDir;
  G.shutdown();
  SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
  if (isDevice()) run("printf '\\033c' > /dev/tty1 2>/dev/null");
  int rc = run(cmd);
  if (isDevice()) run("printf '\\033c' > /dev/tty1 2>/dev/null");
  G.init(assets, !isDevice() && getenv("S40_WINDOW"), 1);
  for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
  music::suspend(false);
  return rc;
}
}  // namespace sys

// ================= game systems =================
static std::string tagVal(const std::string& b, const std::string& tag) {
  auto a = b.find("<" + tag + ">");
  if (a == std::string::npos) return "";
  a += tag.size() + 2;
  auto e = b.find("</" + tag + ">", a);
  return e == std::string::npos ? "" : trim(b.substr(a, e - a));
}
std::vector<GameSys>& systems(bool rescan) {
  static std::vector<GameSys> v;
  static bool loaded = false;
  if (loaded && !rescan) return v;
  loaded = true;
  v.clear();
  const char* env = getenv("S40_ES_SYSTEMS");
  std::string cfg = readFile(env ? env : "/etc/emulationstation/es_systems.cfg");
  const char* rootEnv = getenv("S40_ROMS");  // desktop testing: remap /roms
  size_t pos = 0;
  while ((pos = cfg.find("<system>", pos)) != std::string::npos) {
    size_t end = cfg.find("</system>", pos);
    if (end == std::string::npos) break;
    std::string b = cfg.substr(pos, end - pos);
    pos = end;
    GameSys s;
    s.name = tagVal(b, "name");
    s.fullname = tagVal(b, "fullname");
    s.path = tagVal(b, "path");
    s.command = tagVal(b, "command");
    if (rootEnv && s.path.rfind("/roms", 0) == 0) s.path = rootEnv + s.path.substr(5);
    // First emulator/core that is really installed: a RetroArch core needs its .so; standalone
    // emulators (no <cores>) are taken as present. Falls back to the very first entry.
    std::string core = tagVal(b, "core"), emu;
    {
      auto ep = b.find("<emulator name=\"");
      if (ep != std::string::npos) { size_t q = ep + 16; emu = b.substr(q, b.find('"', q) - q); }
      bool picked = false;
      // collect every installed emulator/core (per-game choice); the first one is the system default
      for (size_t p = ep; p != std::string::npos; p = b.find("<emulator name=\"", p + 1)) {
        size_t q = p + 16;
        std::string en = b.substr(q, b.find('"', q) - q);
        size_t endE = b.find("</emulator>", p);
        std::string eb = b.substr(p, endE == std::string::npos ? std::string::npos : endE - p);
        if (eb.find("<core>") == std::string::npos) { s.choices.push_back({en, ""}); if (!picked) { emu = en; core.clear(); picked = true; } continue; }
        for (size_t c = eb.find("<core>"); c != std::string::npos; c = eb.find("<core>", c + 1)) {
          std::string cn = eb.substr(c + 6, eb.find("</core>", c) - c - 6);
          if (!isDevice() || fileExists("/home/ark/.config/" + en + "/cores/" + cn + "_libretro.so")) {
            s.choices.push_back({en, cn});
            if (!picked) { emu = en; core = cn; picked = true; }
          }
        }
      }
    }
    s.emulator = emu; s.core = core;
    for (auto& e : split(tagVal(b, "extension"), ' ')) if (!e.empty()) s.exts.push_back(lower(e));
    if (s.name.empty() || s.path.empty() || s.name == "retroarch" || s.name == "tools" || s.name == "videos") continue;
    for (auto& f : listDir(s.path)) {
      auto dot = f.find_last_of('.');
      if (dot == std::string::npos) continue;
      std::string ext = lower(f.substr(dot));
      if (std::find(s.exts.begin(), s.exts.end(), ext) != s.exts.end()) s.roms.push_back(f);
    }
    if (!s.roms.empty()) v.push_back(s);
  }
  std::string hidden = S.get("hidden");
  return v;
}
static void replaceAll(std::string& s, const std::string& a, const std::string& b) {
  for (size_t p = 0; (p = s.find(a, p)) != std::string::npos; p += b.size()) s.replace(p, a.size(), b);
}
void launchGame(const GameSys& s, const std::string& rom) {
  std::string path = s.path + (s.path.back() == '/' ? "" : "/") + rom;
  std::string esc;
  for (char c : path) { if (strchr(" '\"()&;!$`[]{}*?<>|#~", c)) esc += '\\'; esc += c; }
  std::string base = rom.substr(0, rom.find_last_of('.'));
  std::string cmd = s.command;
  std::string perf = S.get("perf", tl("Cân bằng"));
  replaceAll(cmd, "%ROM%", esc);
  replaceAll(cmd, "%ROMRAW%", path);
  replaceAll(cmd, "%BASENAME%", base);
  auto ec = gameCore(s, rom);  // per-game emulator/core if one was chosen
  replaceAll(cmd, "%EMULATOR%", ec.first);
  replaceAll(cmd, "%CORE%", ec.second);
  replaceAll(cmd, "%GOVERNOR%", perf == tl("Hiệu năng tối đa") ? "performance" : perf == tl("Tiết kiệm pin") ? "powersave" : "ondemand");
  if (!isDevice()) { fprintf(stderr, "[launch] %s\n", cmd.c_str()); SDL_Delay(800); return; }
  sys::launchExternal(cmd);
}

// ================= music (SDL_mixer) =================
namespace music {
static std::vector<Track> lib;
static std::vector<int> queue;
static int qi = -1;
static Mix_Music* mus = nullptr;
static bool isPlaying = false, opened = false;
static int suspendDepth = 0;
static double posBase = 0; static Uint32 startTick = 0;
static bool finished = false;

std::vector<Track>& library(bool rescan) {
  static bool loaded = false;
  if (loaded && !rescan) return lib;
  loaded = true; lib.clear();
  std::string dir = romsBase() + "/music";
  for (auto& f : listDir(dir)) {
    std::string e = lower(f.substr(f.find_last_of('.') + 1));
    if (e != "mp3" && e != "ogg" && e != "flac" && e != "wav" && e != "opus") continue;
    Track t; t.path = dir + "/" + f;
    std::string base = f.substr(0, f.find_last_of('.'));
    auto d = base.find(" - ");
    if (d != std::string::npos) { t.artist = base.substr(0, d); t.title = base.substr(d + 3); } else { t.title = base; t.artist = tl("Không rõ nghệ sĩ"); }
    lib.push_back(t);
  }
  return lib;
}
static void ensureOpen() {
  if (opened) return;
  if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) == 0) { opened = true; Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_FLAC | MIX_INIT_OPUS); Mix_HookMusicFinished([] { finished = true; }); }
}
static void load(int i) {
  if (mus) { Mix_HaltMusic(); Mix_FreeMusic(mus); mus = nullptr; }
  qi = i; posBase = 0; startTick = SDL_GetTicks();
  if (qi < 0 || qi >= (int)queue.size()) { isPlaying = false; return; }
  ensureOpen();
  mus = Mix_LoadMUS(lib[queue[qi]].path.c_str());
  if (mus && !suspendDepth) Mix_PlayMusic(mus, 0);
  Mix_VolumeMusic(MIX_MAX_VOLUME * S.geti("vol", 6) / 10);
  isPlaying = mus != nullptr;
}
bool playing() { return isPlaying && mus; }
bool active() { return mus != nullptr; }
const Track* current() { return (qi >= 0 && qi < (int)queue.size()) ? &lib[queue[qi]] : nullptr; }
double pos() { return posBase + (isPlaying && !suspendDepth ? (SDL_GetTicks() - startTick) / 1000.0 : 0); }
double length() {
#if SDL_MIXER_VERSION_ATLEAST(2, 6, 0)
  return mus ? std::max(0.0, Mix_MusicDuration(mus)) : 0;
#else
  return 0;
#endif
}
void playList(const std::vector<int>& idx, int at) { queue = idx; load(at); }
void toggle() {
  if (!mus) {
    if (library().empty()) { toast(tl("Chưa có nhạc trong ") + romsBase() + "/music", "music"); return; }
    std::vector<int> all; for (int i = 0; i < (int)lib.size(); i++) all.push_back(i);
    playList(all, 0); toast(current()->title, "play"); return;
  }
  if (isPlaying) { posBase = pos(); Mix_PauseMusic(); isPlaying = false; toast(tl("Tạm dừng nhạc"), "pause"); }
  else { Mix_ResumeMusic(); startTick = SDL_GetTicks(); isPlaying = true; toast(current()->title, "play"); }
}
void next() {
  if (queue.empty()) return;
  int n = (int)queue.size();
  int ni = S.getb("shuffle") && n > 1 ? (qi + 1 + rand() % (n - 1)) % n : (qi + 1) % n;
  load(ni);
  if (current()) toast(current()->title, "next");
}
void prev() {
  if (queue.empty()) return;
  if (pos() > 3) { load(qi); return; }
  load((qi - 1 + (int)queue.size()) % (int)queue.size());
  if (current()) toast(current()->title, "prev");
}
void stop() { if (mus) { Mix_HaltMusic(); Mix_FreeMusic(mus); mus = nullptr; } isPlaying = false; queue.clear(); qi = -1; if (opened) { Mix_CloseAudio(); opened = false; } }
void suspend(bool s) {
  if (s) {
    if (suspendDepth++ == 0 && opened) { if (isPlaying) posBase = pos(); if (mus) Mix_HaltMusic(); Mix_CloseAudio(); opened = false; }
  } else if (suspendDepth > 0 && --suspendDepth == 0 && mus) {
    // reopen and resume from saved position
    Mix_FreeMusic(mus); mus = nullptr;
    double p = posBase; bool was = isPlaying; int keep = qi;
    load(keep);
    if (mus) { Mix_SetMusicPosition(p); posBase = p; startTick = SDL_GetTicks(); if (!was) { Mix_PauseMusic(); isPlaying = false; } }
  }
}
void update() {
  if (finished) {
    finished = false;
    int rep = S.geti("repeat", 1);
    if (rep == 2) load(qi);
    else if (qi + 1 < (int)queue.size() || rep == 1 || S.getb("shuffle")) next();
    else { isPlaying = false; }
  }
  static int lastVol = -1;
  int v = S.geti("vol", 6);
  if (opened && v != lastVol) { Mix_VolumeMusic(MIX_MAX_VOLUME * v / 10); lastVol = v; }
}
}  // namespace music

// ================= lunar calendar =================
static int jdFromDate(int dd, int mm, int yy) {
  int a = (14 - mm) / 12, y = yy + 4800 - a, m = mm + 12 * a - 3;
  int jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
  if (jd < 2299161) jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - 32083;
  return jd;
}
static int newMoon(int k) {
  const double dr = M_PI / 180, T = k / 1236.85, T2 = T * T, T3 = T2 * T;
  double J = 2415020.75933 + 29.53058868 * k + .0001178 * T2 - .000000155 * T3 + .00033 * sin((166.56 + 132.87 * T - .009173 * T2) * dr);
  double M = 359.2242 + 29.10535608 * k - .0000333 * T2 - .00000347 * T3, Mp = 306.0253 + 385.81691806 * k + .0107306 * T2 + .00001236 * T3,
         F = 21.2964 + 390.67050646 * k - .0016528 * T2 - .00000239 * T3;
  double C = (.1734 - .000393 * T) * sin(M * dr) + .0021 * sin(2 * dr * M) - .4068 * sin(Mp * dr) + .0161 * sin(dr * 2 * Mp) - .0004 * sin(dr * 3 * Mp) +
             .0104 * sin(dr * 2 * F) - .0051 * sin(dr * (M + Mp)) - .0074 * sin(dr * (M - Mp)) + .0004 * sin(dr * (2 * F + M)) - .0004 * sin(dr * (2 * F - M)) -
             .0006 * sin(dr * (2 * F + Mp)) + .001 * sin(dr * (2 * F - Mp)) + .0005 * sin(dr * (2 * Mp + M));
  double dt = T < -11 ? .001 + .000839 * T + .0002261 * T2 - .00000845 * T3 - .000000081 * T * T3 : -.000278 + .000265 * T + .000262 * T2;
  return (int)floor(J + C - dt + .5 + 7 / 24.0);
}
static int sunLong(int j) {
  const double dr = M_PI / 180, T = (j - 2451545.5 - 7 / 24.0) / 36525, T2 = T * T;
  double M = 357.5291 + 35999.0503 * T - .0001559 * T2 - .00000048 * T * T2, L0 = 280.46645 + 36000.76983 * T + .0003032 * T2;
  double L = (L0 + (1.9146 - .004817 * T - .000014 * T2) * sin(dr * M) + (.019993 - .000101 * T) * sin(dr * 2 * M) + .00029 * sin(dr * 3 * M)) * dr;
  L -= M_PI * 2 * floor(L / (M_PI * 2));
  return (int)floor(L / M_PI * 6);
}
static int month11(int yy) {
  int k = (int)floor((jdFromDate(31, 12, yy) - 2415021) / 29.530588853);
  int nm = newMoon(k);
  if (sunLong(nm) >= 9) nm = newMoon(k - 1);
  return nm;
}
static int leapOff(int a11) {
  int k = (int)floor((a11 - 2415021.076998695) / 29.530588853 + .5), last, i = 1, arc = sunLong(newMoon(k + i));
  do { last = arc; i++; arc = sunLong(newMoon(k + i)); } while (arc != last && i < 14);
  return i - 1;
}
Lunar toLunar(int dd, int mm, int yy) {
  int day = jdFromDate(dd, mm, yy), k = (int)floor((day - 2415021.076998695) / 29.530588853);
  int ms = newMoon(k + 1);
  if (ms > day) ms = newMoon(k);
  int a11 = month11(yy), b11 = a11, ly;
  if (a11 >= ms) { ly = yy; a11 = month11(yy - 1); } else { ly = yy + 1; b11 = month11(yy + 1); }
  int d = day - ms + 1, diff = (ms - a11) / 29, m = diff + 11;
  bool leap = false;
  if (b11 - a11 > 365) { int lo = leapOff(a11); if (diff >= lo) { m = diff + 10; if (diff == lo) leap = true; } }
  if (m > 12) m -= 12;
  if (m >= 11 && diff < 4) ly -= 1;
  return {d, m, ly, leap, day};
}
static const char* CAN[] = {tl("Giáp"), tl("Ất"), tl("Bính"), tl("Đinh"), tl("Mậu"), tl("Kỷ"), tl("Canh"), tl("Tân"), tl("Nhâm"), tl("Quý")};
static const char* CHI[] = {tl("Tý"), tl("Sửu"), tl("Dần"), tl("Mão"), tl("Thìn"), tl("Tỵ"), tl("Ngọ"), tl("Mùi"), tl("Thân"), tl("Dậu"), tl("Tuất"), tl("Hợi")};
// stem + branch: "Bính Ngọ", "Bing Horse", "丙午" (Chinese writes them together)
static std::string ganzhi(int c, int z) { return std::string(CAN[c]) + (s40tr::lang() == "zh" ? "" : " ") + CHI[z]; }
std::string lunarYearName(int y) { return ganzhi((y + 6) % 10, (y + 8) % 12); }
std::string lunarDayName(int jd) { return ganzhi((jd + 9) % 10, (jd + 1) % 12); }
std::string lunarMonthName(int m, int y) { return ganzhi((y * 12 + m + 3) % 10, (m + 1) % 12); }
std::string lunarHoliday(const Lunar& l) {
  if (l.leap) return "";
  static const std::map<std::pair<int, int>, const char*> H = {
      {{1, 1}, tl("Tết Nguyên đán")}, {{2, 1}, tl("Mùng 2 Tết")}, {{3, 1}, tl("Mùng 3 Tết")}, {{15, 1}, tl("Rằm tháng Giêng")}, {{10, 3}, tl("Giỗ Tổ Hùng Vương")},
      {{15, 4}, tl("Lễ Phật đản")}, {{5, 5}, tl("Tết Đoan ngọ")}, {{15, 7}, tl("Lễ Vu Lan")}, {{15, 8}, tl("Tết Trung thu")}, {{23, 12}, tl("Ông Công ông Táo")}};
  auto it = H.find({l.d, l.m});
  return it == H.end() ? "" : it->second;
}

}  // namespace s40
