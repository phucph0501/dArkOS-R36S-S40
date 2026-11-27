// S40 launcher for dArkOS (R36S / RG351MP, 640x480) - Nokia S40 style frontend.
#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "s40tr.h"  // tl(): UI text in the chosen language (common/)

namespace s40 {

constexpr int W = 640, H = 480;
constexpr int STATUS_H = 28, TITLE_H = 40, SOFT_H = 52;

enum class Key { None, Up, Down, Left, Right, A, B, OK, L, R };

struct Col { Uint8 r, g, b, a = 255; };
Col hex(const char* s);
Col mix(Col a, Col b, float t);
Col lighten(Col c, int d = 60);

struct Theme { std::string name; Col a1, a2, hl1, hl2, bg1, bg2, txt, sub; };
extern std::vector<Theme> THEMES;
const Theme& theme();

// ---------- settings (key=value file) ----------
struct Store {
  std::map<std::string, std::string> v;
  std::string path;
  void load(const std::string& p);
  void save();
  std::string get(const std::string& k, const std::string& d = "") const;
  int geti(const std::string& k, int d = 0) const;
  bool getb(const std::string& k, bool d = false) const;
  void set(const std::string& k, const std::string& val);
  void seti(const std::string& k, int val) { set(k, std::to_string(val)); }
  void setb(const std::string& k, bool val) { set(k, val ? "1" : "0"); }
};
extern Store S;
void applySetting(const std::string& key);  // side effects (volume, brightness, wifi...)

// ---------- graphics ----------
struct Gfx {
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  std::string assetDir;
  bool init(const std::string& assets, bool windowed, int scale);
  void shutdown();
  void clip(const SDL_Rect* r);
  void rect(int x, int y, int w, int h, Col c);
  void grad(int x, int y, int w, int h, Col top, Col bot, int radius = 0);
  void round(int x, int y, int w, int h, Col c, int radius);
  void line(int x1, int y1, int x2, int y2, Col c);
  void circle(int cx, int cy, int r, Col c);  // text: align 0=left 1=center 2=right; returns width drawn
  int text(const std::string& s, int x, int y, int size, Col c, bool bold = false, int align = 0, int maxw = 0);
  int textW(const std::string& s, int size, bool bold = false);
  void icon(const std::string& name, int x, int y, int size, Col c);
  void badge(const std::string& ic, int x, int y, int size, Col c);
  SDL_Texture* image(const std::string& path);  // cached
  void present();
  void flushTextCache();
 private:
  std::map<std::pair<int, bool>, TTF_Font*> fonts;
  std::map<std::string, SDL_Texture*> icons, textCache, images;
  TTF_Font* font(int size, bool bold);
};
extern Gfx G;

// ---------- screens ----------
struct Soft { std::string l, c, r; };

struct Screen {
  virtual ~Screen() = default;
  bool overlay = false, noTitle = false, full = false, live = false, capture = false, noIdle = false;
  // frames per second once there has been no input for a second; 0 = full rate (animations, games)
  int idleFps = 0;
  std::string ic;
  virtual std::string title() { return ""; }
  virtual std::string info() { return ""; }
  virtual void draw(const SDL_Rect& area) = 0;
  virtual Soft soft() { return {"", "", ""}; }
  virtual void key(Key k) = 0;
  virtual void tick(double /*dt*/) {}
  virtual void onEnter() {}
  virtual void onExit() {}
};

void push(Screen* s);
void pop();
void resetTo(Screen* s);
Screen* top();
size_t depth();
void toast(const std::string& msg, const std::string& icon = "info");
void quitApp(int code);

// ---------- list widget ----------
struct Item {
  std::string label, sub, icon, right;
  Col color{84, 110, 122};
  int bar = -1, barMax = 10;   // draw a level bar on the right
  bool fav = false, dis = false, radio = false, radioOn = false, check = false, checkOn = false;
  std::function<void(int)> adj;
  std::function<void()> ok;
  std::string okLabel;
};
using Items = std::vector<Item>;

struct ListScreen : Screen {
  std::string ttl;
  std::function<Items()> build;
  std::function<std::vector<struct Opt>(const Item&, int)> opts;
  std::string empty = tl("(Trống)"), okDefault = tl("Chọn");
  int sel = 0, topRow = 0, vis = 8, rowH = 45;
  ListScreen(std::string t, std::function<Items()> b, std::string icon = "list");
  std::string title() override { return ttl; }
  std::string info() override;
  void draw(const SDL_Rect& a) override;
  Soft soft() override;
  void key(Key k) override;
  void drawList(const Items& its, const SDL_Rect& a);
};
void drawItems(const Items& its, int& sel, int& topRow, int vis, int rowH, const SDL_Rect& a, bool inPopup);

struct Opt { std::string label, icon; std::function<void()> fn; Col color{96, 125, 139}; };
void openOptions(std::vector<Opt> opts, const std::string& title = tl("Tùy chọn"));
void openPicker(const std::string& title, std::vector<std::string> labels, int cur, std::function<void(int)> pick);
void openMulti(const std::string& title, std::vector<std::string> labels, std::vector<int> chosen, std::function<void(std::vector<int>)> done);
void confirmBox(const std::string& text, std::function<void()> yes, const std::string& icon = "warn");
void infoBox(const std::string& title, std::vector<std::pair<std::string, std::string>> rows);
void runTask(const std::string& title, std::vector<std::string> steps, std::function<bool(std::string&)> work, std::function<void()> done = nullptr);
bool ytAppInstalled();                 // yt-dlp + Node + mpv wrapper present (ytapp.cpp)
Screen* YouTubeApp();                   // YouTube without the web page (search, list with thumbnails, mpv)
Screen* OSK(const std::string& title, const std::string& init, bool secret, std::function<void(const std::string&)> done);
bool oskSetText(const std::string& t);  // test automation
bool oskTypeKeys(const std::string& keys);  // test automation: type through the input engine

// ---------- helpers ----------
std::string fmtTime(int sec);
std::string clockStr(bool big = false, std::string* ampm = nullptr);
std::string dateStr();
std::string dateNum(int d, int m, int y);
extern const char* DAYS[7];
struct tm nowTm();
std::string sh(const std::string& cmd);       // run command, return stdout
int run(const std::string& cmd);              // run command, return exit code
bool fileExists(const std::string& p);
std::vector<std::string> listDir(const std::string& dir);
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s, char sep);
std::string lower(const std::string& s);
bool isDevice();  // running on the handheld (vs desktop test build)
// ROM card root: /roms, or /roms2 after dArkOS "Switch to SD2 for Roms" (S40_ROMS overrides for tests)
std::string romsBase();
// home screen texts chosen in Settings (also on the boot screen); "" hides the line
std::string homeTitle();
std::string homeSub();
Screen* SetupWizard();  // first start after installing (settings.cpp)
// screens.cpp: photos on the ROM card, and a photo as wallpaper (cropped + scaled to the screen)
std::vector<std::string> photoFiles();
bool setWallpaper(const std::string& src);

// lunar calendar (Ho Ngoc Duc algorithm, timezone +7)
struct Lunar { int d, m, y; bool leap; int jd; };
Lunar toLunar(int dd, int mm, int yy);
std::string lunarYearName(int y);
std::string lunarDayName(int jd);
std::string lunarMonthName(int m, int y);
std::string lunarHoliday(const Lunar& l);

// ---------- system integration ----------
namespace sys {
int battery();
bool charging();
bool headphone();
bool usbDevice();
void setVolume(int v);       // 0..10
void setBrightness(int v);   // 1..10
void wifiPower(bool on);
std::string wifiSsid();
struct Net { std::string ssid; int signal; bool secure; };
std::vector<Net> wifiScan();
bool wifiConnect(const std::string& ssid, const std::string& pw, std::string& err);
std::string ipAddr();
void suspend();
void reboot();
void poweroff();
// releases the display, runs a command (emulator/app), restores the display
int launchExternal(const std::string& cmd);
}

// ---------- game systems (from es_systems.cfg) ----------
struct GameSys {
  std::string name, fullname, path, command, emulator, core;
  std::vector<std::pair<std::string, std::string>> choices;  // installed (emulator, core); core "" = standalone
  std::vector<std::string> exts;
  std::vector<std::string> roms;  // file names
};
std::vector<GameSys>& systems(bool rescan = false);
void launchGame(const GameSys& s, const std::string& rom);
// gameopts.cpp: per-game emulator/core + RetroArch overrides, RetroAchievements
std::pair<std::string, std::string> gameCore(const GameSys& s, const std::string& rom);
Screen* GameOptionsScreen(const GameSys& s, const std::string& rom);
Screen* RetroAchievementsPage();

// ---------- music (SDL_mixer) ----------
namespace music {
struct Track { std::string path, title, artist; };
std::vector<Track>& library(bool rescan = false);
bool playing();
bool active();
const Track* current();
double pos();
double length();
void playList(const std::vector<int>& idx, int at);
void toggle();
void next();
void prev();
void stop();
void suspend(bool s);
void update();
}

// screens defined in screens.cpp
Screen* HomeScreen();
Screen* FileManagerApp();  // files.cpp
Screen* TerminalApp();     // terminal.cpp
Screen* WeatherApp();                     // weather.cpp
std::string weatherLine(bool withPlace);  // "☀ 28° Trời quang · Hà Nội", "" when no data
Screen* MainMenu();
Screen* BootScreen();
}  // namespace s40
