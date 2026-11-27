// s40abext - WebKit web-process extension for the s40web ad blocker.
// Cancels requests whose host is in the converted host block list (<adblock dir>/hosts.txt) -
// the plain "||host^" rules that make up most of EasyList & co. A hash lookup per request instead
// of hundreds of thousands of compiled regex states. The browser shell writes hosts.txt and
// state.cfg (enabled=, site= exceptions); both are re-read when they change.
#include <wpe/webkit-web-process-extension.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_set>

namespace {
std::string g_dir;
std::unordered_set<std::string> g_any, g_3p, g_allowSites, g_userSites;
bool g_enabled = true;
long long g_hostsStamp = -1, g_stateStamp = -1;
gint64 g_nextCheck = 0;

long long stamp(const std::string& p) {
  struct stat s;
  if (stat(p.c_str(), &s) != 0) return 0;
  return (long long)s.st_mtim.tv_sec * 1000000000LL + s.st_mtim.tv_nsec + s.st_size;
}
void reload() {
  gint64 now = g_get_monotonic_time();
  if (now < g_nextCheck) return;
  g_nextCheck = now + G_USEC_PER_SEC;
  long long st = stamp(g_dir + "/hosts.txt");
  if (st != g_hostsStamp) {
    g_hostsStamp = st;
    g_any.clear(); g_3p.clear(); g_allowSites.clear();
    std::ifstream f(g_dir + "/hosts.txt");
    std::string l;
    while (std::getline(f, l)) {
      if (l.empty()) continue;
      if (l[0] == '@') g_allowSites.insert(l.substr(1));
      else if (l.size() > 3 && l.compare(l.size() - 3, 3, "$3p") == 0) g_3p.insert(l.substr(0, l.size() - 3));
      else g_any.insert(l);
    }
  }
  st = stamp(g_dir + "/state.cfg");
  if (st != g_stateStamp) {
    g_stateStamp = st;
    g_enabled = true;
    g_userSites.clear();
    std::ifstream f(g_dir + "/state.cfg");
    std::string l;
    while (std::getline(f, l)) {
      if (l.rfind("enabled=", 0) == 0) g_enabled = l.substr(8) == "1";
      else if (l.rfind("site=", 0) == 0) g_userSites.insert(l.substr(5));
    }
  }
}
std::string hostOf(const char* uri) {
  const char* a = strstr(uri, "://");
  if (!a) return "";
  a += 3;
  const char* b = a + strcspn(a, "/:?#");
  std::string h(a, b);
  for (char& c : h) if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
  if (h.rfind("www.", 0) == 0) h = h.substr(4);
  return h;
}
bool inSet(const std::unordered_set<std::string>& s, const std::string& host) {
  if (s.empty()) return false;
  for (size_t p = 0;;) {
    if (s.count(host.substr(p))) return true;
    p = host.find('.', p);
    if (p == std::string::npos) return false;
    p++;
  }
}
// registrable domain, approximately (handles example.com and example.com.vn / co.uk)
std::string siteOf(const std::string& h) {
  size_t last = h.rfind('.');
  if (last == std::string::npos || last == 0) return h;
  size_t prev = h.rfind('.', last - 1);
  if (prev == std::string::npos) return h;
  std::string sld = h.substr(prev + 1, last - prev - 1);
  if (h.size() - last - 1 == 2 && (sld == "com" || sld == "net" || sld == "org" || sld == "edu" || sld == "gov" || sld == "co" ||
                                   sld == "ac" || sld == "info" || sld == "biz" || sld == "name" || sld == "or" || sld == "ne")) {
    if (prev == 0) return h;
    size_t p2 = h.rfind('.', prev - 1);
    return p2 == std::string::npos ? h : h.substr(p2 + 1);
  }
  return h.substr(prev + 1);
}

gboolean sendRequest(WebKitWebPage* page, WebKitURIRequest* req, WebKitURIResponse*, gpointer) {
  reload();
  if (!g_enabled) return FALSE;
  const char* u = webkit_uri_request_get_uri(req);
  if (!u || (strncmp(u, "http", 4) != 0 && strncmp(u, "ws", 2) != 0)) return FALSE;
  std::string host = hostOf(u);
  const char* pu = webkit_web_page_get_uri(page);
  std::string pageHost = pu ? hostOf(pu) : "";
  if (!pageHost.empty()) {
    if (inSet(g_userSites, pageHost) || inSet(g_allowSites, pageHost)) return FALSE;
    if (host == pageHost) return FALSE;  // the site itself (e.g. navigating to it)
  }
  bool block = inSet(g_any, host) || (inSet(g_3p, host) && siteOf(host) != siteOf(pageHost));
  static bool debug = getenv("S40AB_DEBUG") != nullptr;
  if (debug && block) fprintf(stderr, "s40abext: blocked %s\n", u);
  return block;
}
}  // namespace

extern "C" G_MODULE_EXPORT void webkit_web_process_extension_initialize_with_user_data(WebKitWebProcessExtension* ext, const GVariant* data) {
  if (data && g_variant_is_of_type((GVariant*)data, G_VARIANT_TYPE_STRING)) g_dir = g_variant_get_string((GVariant*)data, nullptr);
  if (g_dir.empty()) return;
  g_signal_connect(ext, "page-created", G_CALLBACK(+[](WebKitWebProcessExtension*, WebKitWebPage* page, gpointer) {
                     g_signal_connect(page, "send-request", G_CALLBACK(sendRequest), nullptr);
                   }), nullptr);
}
