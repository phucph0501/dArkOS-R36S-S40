// Adblock - uBlock Origin / Adblock Plus filter lists for the WPE WebKit browser.
//
// Filter lists are downloaded and refreshed automatically, then converted (worker thread) into
// three cheap parts, the same split uBlock Origin uses:
//   * plain host blocks (||ads.example^, the bulk of every list) -> hash set in a web-process
//     extension (s40abext.so) that cancels requests; no compilation, a few MB of RAM;
//   * element hiding (##.ad) -> user style sheets: one generic sheet + one per visited site;
//   * everything else (URL patterns, typed rules, exceptions) -> WebKit's native content
//     blocker, compiled once into a memory-mapped bytecode file (recompiled only on change).
#pragma once
#include <atomic>
#include <functional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct _WebKitWebView;
struct _WebKitUserContentFilterStore;
struct _WebKitUserContentFilter;
struct _WebKitUserStyleSheet;
struct _SoupSession;

namespace abp {
struct Stats { int hosts = 0, network = 0, exceptions = 0, cosmetic = 0, skipped = 0; };
struct Result {
  std::string filterJson;                                             // WebKit content-blocker rules
  std::vector<std::string> hosts;                                     // "host", "host$3p", "@site" (site allowed)
  std::vector<std::string> generic;                                   // generic hiding selectors
  std::unordered_map<std::string, std::vector<std::string>> specific; // domain -> hiding selectors
  std::unordered_map<std::string, std::vector<std::string>> unhide;   // domain -> generic selectors excepted
  Stats stats;
};
Result convert(const std::vector<std::string>& lists);
}  // namespace abp

class Adblock {
 public:
  struct List { const char* id; const char* name; const char* url; bool defaultOn; };
  static const std::vector<List>& catalog();
  static int compileTest(const std::string& jsonPath, const std::string& storeDir);

  // dataDir: profile dir; bundledDir: lists shipped with the image (used until the first download)
  void init(_WebKitWebView* view, const std::string& dataDir, const std::string& bundledDir);
  void poll();                                 // every frame on the main thread
  void onNavigate(const std::string& uri);     // per-site sheet + exceptions

  bool enabled() const { return m_enabled; }
  void setEnabled(bool on);
  bool cosmetic() const { return m_cosmetic; }
  void setCosmetic(bool on);
  bool autoUpdate() const { return m_auto; }
  void setAutoUpdate(bool on);
  bool listOn(const std::string& id) const;
  void setListOn(const std::string& id, bool on);
  void updateNow();

  bool siteExcepted(const std::string& host) const;
  void setSiteExcepted(const std::string& host, bool excepted);
  std::vector<std::string> sites() const { return {m_sites.begin(), m_sites.end()}; }

  bool busy() const { return m_busy; }
  std::string status() const;
  std::function<void(const std::string&)> notify;

 private:
  void loadState();
  void saveState();
  std::string listPath(const std::string& id) const;
  void startDownloads(bool force);
  void onDownloaded(const std::string& id, const void* data, size_t size, bool ok);
  void rebuild();
  void onConverted();
  void onCompiled(_WebKitUserContentFilter* filter, const std::string& err);
  void apply();
  void applyStyles();

  _WebKitWebView* m_view = nullptr;
  _WebKitUserContentFilterStore* m_store = nullptr;
  _WebKitUserContentFilter* m_filter = nullptr;
  _SoupSession* m_soup = nullptr;
  std::string m_dir, m_bundled, m_host;
  bool m_enabled = true, m_cosmetic = true, m_auto = true;
  std::set<std::string> m_off, m_on, m_sites;
  long m_lastUpdate = 0;
  int m_rules = 0;
  std::string m_filterHash;

  // cosmetic data (from the last conversion)
  std::vector<std::string> m_generic;
  std::unordered_map<std::string, std::vector<std::string>> m_specific, m_unhide;
  _WebKitUserStyleSheet* m_genericSheet = nullptr;   // shared by all sites without exceptions
  _WebKitUserStyleSheet* m_activeGeneric = nullptr;  // generic sheet currently installed
  _WebKitUserStyleSheet* m_siteSheet = nullptr;      // current site's sheet
  std::string m_styledHost = "\x01";
  bool m_filterOn = false;
  int m_scriptState = -1;  // -1 = no scripts installed yet, else ad-skipping on/off

  // background work
  bool m_busy = false, m_changed = false, m_rebuildQueued = false;
  int m_pending = 0;
  std::thread m_worker;
  std::atomic<bool> m_converted{false};
  abp::Result m_result;
  long long m_t0 = 0, m_t1 = 0;
};
