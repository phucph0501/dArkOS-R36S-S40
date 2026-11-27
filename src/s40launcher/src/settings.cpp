// Settings pages and the Apps hub (utilities + dArkOS tools). Settings apply to the real system on the device.
#include "s40.h"
#include "s40kb.h"
#include <algorithm>
#include <fstream>
#include <cmath>

namespace s40 {

Screen* SleepScreen();
Screen* CalendarApp();
Screen* CalcApp();
Screen* StopwatchApp();
Screen* NotesApp();
Screen* FlashlightApp();
Screen* KeyTestApp();
Screen* RemindersApp();
Screen* DayCountApp();
Screen* WifiScreen();

// /home/ark/.config/s40power.cfg: read by the standby hook and the LED daemon (s40launcher/system)
static void writePowerCfg() {
  const char* home = getenv("HOME");
  std::string p = std::string(home ? home : "/tmp") + "/.config/s40power.cfg";
  std::string led = S.get("led", tl("Theo pin")), sl = S.get("sleepLed", tl("Như lúc thức"));
  FILE* f = fopen(p.c_str(), "w");
  if (!f) return;
  fprintf(f, "led=%s\nsleepled=%s\nwifisleep=%d\nwakepower=%d\n", led == tl("Xanh") ? "green" : led == tl("Đỏ") ? "red" : "battery",
          sl == tl("Xanh") ? "green" : sl == tl("Đỏ") ? "red" : "same", S.getb("wifiSleep", true) ? 1 : 0, S.getb("wakePower", true) ? 1 : 0);
  fclose(f);
}
// last sleeps from the standby hook's log: "start end batteryBefore batteryAfter" per line
static std::string sleepStats(std::string* detail) {
  const char* home = getenv("HOME");
  std::ifstream f(std::string(home ? home : "/tmp") + "/.config/s40-sleep.log");
  long a, b; int c0, c1;
  double hours = 0, drop = 0, lastH = -1; int lastDrop = 0;
  while (f >> a >> b >> c0 >> c1) {
    if (b <= a) continue;
    lastH = (b - a) / 3600.0; lastDrop = c0 - c1;
    if (lastH >= 0.5) { hours += lastH; drop += std::max(0, c0 - c1); }  // short naps say little
  }
  if (lastH < 0) { if (detail) *detail = tl("Chưa có dữ liệu (bấm nút nguồn để ngủ)"); return "—"; }
  char t[96];
  snprintf(t, sizeof t, tl("Lần gần nhất: %.1f giờ, tụt %d%%"), lastH, lastDrop);
  if (detail) *detail = t;
  if (drop < 2 || hours < 2) return tl("Đang đo…");
  snprintf(t, sizeof t, tl("≈ %.1f ngày"), hours * 100.0 / drop / 24.0);  // full battery in standby
  return t;
}

// time zones offered in Settings > Ngày & giờ: label shown, zoneinfo name
static const std::vector<std::pair<std::string, std::string>> TZONES = {
    {tl("Việt Nam (GMT+7)"), "Asia/Ho_Chi_Minh"}, {tl("Trung Quốc (GMT+8)"), "Asia/Shanghai"}, {tl("Nhật Bản (GMT+9)"), "Asia/Tokyo"},
    {tl("Hàn Quốc (GMT+9)"), "Asia/Seoul"}, {tl("Úc - Sydney"), "Australia/Sydney"}, {"Anh - London", "Europe/London"},
    {tl("Pháp / Đức"), "Europe/Paris"}, {tl("Mỹ - New York"), "America/New_York"}, {tl("Mỹ - Los Angeles"), "America/Los_Angeles"},
    {tl("Giờ quốc tế (UTC)"), "UTC"}};
static Items homeTextItems();  // home screen texts (defined with the first-start wizard)
static Item tzItem();
void applySetting(const std::string& k) {
  if (k == "led" || k == "sleepLed" || k == "wifiSleep" || k == "wakePower") {
    writePowerCfg();
    if (k == "led" && isDevice()) run("sudo systemctl restart batt_led.service >/dev/null 2>&1 &");
  }
  if (k == "vol") sys::setVolume(S.geti("vol", 6));
  else if (k == "bri") sys::setBrightness(S.geti("bri", 8));
  else if (k == "wifi") sys::wifiPower(S.getb("wifi", true));
  else if (k == "theme") G.flushTextCache();
  else if (k == "tzName") {  // the label shown in Settings -> the zone stored as "tz"
    for (auto& z : TZONES) if (z.first == S.get("tzName")) S.set("tz", z.second);
  }
  else if (k == "tz") {
    std::string z = S.get("tz", "Asia/Ho_Chi_Minh");
    if (z.find("..") != std::string::npos || !fileExists("/usr/share/zoneinfo/" + z)) return;
    if (isDevice()) run("sudo ln -sf /usr/share/zoneinfo/" + z + " /etc/localtime && echo " + z + " | sudo tee /etc/timezone >/dev/null");
    setenv("TZ", z.c_str(), 1);  // glibc keeps the zone it read first: switch this process (and games it starts)
    tzset();
  }
  else if (k == "remote") {
    // SSH + Samba (file sharing), off by default like stock dArkOS; kept across reboots once enabled
    bool on = S.getb("remote", false);
    if (isDevice())
      run(std::string("sudo systemctl ") + (on ? "enable --now" : "disable --now") + " ssh.service smbd.service nmbd.service >/dev/null 2>&1 &");
    toast(on ? "SSH: ark@" + sys::ipAddr() + tl(" (mật khẩu ark)") : tl("Đã tắt dịch vụ từ xa"), on ? "lock" : "info");
  }
}

// ---------- setting item builders ----------
static Item tog(const std::string& label, const std::string& key, bool def, const std::string& icon, const char* col, const std::string& sub = "") {
  Item it; it.label = label; it.icon = icon; it.color = hex(col);
  bool v = S.getb(key, def);
  it.right = v ? tl("Bật") : tl("Tắt"); it.sub = sub.empty() ? tl("◀ ▶ để thay đổi") : sub; it.okLabel = tl("Đổi");
  it.adj = [key, def](int) { S.setb(key, !S.getb(key, def)); };
  it.ok = [key, def] { S.setb(key, !S.getb(key, def)); };
  return it;
}
static Item choice(const std::string& label, const std::string& key, std::vector<std::string> opts, const std::string& def, const std::string& icon, const char* col, const std::string& sub = "") {
  Item it; it.label = label; it.icon = icon; it.color = hex(col);
  std::string v = S.get(key, def);
  it.right = v; it.sub = sub.empty() ? tl("◀ ▶ để thay đổi") : sub; it.okLabel = tl("Chọn");
  it.adj = [key, opts, def](int d) {
    int n = (int)opts.size(), i = (int)(std::find(opts.begin(), opts.end(), S.get(key, def)) - opts.begin());
    if (i >= n) i = 0;
    S.set(key, opts[(i + d + n) % n]);
  };
  it.ok = [label, key, opts, def] {
    int i = (int)(std::find(opts.begin(), opts.end(), S.get(key, def)) - opts.begin());
    openPicker(label, opts, i, [key, opts](int k) { S.set(key, opts[k]); });
  };
  return it;
}
static Item range(const std::string& label, const std::string& key, int mn, int mx, int def, const std::string& icon, const char* col) {
  Item it; it.label = label; it.icon = icon; it.color = hex(col);
  it.bar = S.geti(key, def) - (mn == 1 ? 0 : mn); it.barMax = mx - (mn == 1 ? 0 : mn); it.okLabel = tl("Tăng");
  it.adj = [key, mn, mx, def](int d) { S.seti(key, std::max(mn, std::min(mx, S.geti(key, def) + d))); };
  it.ok = [key, mn, mx, def] { S.seti(key, std::max(mn, std::min(mx, S.geti(key, def) + 1))); };
  return it;
}
static Item act(const std::string& label, const std::string& sub, const std::string& icon, const char* col, std::function<void()> f, const std::string& right = "") {
  Item it; it.label = label; it.sub = sub; it.icon = icon; it.color = hex(col); it.ok = std::move(f); it.right = right; return it;
}
static Item page(const std::string& label, const std::string& sub, const std::string& icon, const char* col, std::function<Screen*()> f) {
  Item it = act(label, sub, icon, col, [f] { push(f()); }, "›"); it.okLabel = tl("Mở"); return it;
}
static Screen* Page(const std::string& title, const std::string& icon, std::function<Items()> b) {
  auto* l = new ListScreen(title, std::move(b), icon);
  return l;
}
// run a shell command as a task with progress screen
static void shellTask(const std::string& title, std::vector<std::string> steps, const std::string& cmd, std::function<void()> done = nullptr) {
  runTask(title, std::move(steps), [cmd](std::string& err) {
    if (!isDevice()) { SDL_Delay(1500); return true; }
    int rc = run(cmd + " >/tmp/s40-task.log 2>&1");
    if (rc != 0) err = trim(sh("tail -n 1 /tmp/s40-task.log")).substr(0, 70);
    return rc == 0;
  }, std::move(done));
}

// ---------- pages ----------
Screen* SoundPage() {
  return Page(tl("Âm thanh"), "volume", [] {
    return Items{range(tl("Âm lượng"), "vol", 0, 10, 6, "volume", "1e88e5"),
                 choice(tl("Đầu ra âm thanh"), "audioOut", {tl("Tự động"), tl("Loa"), tl("Tai nghe")}, tl("Tự động"), "volume", "1976d2", tl("Tự động = chuyển sang tai nghe khi cắm")),
                 tog(tl("Âm phím"), "keytone", false, "key", "00897b"),
                 act(tl("Sửa lỗi âm thanh"), tl("Khôi phục cấu hình ALSA mặc định"), "restart", "ef6c00", [] {
                   shellTask(tl("Sửa lỗi âm thanh"), {tl("Khôi phục .asoundrc"), tl("Đặt lại đường xuất âm thanh")}, "cp -f /home/ark/.asoundrcbak /home/ark/.asoundrc");
                 })};
  });
}
Screen* DisplayPage() {
  return Page(tl("Hiển thị & giao diện"), "sun", [] {
    std::vector<std::string> th; for (auto& t : THEMES) th.push_back(t.name);
    Item theme = choice(tl("Chủ đề màu"), "themeName", th, THEMES[S.geti("theme", 0)].name, "palette", "8e24aa");
    theme.adj = [th](int d) { int n = (int)th.size(); S.seti("theme", (S.geti("theme", 0) + d + n) % n); };
    theme.ok = [th] { openPicker(tl("Chủ đề màu"), th, S.geti("theme", 0), [](int i) { S.seti("theme", i); }); };
    theme.right = THEMES[S.geti("theme", 0)].name;
    static const std::vector<std::pair<std::string, std::pair<const char*, const char*>>> WALLS = {
        {tl("Bầu trời"), {"1e3c72", "6dd5ed"}}, {tl("Hoàng hôn"), {"41295a", "f39c12"}}, {tl("Rừng"), {"0f3d2e", "a8e063"}}, {tl("Đêm"), {"0b0f1e", "3a4a7a"}}};
    std::vector<std::string> wn; for (auto& w : WALLS) wn.push_back(w.first);
    int curWall = -1;  // the preset in use (none when a photo is the wallpaper)
    if (S.get("wallpaper").empty())
      for (size_t i = 0; i < WALLS.size(); i++) if (S.get("wallTop", "1e3c72") == WALLS[i].second.first) curWall = (int)i;
    Item wall = act(tl("Hình nền"), tl("Màu nền, hoặc một ảnh trên thẻ nhớ"), "photo", "43a047", [wn, curWall] {
      auto opts = wn;
      opts.push_back(tl("Chọn ảnh trên thẻ nhớ…"));
      openPicker(tl("Hình nền"), opts, curWall, [](int i) {
        if (i < (int)WALLS.size()) { S.set("wallpaper", ""); S.set("wallTop", WALLS[i].second.first); S.set("wallBot", WALLS[i].second.second); return; }
        auto ph = photoFiles();
        if (ph.empty()) { toast(tl("Chưa có ảnh: chép ảnh vào thư mục photos"), "photo"); return; }
        std::vector<std::string> names;
        for (auto& p : ph) names.push_back(p.substr(p.find_last_of('/') + 1));
        openPicker(tl("Chọn ảnh làm hình nền"), names, -1, [ph](int k) { setWallpaper(ph[k]); });
      });
    }, !S.get("wallpaper").empty() ? tl("Ảnh") : curWall >= 0 ? WALLS[curWall].first : tl("Màu"));
    Items v{range(tl("Độ sáng"), "bri", 1, 10, 8, "sun", "f9a825"), theme, wall};
    for (auto& it : homeTextItems()) v.push_back(it);
    for (auto& it : Items{choice(tl("Hiệu ứng chuyển màn hình"), "transition", {tl("Trượt"), tl("Tức thì")}, tl("Trượt"), "next", "0277bd"),
                          tog(tl("Hiện phần trăm pin"), "showBat", true, "bolt", "2e7d32"),
                          tog(tl("Hiện gợi ý phím"), "showHelp", true, "key", "00897b"),
                          tog(tl("Kiểu menu danh sách"), "menuList", false, "list", "6d4c41")}) v.push_back(it);
    return v;
  });
}
Screen* NetPage() {
  return Page(tl("Wi-Fi & mạng"), "wifi", [] {
    std::string ssid = sys::wifiSsid();
    Items v{tog("Wi-Fi", "wifi", true, "wifi", "3949ab", S.getb("wifi", true) ? (ssid.empty() ? tl("Chưa kết nối") : tl("Đã kết nối: ") + ssid) : tl("Đang tắt"))};
    Item scan = act(tl("Kết nối mạng"), tl("Quét mạng xung quanh"), "plus", "1e88e5", [] { push(WifiScreen()); });
    scan.dis = !S.getb("wifi", true);
    if (scan.dis) scan.ok = [] { toast(tl("Hãy bật Wi-Fi trước"), "wifioff"); };
    v.push_back(scan);
    v.push_back(act(tl("Thông tin mạng"), "", "info", "607d8b", [ssid] { infoBox(tl("Thông tin mạng"), {{tl("Mạng"), ssid.empty() ? "—" : ssid}, {tl("Địa chỉ IP"), sys::ipAddr()}}); }));
    bool remote = S.getb("remote", false);
    v.push_back(tog(tl("Dịch vụ từ xa (SSH, Samba)"), "remote", false, "lock", "455a64",
                    remote ? "ssh ark@" + sys::ipAddr() + " · \\\\" + sys::ipAddr() : tl("Truy cập tệp và dòng lệnh qua mạng")));
    return v;
  });
}
Screen* WifiScreen() {
  auto nets = std::make_shared<std::vector<sys::Net>>(sys::wifiScan());
  auto* l = new ListScreen(tl("Mạng Wi-Fi"), [nets] {
    Items v;
    std::string cur = sys::wifiSsid();
    for (auto& n : *nets) {
      Item it; it.label = n.ssid; it.sub = std::string(n.secure ? tl("Bảo mật") : tl("Mạng mở")) + (n.ssid == cur ? tl(" · Đang kết nối") : "");
      it.icon = n.secure ? "lock" : "wifi"; it.color = hex(n.ssid == cur ? "43a047" : "3949ab"); it.right = std::to_string(n.signal) + "%"; it.okLabel = tl("Kết nối");
      std::string ssid = n.ssid; bool sec = n.secure;
      it.ok = [ssid, sec] {
        auto go = [ssid](const std::string& pw) {
          runTask(tl("Kết nối ") + ssid, {tl("Đang kết nối tới ") + ssid, tl("Xác thực"), tl("Nhận địa chỉ IP")}, [ssid, pw](std::string& e) { return sys::wifiConnect(ssid, pw, e); });
        };
        if (sec) push(OSK(tl("Mật khẩu: ") + ssid, "", true, [go](const std::string& pw) { if (pw.size() >= 8) go(pw); else toast(tl("Mật khẩu cần ít nhất 8 ký tự"), "warn"); }));
        else go("");
      };
      v.push_back(it);
    }
    return v;
  }, "wifi");
  l->empty = tl("Không tìm thấy mạng nào.");
  l->opts = [nets](const Item& it, int) { return std::vector<Opt>{{tl("Kết nối"), "wifi", it.ok}, {tl("Quét lại"), "restart", [nets] { *nets = sys::wifiScan(); }}}; };
  return l;
}
Screen* PowerPage() {
  return Page(tl("Pin & hiệu năng"), "bolt", [] {
    return Items{act(tl("Tình trạng pin"), std::string(sys::charging() ? tl("Đang sạc") : tl("Đang dùng pin")), "bolt", "2e7d32", [] {}, std::to_string(sys::battery()) + "%"),
                 choice(tl("Chế độ hiệu năng"), "perf", {tl("Tiết kiệm pin"), tl("Cân bằng"), tl("Hiệu năng tối đa")}, tl("Cân bằng"), "chip", "e65100", tl("Xung CPU khi chạy game")),
                 choice(tl("Tự ngủ khi không dùng"), "suspend", {tl("Tắt"), tl("5 phút"), tl("10 phút"), tl("15 phút"), tl("30 phút")}, tl("15 phút"), "moon", "3949ab", tl("Hiện màn hình chờ (đồng hồ)")),
                 choice(tl("Ngủ sâu sau màn hình chờ"), "deepSleep", {tl("Ngay"), tl("1 phút"), tl("5 phút"), tl("15 phút"), tl("Không")}, tl("1 phút"), "moon", "1a237e",
                        tl("Tắt hẳn như bấm nút nguồn (trừ khi đang nghe nhạc)")),
                 tog(tl("Tắt Wi-Fi khi ngủ"), "wifiSleep", true, "wifioff", "3949ab", tl("Tiết kiệm pin, tự bật lại khi thức")),
                 tog(tl("Chỉ nút nguồn đánh thức"), "wakePower", true, "power", "c62828", tl("Cấn phím trong túi không làm máy dậy")),
                 choice(tl("Đèn báo"), "led", {tl("Theo pin"), tl("Xanh"), tl("Đỏ")}, tl("Theo pin"), "bolt", "43a047", tl("Theo pin: xanh, đỏ khi dưới 20%, nháy khi dưới 10%")),
                 choice(tl("Đèn khi ngủ"), "sleepLed", {tl("Như lúc thức"), tl("Xanh"), tl("Đỏ")}, tl("Như lúc thức"), "moon", "e53935"),
                 [] { std::string d; std::string v = sleepStats(&d); return act(tl("Thời gian chờ ước tính"), d, "timer", "00897b", [] {}, v); }(),
                 tog(tl("Hiện đồng hồ khi ngủ"), "sleepClock", true, "recent", "263238", tl("Đồng hồ lớn trên nền đen")),
                 act(tl("Xem thử màn hình ngủ"), "", "moon", "37474f", [] { push(SleepScreen()); })};
  });
}
Screen* GamePage() {
  return Page(tl("Trò chơi & giả lập"), "game", [] {
    Item hide = act(tl("Ẩn / hiện hệ máy"), "", "list", "6d4c41", [] {
      auto& ss = systems();
      std::vector<std::string> names; std::vector<int> chosen;
      auto hidden = split(S.get("hidden"), ',');
      for (int i = 0; i < (int)ss.size(); i++) { names.push_back(ss[i].fullname); if (std::find(hidden.begin(), hidden.end(), ss[i].name) != hidden.end()) chosen.push_back(i); }
      openMulti(tl("Chọn hệ máy cần ẩn"), names, chosen, [](std::vector<int> ch) {
        std::string s; for (int i : ch) { if (!s.empty()) s += ','; s += systems()[i].name; }
        S.set("hidden", s); toast(tl("Đang ẩn ") + std::to_string(ch.size()) + tl(" hệ máy"), "check");
      });
    });
    return Items{page("RetroAchievements", tl("Thành tích, Hardcore, bảng xếp hạng"), "star", "f5a300", RetroAchievementsPage),
                 tog(tl("Hiện game yêu thích lên đầu"), "favFirst", false, "star", "f5a300"),
                 choice(tl("Sắp xếp hệ máy"), "sortSys", {tl("Mặc định"), tl("Theo tên A → Z")}, tl("Mặc định"), "sort", "546e7a"), hide,
                 act(tl("Quét lại thẻ nhớ"), tl("Tìm ROM mới chép vào"), "restart", "00897b", [] { systems(true); toast(tl("Đã quét: ") + std::to_string(systems().size()) + tl(" hệ máy"), "check"); }),
                 // dArkOS's own switch scripts: SD2 gets the folder layout, PortMaster + runtimes (from
                 // /roms.tar) and the ports of card 1; they restart the launcher when done
                 romsBase() == "/roms2"
                     ? act(tl("Thẻ nhớ ROM: thẻ 2 (khe TF2)"), tl("Chuyển về dùng thẻ 1 cho ROM"), "usb", "6d4c41", [] {
                         confirmBox(tl("Dùng lại thẻ 1 cho ROM?\nMáy sẽ tải lại giao diện."), [] {
                           sys::launchExternal("sudo bash '/usr/local/bin/Switch to Main SD for Roms.sh'"); systems(true); }, "usb"); }, tl("Thẻ 2"))
                     : act(tl("Thẻ nhớ ROM: thẻ 1"), tl("Cắm thẻ 2 vào khe TF2 rồi chọn để dùng thẻ 2 cho ROM"), "usb", "6d4c41", [] {
                         confirmBox(tl("Dùng thẻ 2 (khe TF2) cho ROM?\nCấu trúc thư mục, PortMaster và runtime được chép sang thẻ 2 (dữ liệu sẵn có trên thẻ 2 được giữ)."), [] {
                           sys::launchExternal("sudo bash '/usr/local/bin/Switch to SD2 for Roms.sh'"); systems(true); }, "usb"); }, tl("Thẻ 1")),
                 act(tl("Xóa lịch sử chơi"), "", "trash", "c62828", [] { confirmBox(tl("Xóa toàn bộ lịch sử chơi?"), [] { S.set("recent", ""); }, "trash"); })};
  });
}
Screen* TimePage() {
  return Page(tl("Ngày & giờ"), "recent", [] {
    Item h24 = tog(tl("Định dạng 24 giờ"), "h24", true, "recent", "546e7a");
    return Items{h24, tzItem(),
                 choice(tl("Định dạng ngày"), "dateFmt", {"dd/mm/yyyy", "mm/dd/yyyy", "yyyy-mm-dd"}, "dd/mm/yyyy", "calendar", "607d8b"),
                 tog(tl("Hiện ngày âm lịch"), "showLunar", true, "moon", "c62828")};
  });
}
// R36S model (device tree) chosen from /boot/dtb/<r36s|clone|soysauce>/<model>/ by /boot/chon_doi_may.sh
static std::string currentModel() {
  std::string m = trim(sh("cat /boot/doi_may.txt 2>/dev/null"));
  return m.empty() ? tl("Mặc định") : m;
}
static Screen* ModelPage() {
  return Page(tl("Đời máy R36S"), "chip", [] {
    Items v;
    std::string cur = currentModel();
    for (const char* group : {"r36s", "clone", "soysauce"}) {
      for (auto& m : listDir(std::string("/boot/dtb/") + group)) {
        if (!fileExists(std::string("/boot/dtb/") + group + "/" + m + "/rk3326-r36s-linux.dtb")) continue;
        Item it; it.label = m; it.sub = std::string(group) == "r36s" ? "R36S" : std::string(group) == "clone" ? tl("Bản clone") : "Soy sauce";
        it.icon = "chip"; it.color = hex(m == cur ? "2e7d32" : "607d8b"); it.right = m == cur ? tl("Đang dùng") : ""; it.okLabel = tl("Chọn");
        it.ok = [m] {
          confirmBox(tl("Dùng cấu hình \"") + m + tl("\"?\nMáy sẽ khởi động lại. Nếu màn hình không lên, cắm thẻ vào máy tính và chạy CHON_DOI_MAY.bat."), [m] {
            if (run("sudo bash /boot/chon_doi_may.sh '" + m + "' >/dev/null 2>&1") == 0) sys::reboot();
            else toast(tl("Không chép được cấu hình"), "warn");
          }, "chip");
        };
        v.push_back(it);
      }
    }
    return v;
  });
}
Screen* AboutPage() {
  return Page(tl("Thông tin máy"), "info", [] {
    auto row = [](const std::string& a, const std::string& b) { Item it; it.label = a; it.right = b; return it; };
    std::string kernel = trim(sh("uname -r"));
    Item model = page(tl("Đời máy R36S"), currentModel(), "chip", "6d4c41", ModelPage);
    return Items{model, row(tl("Hệ điều hành"), "dArkOS (Debian trixie)"), row(tl("Giao diện"), "S40 launcher"), row(tl("Nhân Linux"), kernel.empty() ? "—" : kernel),
                 row(tl("Pin"), std::to_string(sys::battery()) + "%"), row(tl("Địa chỉ IP"), sys::ipAddr()), row(tl("Dung lượng ROM"), trim(sh("df -h " + romsBase() + " | awk 'NR==2{print $3\"/\"$2}'"))),
                 [] {
                   Item it; it.label = tl("Ghi công phần mềm"); it.sub = tl("Thành phần bên thứ ba và tác giả"); it.icon = "info"; it.color = hex("607d8b"); it.right = "›";
                   it.ok = [] {
                     infoBox(tl("Ghi công"), {{tl("Hệ điều hành"), "dArkOS (christianhaitian)"}, {tl("Trình duyệt"), "WPE WebKit"}, {tl("Thời tiết"), "Open-Meteo (CC BY 4.0)"},
                                          {tl("Lọc quảng cáo"), "EasyList, ABPVN, uBlock"}, {tl("Chữ Hán / Pinyin"), "WenQuanYi, Unicode Unihan"}});
                   };
                   return it;
                 }()};
  });
}
// ---------- keyboard (shared with the browser: ~/.config/s40kb.cfg, see common/s40kb.h) ----------
static s40kb::Config g_kbc;
static std::string kbSummary() {
  std::string s;
  for (auto& l : g_kbc.langs()) s += (s.empty() ? "" : " · ") + s40kb::langLabel(l) + (l == "vi" ? (g_kbc.viMethod == "vni" ? " VNI" : " Telex") : "");
  return s;
}
static Screen* KeyboardPage() {
  g_kbc.load();
  return Page(tl("Bàn phím"), "key", [] {
    auto& c = g_kbc;
    auto langItem = [](const std::string& l, const char* col, const std::string& sub) {
      Item it; it.label = s40kb::langName(l); it.icon = "key"; it.color = hex(col); it.sub = sub; it.okLabel = tl("Đổi");
      it.right = g_kbc.enabled(l) ? tl("Bật") : tl("Tắt");
      auto flip = [l] {
        auto& c = g_kbc;
        bool* p = l == "vi" ? &c.vi : l == "en" ? &c.en : &c.zh;
        if (*p && c.langs().size() <= 1) { toast(tl("Cần bật ít nhất một ngôn ngữ"), "warn"); return; }
        *p = !*p;
        if (!c.enabled(c.lang)) c.lang = c.langs()[0];
        c.save();
      };
      it.adj = [flip](int) { flip(); };
      it.ok = flip;
      return it;
    };
    Items v{langItem("vi", "c62828", tl("Gõ có dấu: Telex hoặc VNI")), langItem("en", "1e88e5", tl("Gõ thường, không đổi chữ")),
            langItem("zh", "e53935", tl("Gõ pinyin rồi chọn chữ Hán, ví dụ nihao → 你好"))};
    Item def = act(tl("Ngôn ngữ khi mở bàn phím"), tl("Nút L trên bàn phím để đổi nhanh"), "key", "6a1b9a", [] {
      auto ls = g_kbc.langs();
      std::vector<std::string> names;
      for (auto& l : ls) names.push_back(s40kb::langName(l));
      int cur = (int)(std::find(ls.begin(), ls.end(), g_kbc.lang) - ls.begin());
      openPicker(tl("Ngôn ngữ mặc định"), names, cur, [ls](int i) { g_kbc.lang = ls[i]; g_kbc.save(); });
    }, s40kb::langName(c.lang));
    v.push_back(def);
    if (c.vi) {
      Item m = act(tl("Kiểu gõ tiếng Việt"), c.viMethod == "vni" ? tl("VNI: 1-5 dấu thanh, 6 â ê ô, 7 ơ ư, 8 ă, 9 đ, 0 bỏ dấu")
                                                           : tl("Telex: s f r x j dấu thanh, aa ee oo â ê ô, w ư ơ ă, dd đ, z bỏ dấu"),
                   "note", "2e7d32", [] { g_kbc.viMethod = g_kbc.viMethod == "vni" ? "telex" : "vni"; g_kbc.save(); },
                   c.viMethod == "vni" ? "VNI" : "Telex");
      m.adj = [](int) { g_kbc.viMethod = g_kbc.viMethod == "vni" ? "telex" : "vni"; g_kbc.save(); };
      m.okLabel = tl("Đổi");
      v.push_back(m);
      Item t = act(tl("Vị trí dấu"), tl("oà, uý (kiểu mới) hay òa, úy (kiểu cũ)"), "note", "00897b",
                   [] { g_kbc.newToneStyle = !g_kbc.newToneStyle; g_kbc.save(); }, c.newToneStyle ? tl("Kiểu mới (hoà)") : tl("Kiểu cũ (hòa)"));
      t.adj = [](int) { g_kbc.newToneStyle = !g_kbc.newToneStyle; g_kbc.save(); };
      t.okLabel = tl("Đổi");
      v.push_back(t);
    }
    v.push_back(act(tl("Gõ thử"), tl("Mở bàn phím để thử ngôn ngữ và kiểu gõ"), "key", "f57c00", [] {
      push(OSK(tl("Gõ thử"), "", false, [](const std::string& s) {
        g_kbc.load();  // the language may have been switched with L
        infoBox(tl("Bạn đã gõ"), {{tl("Nội dung"), s.empty() ? tl("(trống)") : s}});
      }));
    }));
    v.push_back(act(tl("Cách dùng bàn phím"), "", "info", "607d8b", [] {
      infoBox(tl("Bàn phím ảo"), {{"A", tl("Gõ phím đang chọn")}, {"X", tl("Xong")}, {"B", tl("Xóa lùi (trống thì thoát)")},
                              {"L", tl("Đổi ngôn ngữ VI / EN / 中")}, {"R", tl("Trang ký hiệu")}, {"⇧", tl("Chữ hoa (bấm 2 lần: khóa hoa)")},
                              {tl("中"), tl("Lên trên hàng phím để chọn chữ Hán; Cách = chữ đầu")}});
    }));
    return v;
  });
}

// ---------- home screen texts ----------
// stored with a "...Set" flag so an empty text (line hidden) is not mistaken for "never set"
std::string homeTitle() { return S.get("homeTitleSet") == "1" ? S.get("homeTitle") : "dArkOS"; }
std::string homeSub() { return S.get("homeSubSet") == "1" ? S.get("homeSub") : "R36S · darkos"; }
static Item homeTextItem(const std::string& label, const std::string& key, size_t maxChars, std::function<std::string()> cur) {
  std::string v = cur();
  Item it = act(label, tl("Tối đa ") + std::to_string(maxChars) + tl(" ký tự, để trống để ẩn"), "note", "8e24aa", [label, key, maxChars, cur] {
    push(OSK(label, cur(), false, [key, maxChars](const std::string& s) {
      std::u32string u = s40kb::toU32(trim(s));
      if (u.size() > maxChars) { u.resize(maxChars); toast(tl("Đã cắt còn ") + std::to_string(maxChars) + tl(" ký tự"), "info"); }
      S.set(key, trim(s40kb::toU8(u)));
      S.set(key + "Set", "1");
    }));
  }, v.empty() ? tl("(ẩn)") : v);
  return it;
}
static Items homeTextItems() {
  return {homeTextItem(tl("Chữ lớn trên màn hình chính"), "homeTitle", 16, homeTitle),
          homeTextItem(tl("Dòng chữ nhỏ bên dưới"), "homeSub", 32, homeSub)};
}

// ---------- UI language (launcher, browser, keyboard, PortMaster, PPSSPP, terminal) ----------
// Labels stay in their own language (not translated) so anyone can find their way back.
static const std::vector<std::pair<std::string, std::string>> LANGS = {{"vi", "Tiếng Việt"}, {"en", "English"}, {"zh", "中文"}};
static void setLanguage(const std::string& code) {
  const char* home = getenv("HOME");
  std::string dir = std::string(home ? home : "/tmp") + "/.config/s40";
  run("mkdir -p '" + dir + "'");
  if (FILE* f = fopen((dir + "/lang").c_str(), "w")) { fprintf(f, "%s\n", code.c_str()); fclose(f); }
  // PPSSPP has its own vi/en/zh translations: set its Language in every config it may read
  std::string ppl = code == "en" ? "en_US" : code == "zh" ? "zh_CN" : "vi_VN";
  for (std::string ini : {romsBase() + "/psp/ppsspp/PSP/SYSTEM/ppsspp.ini", romsBase() + "/psp/ppsspp/PSP/SYSTEM/ppsspp.ini.sdl",
                          std::string("/opt/ppsspp/backupforromsfolder/ppsspp/PSP/SYSTEM/ppsspp.ini"),
                          std::string("/opt/ppsspp/backupforromsfolder/ppsspp/PSP/SYSTEM/ppsspp.ini.sdl")})
    if (fileExists(ini)) run("sed -i 's/^Language = .*/Language = " + ppl + "/' '" + ini + "' 2>/dev/null");
  // the new language is read when programs start: reload the launcher (PortMaster, the browser and
  // the terminal follow through LANG / the same setting)
  if (isDevice()) run("(sleep 1; sudo systemctl restart s40) >/dev/null 2>&1 &");
  else quitApp(0);
}
static Item langItem() {
  std::string cur = s40tr::lang();
  int sel = 0;
  for (size_t i = 0; i < LANGS.size(); i++) if (LANGS[i].first == cur) sel = (int)i;
  Item it = act("Ngôn ngữ / Language / 语言", "", "globe", "1565c0", [sel] {
    std::vector<std::string> names;
    for (auto& l : LANGS) names.push_back(l.second);
    openPicker("Ngôn ngữ / Language / 语言", names, sel, [sel](int k) { if (k != sel) setLanguage(LANGS[k].first); });
  }, LANGS[sel].second);
  return it;
}

// ---------- first start after installing ----------
static Item tzItem() {
  std::vector<std::string> zones;
  for (auto& z : TZONES) zones.push_back(z.first);
  return choice(tl("Múi giờ"), "tzName", zones, TZONES[0].first, "recent", "00897b", tl("Giờ tự đồng bộ qua mạng khi có Wi-Fi"));
}
Screen* SetupWizard() {
  S.setb("setupDone", true);  // asked once; everything stays changeable in Settings
  g_kbc.load();
  auto* l = (ListScreen*)Page(tl("Thiết lập ban đầu"), "settings", [] {
    Item hello; hello.label = tl("Chào bạn! Chọn vài thiết lập cơ bản"); hello.sub = tl("Đổi lại lúc nào cũng được trong Cài đặt"); hello.icon = "star";
    hello.color = hex("f5a300"); hello.dis = true;
    Items v{hello, langItem(), tzItem(), tog(tl("Định dạng 24 giờ"), "h24", true, "recent", "546e7a"),
            page(tl("Ngôn ngữ bàn phím"), kbSummary(), "key", "6a1b9a", KeyboardPage),
            page("Wi-Fi", S.getb("wifi", true) ? (sys::wifiSsid().empty() ? tl("Chưa kết nối") : sys::wifiSsid()) : tl("Tắt"), "wifi", "3949ab", NetPage),
            page(tl("Đời máy R36S"), currentModel(), "chip", "e65100", ModelPage)};
    for (auto& it : homeTextItems()) v.push_back(it);
    v.push_back(act(tl("Hoàn tất"), tl("Bắt đầu dùng máy"), "check", "2e7d32", [] { pop(); toast(tl("Đã lưu thiết lập"), "check"); }));
    return v;
  });
  l->sel = 1;  // first row is the greeting
  return l;
}

// ---------- backup & restore ----------
// Settings of the launcher, keyboard, power, RetroArch (incl. per-game overrides and RetroAchievements
// login), saved Wi-Fi networks and the time zone, into <roms>/backup/s40-backup-<date>.tar.gz.
// ROMs, saves and ports already live on the ROM card and are not part of it.
static const char* BACKUP_PATHS =
    "home/ark/.config/s40 home/ark/.config/s40kb.cfg home/ark/.config/s40power.cfg "
    "home/ark/.config/retroarch/retroarch.cfg home/ark/.config/retroarch/retroarch-core-options.cfg "
    "home/ark/.config/retroarch/config home/ark/.config/retroarch32/retroarch.cfg home/ark/.config/retroarch32/retroarch-core-options.cfg "
    "home/ark/.config/retroarch32/config etc/NetworkManager/system-connections etc/localtime etc/timezone";
static std::vector<std::string> backups() {
  std::vector<std::string> v;
  for (auto& f : listDir(romsBase() + "/backup"))
    if (f.rfind("s40-backup", 0) == 0 && f.size() > 7 && f.substr(f.size() - 7) == ".tar.gz") v.push_back(f);
  std::sort(v.rbegin(), v.rend());  // newest first (the date is in the name)
  return v;
}
static void restoreBackup(const std::string& f) {
  confirmBox(tl("Khôi phục cài đặt từ\n") + f + tl("?\nCài đặt hiện tại sẽ bị thay thế."), [f] {
    shellTask(tl("Khôi phục cài đặt"), {tl("Giải nén bản sao lưu"), tl("Nạp lại mạng")},
              "sudo tar xzf '" + romsBase() + "/backup/" + f + "' -C / && (sudo systemctl restart NetworkManager || true)", [] {
                toast(tl("Đã khôi phục, đang tải lại giao diện"), "check");
                if (isDevice()) run("(sleep 2; sudo systemctl restart s40) >/dev/null 2>&1 &");
              });
  }, "restart");
}
static Screen* BackupPage() {
  auto* l = new ListScreen(tl("Sao lưu & khôi phục"), [] {
    Items v{act(tl("Sao lưu ngay"), tl("Cài đặt, RetroArch, Wi-Fi → ") + romsBase() + "/backup", "download", "1e88e5", [] {
      shellTask(tl("Sao lưu cài đặt"), {tl("Gom cấu hình"), tl("Ghi tệp sao lưu")},
                "mkdir -p " + romsBase() + "/backup && cd / && sudo tar czf " + romsBase() + "/backup/s40-backup-$(date +%Y%m%d-%H%M).tar.gz "
                "--ignore-failed-read " + BACKUP_PATHS, [] { toast(tl("Đã sao lưu"), "check"); });
    })};
    auto bs = backups();
    if (bs.empty()) { Item e; e.label = tl("Chưa có bản sao lưu nào"); e.icon = "info"; e.dis = true; v.push_back(e); }
    for (auto& f : bs) {
      std::string d = f.substr(11, 13);  // YYYYmmdd-HHMM
      std::string when = d.size() == 13 ? d.substr(6, 2) + "/" + d.substr(4, 2) + "/" + d.substr(0, 4) + " " + d.substr(9, 2) + ":" + d.substr(11, 2) : f;
      Item it = act(tl("Bản sao lưu ") + when, f, "restart", "2e7d32", [f] { restoreBackup(f); }, tl("Khôi phục"));
      it.okLabel = tl("Khôi phục");
      v.push_back(it);
    }
    return v;
  }, "download");
  l->opts = [](const Item& it, int) {
    std::string f = it.sub;
    if (f.rfind("s40-backup", 0) != 0) return std::vector<Opt>{};
    return std::vector<Opt>{{tl("Khôi phục"), "restart", [f] { restoreBackup(f); }},
                            {tl("Xóa bản sao lưu này"), "trash", [f] {
                               confirmBox(tl("Xóa ") + f + "?", [f] { remove((romsBase() + "/backup/" + f).c_str()); toast(tl("Đã xóa"), "trash"); }, "trash");
                             }, hex("c62828")}};
  };
  return l;
}

// ---------- reset to factory data ----------
// ROMs, saves and ports on the ROM card are never touched.
static void resetLauncher() {
  S.v.clear(); S.save();  // also clears "setupDone": the first-start questions come back
}
static void resetRetroArch() {
  for (auto it = S.v.begin(); it != S.v.end();)  // per-game core / option choices
    it = (it->first.rfind("gopt.", 0) == 0 || it->first.rfind("gcore.", 0) == 0) ? S.v.erase(it) : std::next(it);
  S.save();
}
// Back to the files as dArkOS had them (the tuning installer keeps them), then the tuning is applied again; without the tuning
// installer the defaults shipped with the launcher are used. Both RetroArch builds (64 and 32 bit).
static const char* RA_RESET =
    "if [ -x /usr/local/bin/s40-tune.sh ]; then sudo /usr/local/bin/s40-tune.sh reset-retroarch; else "
    "cp /opt/s40/defaults/retroarch.cfg /home/ark/.config/retroarch/retroarch.cfg && "
    "cp /opt/s40/defaults/retroarch-core-options.cfg /home/ark/.config/retroarch/retroarch-core-options.cfg; fi; "
    "rm -rf /home/ark/.config/retroarch/config/* /home/ark/.config/retroarch32/config/* 2>/dev/null; true";
static const char* WIFI_RESET =
    "for c in $(nmcli -t -f UUID,TYPE con show | awk -F: '$2==\"802-11-wireless\"{print $1}'); do sudo nmcli con delete uuid $c; done; true";
static Screen* ResetPage() {
  auto* l = (ListScreen*)Page(tl("Đặt lại dữ liệu gốc"), "trash", [] {
    Item note; note.label = tl("ROM, save game và port trên thẻ được giữ nguyên"); note.icon = "info"; note.dis = true;
    return Items{note,
        act(tl("Cài đặt giao diện"), tl("Yêu thích, lịch sử, chủ đề, hình nền… và hỏi lại thiết lập ban đầu"), "settings", "546e7a", [] {
          confirmBox(tl("Đặt lại cài đặt giao diện?"), [] { resetLauncher(); push(SetupWizard()); }, "restart");
        }),
        act(tl("RetroArch về mặc định"), tl("Cấu hình, lõi, thiết lập riêng từng game, đăng nhập RetroAchievements"), "game", "e53935", [] {
          confirmBox(tl("Đưa RetroArch về cấu hình gốc của máy?"), [] {
            shellTask(tl("Đặt lại RetroArch"), {tl("Chép cấu hình gốc"), tl("Xóa thiết lập riêng")}, RA_RESET, [] { resetRetroArch(); toast(tl("RetroArch đã về mặc định"), "check"); });
          }, "restart");
        }),
        act(tl("Xóa mạng Wi-Fi đã lưu"), tl("Quên mọi mạng và mật khẩu đã lưu"), "wifioff", "3949ab", [] {
          confirmBox(tl("Quên tất cả mạng Wi-Fi đã lưu?"), [] {
            shellTask(tl("Xóa mạng Wi-Fi"), {tl("Xóa hồ sơ mạng")}, WIFI_RESET, [] { toast(tl("Đã xóa mạng đã lưu"), "check"); });
          }, "wifioff");
        }),
        act(tl("Đặt lại tất cả (như mới cài)"), tl("Cả ba mục trên, rồi tải lại giao diện"), "trash", "c62828", [] {
          confirmBox(tl("Đặt lại TẤT CẢ cài đặt như mới cài?\nKhông thể hoàn tác."), [] {
            shellTask(tl("Đặt lại tất cả"), {"RetroArch", "Wi-Fi", tl("Giao diện")}, std::string(RA_RESET) + "; " + WIFI_RESET, [] {
              resetLauncher();
              if (isDevice()) run("(sleep 1; sudo systemctl restart s40) >/dev/null 2>&1 &");
              else push(SetupWizard());
            });
          }, "trash");
        })};
  });
  l->sel = 1;  // first row is the note
  return l;
}

Screen* SettingsScreen() {
  g_kbc.load();
  return Page(tl("Cài đặt"), "settings", [] {
    return Items{langItem(),
                 page(tl("Âm thanh"), tl("Âm lượng ") + std::to_string(S.geti("vol", 6)) + "/10", "volume", "1e88e5", SoundPage),
                 page(tl("Hiển thị & giao diện"), tl("Độ sáng ") + std::to_string(S.geti("bri", 8)) + "/10 · " + THEMES[S.geti("theme", 0)].name, "sun", "f9a825", DisplayPage),
                 page(tl("Bàn phím"), kbSummary(), "key", "6a1b9a", KeyboardPage),
                 page(tl("Wi-Fi & mạng"), S.getb("wifi", true) ? tl("Bật") : tl("Tắt"), "wifi", "3949ab", NetPage),
                 page(tl("Pin & hiệu năng"), tl("Pin ") + std::to_string(sys::battery()) + "% · " + S.get("perf", tl("Cân bằng")), "bolt", "2e7d32", PowerPage),
                 page(tl("Trò chơi & giả lập"), tl("Yêu thích, ẩn hệ máy, quét ROM"), "game", "e53935", GamePage),
                 page(tl("Ngày & giờ"), tl("Định dạng giờ, âm lịch"), "recent", "546e7a", TimePage),
                 page(tl("Thông tin máy"), "", "info", "607d8b", AboutPage),
                 page(tl("Sao lưu & khôi phục"), std::to_string(backups().size()) + tl(" bản sao lưu"), "download", "1e88e5", BackupPage),
                 page(tl("Đặt lại dữ liệu gốc"), tl("Giao diện, RetroArch, Wi-Fi"), "trash", "c62828", ResetPage)};
  });
}

// ---------- apps hub ----------
Screen* AppsScreen() {
  return Page(tl("Ứng dụng"), "tools", [] {
    return Items{
        page(tl("Tiện ích"), tl("Lịch, máy tính, bấm giờ, ghi chú, đèn pin"), "calc", "e65100", [] {
          return Page(tl("Tiện ích"), "calc", [] {
            return Items{act(tl("Lịch"), tl("Dương lịch và âm lịch"), "calendar", "c62828", [] { push(CalendarApp()); }),
                         act(tl("Nhắc việc"), tl("Nhắc theo ngày giờ, đổ chuông"), "bell", "ef6c00", [] { push(RemindersApp()); }),
                         act(tl("Đếm ngày"), tl("Tết, sinh nhật, ngày giỗ (âm/dương)"), "timer", "c62828", [] { push(DayCountApp()); }),
                         act(tl("Máy tính"), "", "calc", "e65100", [] { push(CalcApp()); }),
                         act(tl("Đồng hồ bấm giờ"), "", "timer", "e65100", [] { push(StopwatchApp()); }),
                         act(tl("Ghi chú"), "", "note", "f5a300", [] { push(NotesApp()); }),
                         act(tl("Đèn pin (màn hình)"), "", "flash", "fbc02d", [] { push(FlashlightApp()); }),
                         act(tl("Kiểm tra phím"), tl("Thử các nút trên máy"), "key", "00897b", [] { push(KeyTestApp()); })};
          });
        }),
        page(tl("Tệp & hệ thống"), tl("Quản lý tệp, USB, cập nhật"), "folder", "6d4c41", [] {
          return Page(tl("Tệp & hệ thống"), "folder", [] {
            return Items{act(tl("Gắn ổ USB"), "", "usb", "43a047", [] { shellTask(tl("Gắn ổ USB"), {tl("Tìm thiết bị"), tl("Gắn vào /roms/usbdrive")}, "sudo /usr/local/bin/usbmount.sh 2>/dev/null || sudo mount /dev/sda1 /roms/usbdrive"); }),
                         act(tl("Tháo ổ USB"), "", "usb", "ef6c00", [] { shellTask(tl("Tháo ổ USB"), {tl("Ghi dữ liệu"), tl("Tháo ổ")}, "sync && sudo umount /roms/usbdrive"); }),
                         page(tl("Sao lưu & khôi phục"), tl("Cài đặt, RetroArch, Wi-Fi"), "download", "1e88e5", BackupPage)};
          });
        }),
        act(tl("Quản lý tệp"), tl("Chép, đổi tên, tệp ẩn, giải nén zip/rar/7z có mật khẩu"), "folder", "f9a825", [] { push(FileManagerApp()); }),
        act(tl("Terminal"), tl("Dòng lệnh Linux (bash)"), "term", "37474f", [] { push(TerminalApp()); })};
  });
}

}  // namespace s40
