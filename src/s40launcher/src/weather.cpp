// Weather ("Thời tiết"): current conditions, next 24 hours and 7 days from Open-Meteo
// (open-meteo.com, free, no account or API key; data CC BY 4.0), city search through its geocoding
// API. Fetched in a background thread with curl, cached in ~/.config/s40weather.json so the last
// forecast is shown offline. Also feeds the home screen chip and the standby screen line.
#include <atomic>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <sys/stat.h>

#include "s40.h"

namespace s40 {
namespace weather {

struct Hour { std::string time; double t = 0; int code = 0, rain = 0; };
struct Day { std::string date; int code = 0, rain = 0; double tmin = 0, tmax = 0; };
struct Data {
  bool ok = false;
  double t = 0, feels = 0, wind = 0;
  int hum = 0, code = 0, isDay = 1;
  std::vector<Hour> hours;
  std::vector<Day> days;
  time_t fetched = 0;
};

static std::mutex mu;
static Data data;
static std::atomic<bool> busy{false};
static std::string lastError;
static bool loadedCache = false;

static std::string cachePath() { const char* h = getenv("HOME"); return std::string(h ? h : "/tmp") + "/.config/s40weather.json"; }
std::string place() { return S.get("wxName", tl("Hà Nội")); }
bool fahrenheit() { return S.get("wxUnit", "C") == "F"; }

// WMO weather interpretation codes -> Vietnamese text / symbol
std::string codeText(int c) {
  if (c == 0) return tl("Trời quang");
  if (c == 1) return tl("Ít mây");
  if (c == 2) return tl("Có mây");
  if (c == 3) return tl("Nhiều mây");
  if (c == 45 || c == 48) return tl("Sương mù");
  if (c >= 51 && c <= 57) return tl("Mưa phùn");
  if (c == 61 || c == 80) return tl("Mưa nhỏ");
  if (c == 63 || c == 81) return tl("Mưa vừa");
  if (c == 65 || c == 82) return tl("Mưa to");
  if (c == 66 || c == 67) return tl("Mưa lạnh");
  if (c >= 71 && c <= 77) return tl("Tuyết");
  if (c == 85 || c == 86) return tl("Mưa tuyết");
  if (c == 95) return tl("Dông");
  if (c == 96 || c == 99) return tl("Dông, mưa đá");
  return "—";
}
std::string codeIcon(int c, bool day = true) {
  if (c == 0) return day ? "☀" : "★";
  if (c <= 2) return day ? "☀" : "☁";
  if (c == 3 || c == 45 || c == 48) return "☁";
  if (c >= 95) return "⚡";
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return "❄";
  return "☂";
}
static std::string deg(double v) { char b[16]; snprintf(b, sizeof b, "%.0f°", v); return b; }

// ---- minimal JSON field extraction (Open-Meteo's fixed response layout) ----
static std::string section(const std::string& j, const std::string& key) {
  size_t p = j.find("\"" + key + "\":{");
  if (p == std::string::npos) return "";
  size_t s = j.find('{', p), e = j.find('}', s);
  return e == std::string::npos ? "" : j.substr(s, e - s + 1);
}
static double num(const std::string& j, const std::string& key, double def = 0) {
  size_t p = j.find("\"" + key + "\":");
  if (p == std::string::npos) return def;
  return strtod(j.c_str() + p + key.size() + 3, nullptr);
}
static std::string str(const std::string& j, const std::string& key) {
  size_t p = j.find("\"" + key + "\":\"");
  if (p == std::string::npos) return "";
  p += key.size() + 4;
  size_t e = j.find('"', p);
  return e == std::string::npos ? "" : j.substr(p, e - p);
}
static std::vector<std::string> arr(const std::string& j, const std::string& key) {
  std::vector<std::string> out;
  size_t p = j.find("\"" + key + "\":[");
  if (p == std::string::npos) return out;
  p = j.find('[', p) + 1;
  size_t e = j.find(']', p);
  std::stringstream ss(j.substr(p, e - p));
  std::string it;
  while (std::getline(ss, it, ',')) {
    if (!it.empty() && it.front() == '"') it = it.substr(1, it.size() - 2);
    out.push_back(it);
  }
  return out;
}

static bool parse(const std::string& j, Data& d) {
  std::string cur = section(j, "current");
  if (cur.empty()) return false;
  d.t = num(cur, "temperature_2m"); d.feels = num(cur, "apparent_temperature"); d.wind = num(cur, "wind_speed_10m");
  d.hum = (int)num(cur, "relative_humidity_2m"); d.code = (int)num(cur, "weather_code"); d.isDay = (int)num(cur, "is_day", 1);
  std::string h = section(j, "hourly"), dl = section(j, "daily");
  auto ht = arr(h, "time"), htemp = arr(h, "temperature_2m"), hcode = arr(h, "weather_code"), hrain = arr(h, "precipitation_probability");
  d.hours.clear();
  for (size_t i = 0; i < ht.size() && i < htemp.size() && i < hcode.size(); i++)
    d.hours.push_back({ht[i], atof(htemp[i].c_str()), atoi(hcode[i].c_str()), i < hrain.size() ? atoi(hrain[i].c_str()) : 0});
  auto dt = arr(dl, "time"), dc = arr(dl, "weather_code"), dmax = arr(dl, "temperature_2m_max"), dmin = arr(dl, "temperature_2m_min"), dr = arr(dl, "precipitation_probability_max");
  d.days.clear();
  for (size_t i = 0; i < dt.size() && i < dc.size() && i < dmax.size() && i < dmin.size(); i++)
    d.days.push_back({dt[i], atoi(dc[i].c_str()), i < dr.size() ? atoi(dr[i].c_str()) : 0, atof(dmin[i].c_str()), atof(dmax[i].c_str())});
  d.ok = true;
  return true;
}

static std::string fetchUrl(const std::string& url) {
  return sh("(curl -s -m 20 -A s40-weather '" + url + "' || wget -q -T 20 -O- '" + url + "') 2>/dev/null");
}
static std::string urlEnc(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.') o += (char)c;
    else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); o += b; }
  }
  return o;
}

static void loadCache() {
  if (loadedCache) return;
  loadedCache = true;
  std::ifstream f(cachePath());
  std::string j((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  Data d;
  if (!j.empty() && parse(j, d)) {
    struct stat st;
    if (stat(cachePath().c_str(), &st) == 0) d.fetched = st.st_mtime;
    std::lock_guard<std::mutex> l(mu);
    data = d;
  }
}
Data get() { loadCache(); std::lock_guard<std::mutex> l(mu); return data; }

void refresh(bool force) {
  loadCache();
  if (busy) return;
  { std::lock_guard<std::mutex> l(mu); if (!force && data.ok && time(nullptr) - data.fetched < 30 * 60) return; }
  if (!force && (!S.getb("wifi", true) || sys::wifiSsid().empty()) && isDevice()) return;
  busy = true;
  std::string lat = S.get("wxLat", "21.0285"), lon = S.get("wxLon", "105.8542");
  std::string url = "https://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon +
                    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m"
                    "&hourly=temperature_2m,weather_code,precipitation_probability&forecast_hours=24"
                    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&forecast_days=7"
                    "&timezone=auto" + std::string(fahrenheit() ? "&temperature_unit=fahrenheit" : "");
  std::thread([url] {
    std::string j = fetchUrl(url);
    Data d;
    if (parse(j, d)) {
      d.fetched = time(nullptr);
      std::string p = cachePath();
      mkdir(p.substr(0, p.rfind('/')).c_str(), 0755);
      std::ofstream(p, std::ios::trunc) << j;
      std::lock_guard<std::mutex> l(mu);
      data = d;
      lastError.clear();
    } else {
      std::lock_guard<std::mutex> l(mu);
      lastError = j.empty() ? tl("Không kết nối được") : tl("Dữ liệu lỗi");
    }
    busy = false;
  }).detach();
}
bool updating() { return busy; }

// one line for the home / standby screens, e.g. "☀ 28° Trời quang · Hà Nội"
std::string summary(bool withPlace) {
  Data d = get();
  if (!d.ok) return "";
  return codeIcon(d.code, d.isDay) + " " + deg(d.t) + " " + codeText(d.code) + (withPlace ? " · " + place() : "");
}
std::string ago(time_t t) {
  long s = (long)(time(nullptr) - t);
  if (s < 90) return tl("vừa xong");
  if (s < 3600) return std::to_string(s / 60) + tl(" phút trước");
  if (s < 86400) return std::to_string(s / 3600) + tl(" giờ trước");
  return std::to_string(s / 86400) + tl(" ngày trước");
}

// ---- city search ----
struct Place { std::string name, region, lat, lon; };
static std::vector<Place> search(const std::string& q) {
  std::string j = fetchUrl("https://geocoding-api.open-meteo.com/v1/search?count=10&language=vi&format=json&name=" + urlEnc(q));
  std::vector<Place> out;
  size_t p = 0;
  while ((p = j.find("{\"id\":", p)) != std::string::npos) {
    size_t e = j.find('}', p);
    std::string o = j.substr(p, e - p + 1);
    char la[24], lo[24];
    snprintf(la, sizeof la, "%.4f", num(o, "latitude"));
    snprintf(lo, sizeof lo, "%.4f", num(o, "longitude"));
    std::string region = str(o, "admin1"), country = str(o, "country");
    out.push_back({str(o, "name"), region + (region.empty() || country.empty() ? "" : ", ") + country, la, lo});
    p = e;
  }
  return out;
}
void chooseLocation() {
  push(OSK(tl("Tìm thành phố (vd: Đà Nẵng)"), "", false, [](const std::string& q) {
    if (trim(q).empty()) return;
    auto res = std::make_shared<std::vector<Place>>();
    runTask(tl("Tìm địa điểm"), {q}, [q, res](std::string& err) {
      *res = search(q);
      if (res->empty()) { err = tl("Không tìm thấy \"") + q + "\""; return false; }
      return true;
    }, [res] {
      if (res->empty()) return;
      push(new ListScreen(tl("Chọn địa điểm"), [res] {
        Items v;
        for (auto& pl : *res) {
          Item it; it.label = pl.name; it.sub = pl.region; it.icon = "sun"; it.color = hex("f9a825"); it.okLabel = tl("Chọn");
          Place c = pl;
          it.ok = [c] {
            S.set("wxName", c.name); S.set("wxLat", c.lat); S.set("wxLon", c.lon);
            refresh(true);
            pop();                                                        // the results list
            if (top() && top()->title() == tl("Tìm địa điểm")) pop();         // the finished search task under it
            toast(tl("Thời tiết: ") + c.name, "check");
          };
          v.push_back(it);
        }
        return v;
      }, "sun"));
    });
  }));
}

static std::string dayName(const std::string& date, int idx) {
  if (idx == 0) return tl("Hôm nay");
  struct tm tm = {};
  if (sscanf(date.c_str(), "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) return date;
  tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_hour = 12;
  mktime(&tm);
  static const char* n[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
  return std::string(n[tm.tm_wday]) + " " + std::to_string(tm.tm_mday) + "/" + std::to_string(tm.tm_mon + 1);
}

// ---- the app ----
struct WeatherScr : Screen {
  WeatherScr() { ic = "sun"; live = true; idleFps = 1; refresh(false); }
  std::string title() override { return tl("Thời tiết · ") + place(); }
  void draw(const SDL_Rect& a) override {
    const Theme& t = theme();
    Data d = get();
    if (!d.ok) {
      G.icon("sun", a.x + a.w / 2 - 24, a.y + a.h / 2 - 70, 48, {t.sub.r, t.sub.g, t.sub.b, 140});
      std::string m;
      { std::lock_guard<std::mutex> l(mu); m = lastError; }
      G.text(updating() ? tl("Đang tải thời tiết…") : m.empty() ? tl("Chưa có dữ liệu") : m, a.x + a.w / 2, a.y + a.h / 2 - 8, 18, t.sub, false, 1);
      G.text(tl("A: cập nhật · X: đổi địa điểm"), a.x + a.w / 2, a.y + a.h / 2 + 22, 14, t.sub, false, 1);
      return;
    }
    // current
    int x = a.x + 16, y = a.y + 8;
    G.round(a.x + 8, y, a.w - 16, 118, {0, 0, 0, 18}, 12);
    G.text(codeIcon(d.code, d.isDay), x + 44, y + 14, 64, t.a2, false, 1);
    G.text(deg(d.t), x + 96, y + 2, 64, t.txt, true);
    int tw = G.textW(deg(d.t), 64, true);
    G.text(fahrenheit() ? "F" : "C", x + 98 + tw, y + 14, 22, t.sub, true);
    G.text(codeText(d.code), x + 96, y + 80, 18, t.txt, true);
    int rx = a.x + a.w - 24;
    G.text(tl("Cảm giác ") + deg(d.feels), rx, y + 12, 15, t.sub, false, 2);
    G.text(tl("Độ ẩm ") + std::to_string(d.hum) + "%", rx, y + 36, 15, t.sub, false, 2);
    char wb[32]; snprintf(wb, sizeof wb, tl("Gió %.0f km/h"), d.wind);
    G.text(wb, rx, y + 60, 15, t.sub, false, 2);
    G.text(updating() ? tl("Đang cập nhật…") : tl("Cập nhật ") + ago(d.fetched), rx, y + 86, 13, t.sub, false, 2);
    // next 24 h, every 3 hours
    y += 126;
    int cells = 8, cw = (a.w - 16) / cells;
    for (int i = 0; i < cells && i * 3 < (int)d.hours.size(); i++) {
      const Hour& h = d.hours[i * 3];
      int cx = a.x + 8 + i * cw + cw / 2;
      std::string hh = h.time.size() >= 13 ? h.time.substr(11, 2) + "h" : h.time;
      G.text(i == 0 ? tl("Bây giờ") : hh, cx, y, 13, t.sub, false, 1);
      G.text(codeIcon(h.code), cx, y + 17, 20, t.a2, false, 1);
      G.text(deg(h.t), cx, y + 44, 15, t.txt, true, 1);
      if (h.rain >= 20) G.text(std::to_string(h.rain) + "%", cx, y + 64, 12, {30, 136, 229}, false, 1);
    }
    // 7 days
    y += 84;
    int rows = std::min((int)d.days.size(), (a.y + a.h - y) / 24);
    for (int i = 0; i < rows; i++) {
      const Day& dd = d.days[i];
      int ry = y + i * 24;
      if (i % 2 == 0) G.round(a.x + 8, ry - 2, a.w - 16, 23, {0, 0, 0, 12}, 6);
      G.text(dayName(dd.date, i), a.x + 20, ry, 15, t.txt, i == 0);
      G.text(codeIcon(dd.code), a.x + 140, ry - 1, 16, t.a2, false, 1);
      G.text(codeText(dd.code), a.x + 160, ry, 14, t.sub, false, 0, 170);
      if (dd.rain >= 20) G.text("☂ " + std::to_string(dd.rain) + "%", a.x + 400, ry, 14, {30, 136, 229}, false, 2);
      G.text(deg(dd.tmin) + " / " + deg(dd.tmax), a.x + a.w - 24, ry, 15, t.txt, true, 2);
    }
  }
  Soft soft() override { return {tl("Tùy chọn"), tl("Cập nhật"), tl("Quay lại")}; }
  void key(Key k) override {
    if (k == Key::B) pop();
    else if (k == Key::OK) refresh(true);
    else if (k == Key::A)
      openOptions({{tl("Đổi địa điểm"), "sun", [] { chooseLocation(); }},
                   {tl("Cập nhật ngay"), "restart", [] { refresh(true); }},
                   {std::string(tl("Đơn vị: ")) + (fahrenheit() ? "°F → °C" : "°C → °F"), "sort", [] {
                      S.set("wxUnit", fahrenheit() ? "C" : "F"); refresh(true); }},
                   {std::string(S.getb("wxHome", true) ? tl("Ẩn") : tl("Hiện")) + tl(" trên màn hình chính"), "list", [] { S.setb("wxHome", !S.getb("wxHome", true)); }},
                   {std::string(S.getb("wxSleep", true) ? tl("Ẩn") : tl("Hiện")) + tl(" trên màn hình chờ"), "moon", [] { S.setb("wxSleep", !S.getb("wxSleep", true)); }},
                   {tl("Nguồn dữ liệu: Open-Meteo"), "info", [] { infoBox(tl("Nguồn dữ liệu"), {{tl("Dịch vụ"), "open-meteo.com"}, {tl("Giấy phép"), "CC BY 4.0"}}); }}},
                  tl("Thời tiết"));
  }
};

}  // namespace weather

Screen* WeatherApp() { return new weather::WeatherScr; }
std::string weatherLine(bool withPlace) { weather::refresh(false); return weather::summary(withPlace); }

}  // namespace s40
