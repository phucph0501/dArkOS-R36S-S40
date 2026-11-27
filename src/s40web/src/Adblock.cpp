#include "s40tr.h"
#include "Adblock.h"

#include <wpe/webkit.h>
#include <libsoup/soup.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <unordered_set>

// ======================================================================================
// ABP / uBlock Origin syntax  ->  host set + cosmetic maps + WebKit content-blocker JSON
// ======================================================================================
namespace abp {
namespace {

std::string js(const std::string& s) {  // JSON string literal
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  return o + "\"";
}
std::string arr(const std::vector<std::string>& v) {
  std::string o = "[";
  for (size_t i = 0; i < v.size(); i++) o += (i ? "," : "") + js(v[i]);
  return o + "]";
}
std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
bool starts(const std::string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }

// "Example.com" -> "example.com"; false for entities (google.*), regexes, IDN, junk.
bool cleanDomain(std::string& d) {
  if (d.empty()) return false;
  for (char& c : d) {
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return false;
  }
  return d.front() != '.' && d.back() != '.';
}
// "a.com,~b.a.com" (sep ',') or "a.com|~b.com" (sep '|'). bad = only domains we cannot express,
// so the rule must be dropped instead of becoming global.
void domains(const std::string& s, char sep, std::vector<std::string>& inc, std::vector<std::string>& exc, bool& bad) {
  std::stringstream ss(s);
  std::string t;
  int posTotal = 0, posOk = 0;
  while (std::getline(ss, t, sep)) {
    t = trim(t);
    if (t.empty()) continue;
    bool neg = t[0] == '~';
    if (neg) t = t.substr(1);
    if (!neg) posTotal++;
    if (!cleanDomain(t)) continue;
    if (neg) exc.push_back(t); else { inc.push_back(t); posOk++; }
  }
  bad = posTotal > 0 && posOk == 0;
}
std::vector<std::string> star(const std::vector<std::string>& v) {  // WebKit: "*d" = d and subdomains
  std::vector<std::string> o;
  for (auto& d : v) o.push_back("*" + d);
  return o;
}

// URL pattern -> WebKit url-filter (a small regex subset: . * + ? [] () ^ $ and escapes).
bool urlFilter(std::string p, std::string& out) {
  if (p.size() > 2 && p.front() == '/' && p.back() == '/') return false;  // raw regex: not portable
  std::string r;
  bool anchoredStart = false;
  if (starts(p, "||")) { r = "^[^:]+:(//)?([^/]+\\.)?"; p = p.substr(2); anchoredStart = true; }
  else if (starts(p, "|")) { r = "^"; p = p.substr(1); anchoredStart = true; }
  bool anchoredEnd = !p.empty() && p.back() == '|';
  if (anchoredEnd) p.pop_back();
  if (!anchoredStart) while (!p.empty() && p.front() == '*') p.erase(0, 1);
  if (!anchoredEnd) while (!p.empty() && p.back() == '*') p.pop_back();
  for (size_t i = 0; i < p.size(); i++) {
    unsigned char c = p[i];
    if (c >= 0x80 || c < 0x20) return false;
    if (c == '*') { if (r.size() < 2 || r.compare(r.size() - 2, 2, ".*") != 0) r += ".*"; }
    else if (c == '^') r += "[/:?=&]";
    else if (strchr(".+?{}()[]\\$|", c)) { r += '\\'; r += (char)c; }
    else if (c >= 'A' && c <= 'Z') r += (char)(c - 'A' + 'a');
    else r += (char)c;
  }
  if (anchoredEnd) r += "$";
  if (r.empty() || r == "^") r = ".*";
  out = r;
  return true;
}
// "||host^" / "||host" / "||host^|" -> host
bool plainHost(const std::string& p, std::string& host) {
  if (!starts(p, "||")) return false;
  std::string h = p.substr(2);
  while (!h.empty() && (h.back() == '^' || h.back() == '|')) h.pop_back();
  if (h.find_first_of("/*:?=&^|") != std::string::npos) return false;
  if (!cleanDomain(h) || h.find('.') == std::string::npos) return false;
  host = h;
  return true;
}
// leading host of a "||host/..." pattern (for exception bookkeeping)
std::string patternHost(const std::string& p) {
  if (!starts(p, "||")) return "";
  std::string h = p.substr(2, p.find_first_of("/^*|$:?", 2) - 2);
  return cleanDomain(h) ? h : "";
}

const char* kUnsupportedSel[] = {":has-text(", ":-abp-", ":xpath(", ":upward(", ":matches-css", ":style(", ":remove(",
                                 ":watch-attr(", ":min-text-length(", ":matches-path(", ":others(", ":matches-attr(",
                                 ":matches-prop(", ":if(", ":if-not(", ":nth-ancestor(", ":remove-attr(", ":remove-class(",
                                 ":matches-media(", ":spath(", ":shadow(", ":contains(", "{", "}"};
bool selectorOk(const std::string& s) {
  if (s.empty() || s[0] == '+' || s[0] == '^') return false;
  for (const char* u : kUnsupportedSel) if (s.find(u) != std::string::npos) return false;
  return true;
}

struct Ctx {
  std::vector<std::string> block, allow;
  std::unordered_set<std::string> seen;
  std::unordered_map<std::string, std::pair<bool, std::string>> hosts;  // host -> (third-party only, filter rule)
  std::vector<std::string> hostOrder;
  std::unordered_set<std::string> exceptionHosts, allowSites;
  Result* res;
  std::unordered_set<std::string> genericSeen;
  void add(std::vector<std::string>& to, std::string rule) { if (seen.insert(rule).second) to.push_back(std::move(rule)); }
};

std::string trigger(const std::string& filter, const std::vector<std::string>& types, const std::vector<std::string>& inc,
                    const std::vector<std::string>& exc, const char* load, bool caseSensitive) {
  std::string t = "{\"url-filter\":" + js(filter);
  if (caseSensitive) t += ",\"url-filter-is-case-sensitive\":true";
  if (!types.empty()) t += ",\"resource-type\":" + arr(types);
  if (load) t += std::string(",\"load-type\":[\"") + load + "\"]";
  if (!inc.empty()) t += ",\"if-domain\":" + arr(star(inc));  // WebKit: if- and unless-domain are exclusive
  else if (!exc.empty()) t += ",\"unless-domain\":" + arr(star(exc));
  return t + "}";
}
void addType(std::vector<std::string>& v, const char* t) { for (auto& x : v) if (x == t) return; v.push_back(t); }

void network(std::string line, Ctx& c) {
  Stats& st = c.res->stats;
  bool exception = starts(line, "@@");
  if (exception) line = line.substr(2);
  std::string pat = line, opts;
  size_t d = line.rfind('$');
  if (d != std::string::npos && !(line.size() > 2 && line[0] == '/' && line.back() == '/')) { pat = line.substr(0, d); opts = line.substr(d + 1); }

  std::vector<std::string> types, negTypes, inc, exc;
  const char* load = nullptr;
  bool caseSensitive = false, wholeDoc = false, hasPositiveType = false;
  std::stringstream ss(opts);
  std::string o1;
  while (std::getline(ss, o1, ',')) {
    o1 = trim(o1);
    if (o1.empty()) continue;
    bool neg = o1[0] == '~';
    std::string k = neg ? o1.substr(1) : o1, val;
    size_t eq = k.find('=');
    if (eq != std::string::npos) { val = k.substr(eq + 1); k = k.substr(0, eq); }
    const char* type = nullptr;
    if (k == "script") type = "script";
    else if (k == "image") type = "image";
    else if (k == "stylesheet" || k == "css") type = "style-sheet";
    else if (k == "font") type = "font";
    else if (k == "media") type = "media";
    else if (k == "xmlhttprequest" || k == "xhr" || k == "websocket" || k == "ping" || k == "other") type = "raw";
    else if (k == "subdocument" || k == "frame") type = "document";
    else if (k == "popup") type = "popup";
    if (type) {
      if (neg) negTypes.push_back(type); else { addType(types, type); hasPositiveType = true; }
      continue;
    }
    if (k == "third-party" || k == "3p") load = neg ? "first-party" : "third-party";
    else if (k == "first-party" || k == "1p") load = neg ? "third-party" : "first-party";
    else if (k == "domain" || k == "from") { bool bad; domains(val, '|', inc, exc, bad); if (bad) { st.skipped++; return; } }
    else if (k == "match-case") caseSensitive = true;
    else if (k == "important" || k == "all") {}
    else if (k == "document" || k == "doc") { if (exception) wholeDoc = true; else { addType(types, "document"); hasPositiveType = true; } }
    else if (k == "object" || k == "object-subrequest") { if (!neg) hasPositiveType = true; }  // no WebKit type
    else { st.skipped++; return; }  // redirect, removeparam, csp, elemhide, badfilter, ...
  }
  if (hasPositiveType && types.empty()) { st.skipped++; return; }
  if (!negTypes.empty() && types.empty())
    for (const char* t : {"document", "image", "style-sheet", "script", "font", "raw", "svg-document", "media", "popup"}) {
      bool n = false;
      for (auto& x : negTypes) if (x == t) n = true;
      if (!n) types.push_back(t);
    }

  if (exception) {
    std::string eh = patternHost(pat);
    if (!eh.empty()) c.exceptionHosts.insert(eh);
    if (wholeDoc) {  // @@||site^$document: whole site allowed
      std::string host;
      if (!plainHost(pat, host)) { st.skipped++; return; }
      c.allowSites.insert(host);
      c.add(c.allow, "{\"trigger\":{\"url-filter\":\".*\",\"if-domain\":[" + js("*" + host) + "]},\"action\":{\"type\":\"ignore-previous-rules\"}}");
      st.exceptions++;
      return;
    }
  }
  std::string f;
  if (!urlFilter(pat, f)) { st.skipped++; return; }
  if (f == ".*" && inc.empty() && types.empty() && !exception) { st.skipped++; return; }  // would block everything
  if (exception && types.empty())  // network exceptions must not switch off element hiding on the page itself
    types = {"image", "style-sheet", "script", "font", "raw", "svg-document", "media", "popup"};
  std::string rule = "{\"trigger\":" + trigger(f, types, inc, exc, load, caseSensitive) + ",\"action\":{\"type\":\"" +
                     (exception ? "ignore-previous-rules" : "block") + "\"}}";
  if (exception) { c.add(c.allow, rule); st.exceptions++; return; }

  std::string host;
  bool simple = types.empty() && inc.empty() && exc.empty() && !caseSensitive && (!load || !strcmp(load, "third-party"));
  if (simple && plainHost(pat, host)) {
    auto it = c.hosts.find(host);
    if (it == c.hosts.end()) { c.hosts[host] = {load != nullptr, rule}; c.hostOrder.push_back(host); }
    else if (!load) it->second = {false, rule};  // an unconditional rule wins over a third-party one
    return;
  }
  c.add(c.block, rule);
  st.network++;
}

void cosmeticRule(const std::string& dom, const std::string& sel0, bool exception, Ctx& c) {
  Stats& st = c.res->stats;
  std::string sel = trim(sel0);
  if (!selectorOk(sel)) { st.skipped++; return; }
  std::vector<std::string> inc, exd;
  bool bad = false;
  domains(dom, ',', inc, exd, bad);
  if (bad) { st.skipped++; return; }
  if (exception) { for (auto& d : inc) c.res->unhide[d].push_back(sel); return; }
  if (inc.empty()) {  // generic (optionally "except on ..." domains)
    if (c.genericSeen.insert(sel).second) { c.res->generic.push_back(sel); st.cosmetic++; }
    for (auto& d : exd) c.res->unhide[d].push_back(sel);
  } else {
    for (auto& d : inc) c.res->specific[d].push_back(sel);
    st.cosmetic++;
  }
}

}  // namespace

Result convert(const std::vector<std::string>& lists) {
  Result res;
  Ctx c;
  c.res = &res;
  for (const std::string& text : lists) {
    std::istringstream in(text);
    std::string line;
    int skipDepth = 0, depth = 0;
    while (std::getline(in, line)) {
      line = trim(line);
      if (line.empty() || line[0] == '[') continue;
      if (line[0] == '!') {
        if (starts(line, "!#if")) { depth++; if (!skipDepth && line.find("false") != std::string::npos) skipDepth = depth; }
        else if (starts(line, "!#endif")) { if (skipDepth == depth) skipDepth = 0; if (depth) depth--; }
        continue;
      }
      if (skipDepth) continue;
      if (starts(line, "0.0.0.0 ") || starts(line, "127.0.0.1 ")) {  // hosts-file syntax
        std::string h = trim(line.substr(line.find(' ') + 1));
        size_t e = h.find_first_of(" \t#");
        if (e != std::string::npos) h = h.substr(0, e);
        if (!h.empty() && h != "localhost" && h != "0.0.0.0") network("||" + h + "^", c);
        continue;
      }
      size_t hash = line.find('#');
      if (hash != std::string::npos && line.find_first_of("/|$") > hash) {
        std::string dom = line.substr(0, hash), rest = line.substr(hash);
        if (starts(rest, "##")) { cosmeticRule(dom, rest.substr(2), false, c); continue; }
        if (starts(rest, "#@#")) { cosmeticRule(dom, rest.substr(3), true, c); continue; }
        if (rest.size() > 2 && rest[2] == '#' && strchr("?$%", rest[1])) { res.stats.skipped++; continue; }  // extended syntaxes
        if (rest.size() > 3 && rest[1] == '@' && rest[3] == '#') { res.stats.skipped++; continue; }
      }
      network(line, c);
    }
  }
  // A host with exception rules under it (@@||cdn.ads.com/lib.js) must stay in the content blocker,
  // where the exception can override it; the extension's host set has no exceptions.
  for (auto& eh : c.exceptionHosts) {
    std::string h = eh;
    while (true) {
      auto it = c.hosts.find(h);
      if (it != c.hosts.end()) { c.add(c.block, it->second.second); res.stats.network++; c.hosts.erase(it); }
      size_t dot = h.find('.');
      if (dot == std::string::npos) break;
      h = h.substr(dot + 1);
    }
  }
  for (auto& h : c.hostOrder) {
    auto it = c.hosts.find(h);
    if (it == c.hosts.end()) continue;
    res.hosts.push_back(it->second.first ? h + "$3p" : h);
  }
  for (auto& s : c.allowSites) res.hosts.push_back("@" + s);
  res.stats.hosts = (int)c.hosts.size();
  // order matters: ignore-previous-rules only cancels rules listed before it
  std::string j = "[";
  bool first = true;
  for (auto* v : {&c.block, &c.allow})
    for (auto& r : *v) { if (!first) j += ",\n"; j += r; first = false; }
  if (first) j += "{\"trigger\":{\"url-filter\":\"^s40web-nothing:\"},\"action\":{\"type\":\"block\"}}";
  res.filterJson = j + "]";
  return res;
}
}  // namespace abp

// ======================================================================================
// Adblock manager
// ======================================================================================
static const long kUpdateEvery = 4 * 24 * 3600;  // like uBlock's default list expiry (4-5 days)

const std::vector<Adblock::List>& Adblock::catalog() {
  static const std::vector<List> c = {
      {"easylist", tl("EasyList (quảng cáo quốc tế)"), "https://easylist.to/easylist/easylist.txt", true},
      {"abpvn", tl("ABPVN (quảng cáo Việt Nam)"), "https://raw.githubusercontent.com/abpvn/abpvn/master/filter/abpvn.txt", true},
      {"ublock", "uBlock filters", "https://ublockorigin.github.io/uAssets/filters/filters.txt", true},
      {"pgl", tl("Peter Lowe (máy chủ quảng cáo)"), "https://pgl.yoyo.org/adservers/serverlist.php?hostformat=adblockplus&showintro=0&mimetype=plaintext", true},
      {"badware", tl("Trang lừa đảo, mã độc"), "https://ublockorigin.github.io/uAssets/filters/badware.txt", true},
      {"easyprivacy", tl("EasyPrivacy (chặn theo dõi)"), "https://easylist.to/easylist/easyprivacy.txt", false},
      {"cookies", tl("Ẩn thông báo cookie"), "https://ublockorigin.github.io/uAssets/thirdparties/easylist-cookies.txt", false},
  };
  return c;
}

static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return "";
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}
static bool writeFileAtomic(const std::string& p, const std::string& data) {
  std::string tmp = p + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
  ok = fclose(f) == 0 && ok;
  return ok && rename(tmp.c_str(), p.c_str()) == 0;
}
static bool exists(const std::string& p) { struct stat s; return stat(p.c_str(), &s) == 0 && s.st_size > 0; }
static long mtime(const std::string& p) { struct stat s; return stat(p.c_str(), &s) == 0 ? (long)s.st_mtime : 0; }
static std::string hostOf(const std::string& uri) {
  size_t a = uri.find("://");
  if (a == std::string::npos) return "";
  a += 3;
  size_t b = uri.find_first_of("/:?#", a);
  std::string h = uri.substr(a, b == std::string::npos ? std::string::npos : b - a);
  for (char& c : h) if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
  if (h.rfind("www.", 0) == 0) h = h.substr(4);
  return h;
}
static std::string hashOf(const std::string& s) {  // FNV-1a 64
  unsigned long long h = 1469598103934665603ULL;
  for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
  char b[24];
  snprintf(b, sizeof b, "%016llx", h);
  return b;
}

std::string Adblock::listPath(const std::string& id) const { return m_dir + "/lists/" + id + ".txt"; }

bool Adblock::listOn(const std::string& id) const {
  if (m_on.count(id)) return true;
  if (m_off.count(id)) return false;
  for (auto& l : catalog()) if (id == l.id) return l.defaultOn;
  return false;
}

// state.cfg is also read by the web-process extension (enabled=, site=)
void Adblock::loadState() {
  std::ifstream f(m_dir + "/state.cfg");
  std::string l;
  while (std::getline(f, l)) {
    size_t e = l.find('=');
    if (e == std::string::npos) continue;
    std::string k = l.substr(0, e), v = l.substr(e + 1);
    if (k == "enabled") m_enabled = v == "1";
    else if (k == "cosmetic") m_cosmetic = v == "1";
    else if (k == "auto") m_auto = v == "1";
    else if (k == "last") m_lastUpdate = atol(v.c_str());
    else if (k == "rules") m_rules = atoi(v.c_str());
    else if (k == "fhash") m_filterHash = v;
    else if (k == "on") m_on.insert(v);
    else if (k == "off") m_off.insert(v);
    else if (k == "site") m_sites.insert(v);
  }
}
void Adblock::saveState() {
  std::ostringstream f;
  f << "enabled=" << m_enabled << "\ncosmetic=" << m_cosmetic << "\nauto=" << m_auto << "\nlast=" << m_lastUpdate
    << "\nrules=" << m_rules << "\nfhash=" << m_filterHash << "\n";
  for (auto& s : m_on) f << "on=" << s << "\n";
  for (auto& s : m_off) f << "off=" << s << "\n";
  for (auto& s : m_sites) f << "site=" << s << "\n";
  writeFileAtomic(m_dir + "/state.cfg", f.str());
}

// YouTube serves its ads from its own servers, so network rules cannot catch them: skip them in-page.
static const char* kYoutubeJs =
    "(function(){if(window.__s40ab)return;window.__s40ab=1;setInterval(function(){"
    "var p=document.querySelector('.ad-showing,.ad-interrupting');"
    "if(p){var v=p.querySelector('video');if(v&&isFinite(v.duration)&&v.duration>0&&v.currentTime<v.duration-0.3){v.muted=true;v.currentTime=v.duration-0.1;}}"
    "document.querySelectorAll('.ytp-ad-skip-button,.ytp-ad-skip-button-modern,.ytp-skip-ad-button,.ytp-ad-overlay-close-button').forEach(function(b){b.click()});"
    "},400)})();";
static const char* kYoutubeCss =
    "ytm-promoted-sparkles-web-renderer,ytm-companion-ad-renderer,ytm-ad-slot-renderer,ad-slot-renderer,"
    "ytm-promoted-video-renderer,ytd-ad-slot-renderer,.ytp-ad-overlay-container,ytm-paid-content-overlay-renderer"
    "{display:none!important}";
// Video quality for a 640x480 handheld: advertise only H.264 up to 480p/30fps to YouTube's player
// (like the h264ify extension). Cheap to decode in software on a Cortex-A35, and avoids WebKit's
// MSE stalls when YouTube switches to a high-resolution or 60fps stream. Always on, also with ads allowed.
static const char* kYoutubeQualityJs =
    "(function(){if(window.__s40q)return;window.__s40q=1;"
    "function ok(t){t=String(t||'');if(/vp0?9|av01|vp8/i.test(t))return false;"
    "var f=/framerate=([0-9]+)/.exec(t);if(f&&+f[1]>30)return false;"
    "var w=/width=([0-9]+)/.exec(t);if(w&&+w[1]>854)return false;"
    "var h=/height=([0-9]+)/.exec(t);if(h&&+h[1]>480)return false;return true;}"
    "if(window.MediaSource){var m=MediaSource.isTypeSupported.bind(MediaSource);MediaSource.isTypeSupported=function(t){return ok(t)&&m(t);};}"
    "var c=HTMLMediaElement.prototype.canPlayType;HTMLMediaElement.prototype.canPlayType=function(t){return ok(t)?c.call(this,t):'';};"
    "})();";
static const char* const kYoutubeUrls[] = {"https://*.youtube.com/*", "https://youtube.com/*", nullptr};

void Adblock::init(_WebKitWebView* view, const std::string& dataDir, const std::string& bundledDir) {
  m_view = view;
  m_dir = dataDir + "/adblock";
  m_bundled = bundledDir;
  mkdir(m_dir.c_str(), 0755);
  mkdir((m_dir + "/lists").c_str(), 0755);
  mkdir((m_dir + "/store").c_str(), 0755);
  loadState();
  saveState();  // make sure the extension finds a state file
  m_store = webkit_user_content_filter_store_new((m_dir + "/store").c_str());
  m_soup = soup_session_new_with_options("timeout", 60, "user-agent", "Mozilla/5.0 s40web-adblock", nullptr);
  apply();
  m_busy = true;
  webkit_user_content_filter_store_load(m_store, "s40", nullptr,
      +[](GObject* src, GAsyncResult* res, gpointer self) {
        auto* a = static_cast<Adblock*>(self);
        WebKitUserContentFilter* f = webkit_user_content_filter_store_load_finish(WEBKIT_USER_CONTENT_FILTER_STORE(src), res, nullptr);
        if (f) { a->m_filter = f; a->apply(); }
        else a->m_filterHash.clear();  // forces a compile after the conversion below
        a->rebuild();  // cosmetic data lives in memory only: convert the local lists in the background
        if (a->m_auto && time(nullptr) - a->m_lastUpdate > kUpdateEvery) a->startDownloads(false);
      },
      this);
}

void Adblock::startDownloads(bool force) {
  if (m_pending) return;
  m_changed = false;
  struct Dl { Adblock* a; std::string id; SoupMessage* msg; };
  for (auto& l : catalog()) {
    if (!listOn(l.id)) continue;
    std::string p = listPath(l.id);
    if (!force && exists(p) && time(nullptr) - mtime(p) < kUpdateEvery) continue;
    SoupMessage* msg = soup_message_new("GET", l.url);
    if (!msg) continue;
    m_pending++;
    soup_session_send_and_read_async(m_soup, msg, G_PRIORITY_LOW, nullptr,
        +[](GObject* src, GAsyncResult* res, gpointer data) {
          auto* d = static_cast<Dl*>(data);
          GError* err = nullptr;
          GBytes* b = soup_session_send_and_read_finish(SOUP_SESSION(src), res, &err);
          gsize n = 0;
          const void* p = b ? g_bytes_get_data(b, &n) : nullptr;
          d->a->onDownloaded(d->id, p, n, b && soup_message_get_status(d->msg) == 200);
          if (b) g_bytes_unref(b);
          if (err) g_error_free(err);
          g_object_unref(d->msg);
          delete d;
        },
        new Dl{this, l.id, msg});
  }
  if (!m_pending) { if (force && notify) notify(tl("Không có danh sách nào được bật")); return; }
  m_busy = true;
  if (force && notify) notify(tl("Đang tải danh sách lọc…"));
}

void Adblock::onDownloaded(const std::string& id, const void* data, size_t size, bool ok) {
  if (ok && size > 512) {
    std::string body((const char*)data, size);
    // sanity check: an ABP list, not an error or captive-portal page
    if (body[0] == '[' || body[0] == '!' || body.find("||") != std::string::npos) {
      std::string p = listPath(id);
      if (readFile(p) != body) { if (writeFileAtomic(p, body)) m_changed = true; }
      else utimes(p.c_str(), nullptr);  // unchanged, but fresh
      m_lastUpdate = time(nullptr);
    }
  }
  if (--m_pending > 0) return;
  saveState();
  if (m_changed) rebuild();
  else {
    m_busy = m_worker.joinable();
    if (notify) notify(m_lastUpdate && time(nullptr) - m_lastUpdate < 120 ? tl("Danh sách lọc đã mới nhất") : tl("Không tải được danh sách lọc (mạng?)"));
  }
}

void Adblock::rebuild() {
  if (m_worker.joinable()) { m_rebuildQueued = true; return; }
  m_busy = true;
  std::vector<std::string> paths;
  for (auto& l : catalog()) {
    if (!listOn(l.id)) continue;
    std::string p = listPath(l.id);
    if (!exists(p)) p = m_bundled + "/" + l.id + ".txt";
    if (exists(p)) paths.push_back(p);
  }
  m_t0 = g_get_monotonic_time();
  std::string hostsPath = m_dir + "/hosts.txt";
  m_worker = std::thread([this, paths, hostsPath] {
    std::vector<std::string> texts;
    for (auto& p : paths) texts.push_back(readFile(p));
    m_result = abp::convert(texts);
    std::string hosts;
    for (auto& h : m_result.hosts) { hosts += h; hosts += '\n'; }
    if (readFile(hostsPath) != hosts) writeFileAtomic(hostsPath, hosts);  // picked up by the extension
    m_converted = true;
  });
}

static WebKitUserStyleSheet* makeSheet(const std::vector<std::string>& sels, const std::unordered_set<std::string>* skip) {
  std::string css;
  css.reserve(sels.size() * 40);
  // one rule per selector: an unsupported selector then only drops itself, not a whole group
  for (auto& s : sels) if (!skip || !skip->count(s)) { css += s; css += "{display:none!important}\n"; }
  return webkit_user_style_sheet_new(css.c_str(), WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_STYLE_LEVEL_USER, nullptr, nullptr);
}

void Adblock::onConverted() {
  m_worker.join();
  m_t1 = g_get_monotonic_time();
  m_generic = std::move(m_result.generic);
  m_specific = std::move(m_result.specific);
  m_unhide = std::move(m_result.unhide);
  // drop the installed sheets; apply() rebuilds them from the new data
  WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(m_view);
  if (m_siteSheet) { webkit_user_content_manager_remove_style_sheet(ucm, m_siteSheet); webkit_user_style_sheet_unref(m_siteSheet); m_siteSheet = nullptr; }
  if (m_activeGeneric) { webkit_user_content_manager_remove_style_sheet(ucm, m_activeGeneric); if (m_activeGeneric != m_genericSheet) webkit_user_style_sheet_unref(m_activeGeneric); m_activeGeneric = nullptr; }
  if (m_genericSheet) webkit_user_style_sheet_unref(m_genericSheet);
  m_genericSheet = nullptr;
  m_styledHost = "\x01";
  abp::Stats st = m_result.stats;
  m_rules = st.hosts + st.network + st.exceptions + st.cosmetic;
  fprintf(stderr, "s40web adblock: %d hosts, %d url rules, %d exceptions, %d hiding, %d skipped; convert %lld ms\n", st.hosts,
          st.network, st.exceptions, st.cosmetic, st.skipped, (long long)(m_t1 - m_t0) / 1000);
  std::string h = hashOf(m_result.filterJson);
  if (m_filter && h == m_filterHash) {  // URL rules unchanged: keep the compiled filter
    std::string().swap(m_result.filterJson);
    m_busy = m_pending > 0;
    saveState();
    apply();
    if (m_rebuildQueued) { m_rebuildQueued = false; rebuild(); }
    return;
  }
  m_filterHash = h;
  GBytes* bytes = g_bytes_new(m_result.filterJson.data(), m_result.filterJson.size());
  std::string().swap(m_result.filterJson);
  webkit_user_content_filter_store_save(m_store, "s40", bytes, nullptr,
      +[](GObject* src, GAsyncResult* res, gpointer self) {
        GError* err = nullptr;
        WebKitUserContentFilter* f = webkit_user_content_filter_store_save_finish(WEBKIT_USER_CONTENT_FILTER_STORE(src), res, &err);
        static_cast<Adblock*>(self)->onCompiled(f, err ? err->message : "");
        if (err) g_error_free(err);
      },
      this);
  g_bytes_unref(bytes);
  apply();  // cosmetic part is ready already
}

void Adblock::poll() {
  if (m_converted) { m_converted = false; onConverted(); }
  static long nextCheck = 0;
  long now = time(nullptr);
  if (now >= nextCheck) {
    nextCheck = now + 3600;
    if (m_auto && !m_busy && m_lastUpdate && now - m_lastUpdate > kUpdateEvery) startDownloads(false);
  }
}

void Adblock::onCompiled(_WebKitUserContentFilter* filter, const std::string& err) {
  fprintf(stderr, "s40web adblock: content filter compile %lld ms%s%s\n", (long long)(g_get_monotonic_time() - m_t1) / 1000,
          filter ? "" : " FAILED: ", err.c_str());
  if (filter) {
    if (m_filter) webkit_user_content_filter_unref(m_filter);
    m_filter = filter;
    m_filterOn = false;
    WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(m_view);
    webkit_user_content_manager_remove_all_filters(ucm);
  } else {
    m_filterHash.clear();
    if (notify) notify(tl("Lỗi biên dịch bộ lọc quảng cáo"));
  }
  saveState();
  apply();
  m_busy = m_pending > 0;
  if (m_rebuildQueued) { m_rebuildQueued = false; rebuild(); }
}

void Adblock::applyStyles() {
  WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(m_view);
  bool want = m_enabled && m_cosmetic && !siteExcepted(m_host);
  std::string host = want ? m_host : "";
  if (host == m_styledHost && (m_activeGeneric != nullptr) == (want && !m_generic.empty())) return;
  m_styledHost = host;
  if (m_siteSheet) { webkit_user_content_manager_remove_style_sheet(ucm, m_siteSheet); webkit_user_style_sheet_unref(m_siteSheet); m_siteSheet = nullptr; }
  if (!want) {
    if (m_activeGeneric) { webkit_user_content_manager_remove_style_sheet(ucm, m_activeGeneric); if (m_activeGeneric != m_genericSheet) webkit_user_style_sheet_unref(m_activeGeneric); m_activeGeneric = nullptr; }
    return;
  }
  // collect this site's data (host and parent domains)
  std::vector<std::string> site;
  std::unordered_set<std::string> unhide;
  for (std::string h = host; !h.empty();) {
    auto s = m_specific.find(h);
    if (s != m_specific.end()) site.insert(site.end(), s->second.begin(), s->second.end());
    auto u = m_unhide.find(h);
    if (u != m_unhide.end()) unhide.insert(u->second.begin(), u->second.end());
    size_t d = h.find('.');
    if (d == std::string::npos) break;
    h = h.substr(d + 1);
  }
  WebKitUserStyleSheet* generic = nullptr;
  if (!m_generic.empty()) {
    if (unhide.empty()) { if (!m_genericSheet) m_genericSheet = makeSheet(m_generic, nullptr); generic = m_genericSheet; }
    else generic = makeSheet(m_generic, &unhide);  // rare: a site that excepts some generic rules
  }
  if (generic != m_activeGeneric) {
    if (m_activeGeneric) { webkit_user_content_manager_remove_style_sheet(ucm, m_activeGeneric); if (m_activeGeneric != m_genericSheet) webkit_user_style_sheet_unref(m_activeGeneric); }
    if (generic) webkit_user_content_manager_add_style_sheet(ucm, generic);
    m_activeGeneric = generic;
  }
  if (!site.empty()) {
    for (auto it = site.begin(); it != site.end();) it = unhide.count(*it) ? site.erase(it) : it + 1;
    m_siteSheet = makeSheet(site, nullptr);
    webkit_user_content_manager_add_style_sheet(ucm, m_siteSheet);
  }
}

void Adblock::apply() {
  if (!m_view) return;
  WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(m_view);
  bool active = m_enabled && !siteExcepted(m_host);
  bool wantFilter = active && m_filter;
  if (wantFilter != m_filterOn) {
    webkit_user_content_manager_remove_all_filters(ucm);
    if (wantFilter) webkit_user_content_manager_add_filter(ucm, m_filter);
    m_filterOn = wantFilter;
  }
  if (m_scriptState != (int)active) {
    webkit_user_content_manager_remove_all_scripts(ucm);
    WebKitUserScript* q = webkit_user_script_new(kYoutubeQualityJs, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, kYoutubeUrls, nullptr);
    webkit_user_content_manager_add_script(ucm, q);
    webkit_user_script_unref(q);
    static WebKitUserStyleSheet* ytCss = nullptr;
    if (!ytCss) ytCss = webkit_user_style_sheet_new(kYoutubeCss, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_STYLE_LEVEL_USER, kYoutubeUrls, nullptr);
    webkit_user_content_manager_remove_style_sheet(ucm, ytCss);
    if (active) {
      WebKitUserScript* s = webkit_user_script_new(kYoutubeJs, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, kYoutubeUrls, nullptr);
      webkit_user_content_manager_add_script(ucm, s);
      webkit_user_script_unref(s);
      webkit_user_content_manager_add_style_sheet(ucm, ytCss);
    }
    m_scriptState = active;
  }
  applyStyles();
}

void Adblock::onNavigate(const std::string& uri) {
  std::string h = hostOf(uri);
  if (h == m_host) return;
  m_host = h;
  apply();
}

void Adblock::setEnabled(bool on) { m_enabled = on; saveState(); apply(); }
void Adblock::setCosmetic(bool on) { m_cosmetic = on; saveState(); apply(); }
void Adblock::setAutoUpdate(bool on) {
  m_auto = on;
  saveState();
  if (on && time(nullptr) - m_lastUpdate > kUpdateEvery) startDownloads(false);
}
void Adblock::setListOn(const std::string& id, bool on) {
  bool def = false;
  for (auto& l : catalog()) if (id == l.id) def = l.defaultOn;
  m_on.erase(id);
  m_off.erase(id);
  if (on != def) (on ? m_on : m_off).insert(id);
  saveState();
  if (on && !exists(listPath(id)) && !exists(m_bundled + "/" + id + ".txt")) startDownloads(false);  // rebuilds when done
  else rebuild();
}
void Adblock::updateNow() { startDownloads(true); }

bool Adblock::siteExcepted(const std::string& host) const {
  for (std::string h = host; !h.empty();) {
    if (m_sites.count(h)) return true;
    size_t d = h.find('.');
    if (d == std::string::npos) break;
    h = h.substr(d + 1);
  }
  return false;
}
void Adblock::setSiteExcepted(const std::string& host, bool ex) {
  std::string h = hostOf("x://" + host);
  if (h.empty()) return;
  if (ex) m_sites.insert(h); else m_sites.erase(h);
  saveState();
  m_host.clear();  // re-evaluate on the next navigation
  apply();
}

std::string Adblock::status() const {
  if (!m_enabled) return tl("Đang tắt");
  if (m_busy && !m_rules) return tl("Đang chuẩn bị bộ lọc…");
  std::string s = std::to_string(m_rules) + tl(" quy tắc");
  if (m_lastUpdate) {
    char b[32];
    time_t t = m_lastUpdate;
    strftime(b, sizeof b, "%d/%m/%Y", localtime(&t));
    s += tl(" · cập nhật ") + std::string(b);
  }
  if (m_busy) s += tl(" · đang cập nhật…");
  return s;
}

// Test helper: compile a content-blocker JSON file, report time and peak memory.
int Adblock::compileTest(const std::string& jsonPath, const std::string& storeDir) {
  std::string json = readFile(jsonPath);
  WebKitUserContentFilterStore* store = webkit_user_content_filter_store_new(storeDir.c_str());
  GBytes* b = g_bytes_new(json.data(), json.size());
  struct R { bool done = false; bool ok = false; std::string err; } r;
  gint64 t0 = g_get_monotonic_time();
  webkit_user_content_filter_store_save(store, "test", b, nullptr,
      +[](GObject* src, GAsyncResult* res, gpointer data) {
        auto* r = static_cast<R*>(data);
        GError* e = nullptr;
        WebKitUserContentFilter* f = webkit_user_content_filter_store_save_finish(WEBKIT_USER_CONTENT_FILTER_STORE(src), res, &e);
        r->ok = f != nullptr;
        if (e) { r->err = e->message; g_error_free(e); }
        if (f) webkit_user_content_filter_unref(f);
        r->done = true;
      },
      &r);
  while (!r.done) g_main_context_iteration(nullptr, TRUE);
  long hwm = 0;
  std::ifstream st("/proc/self/status");
  std::string l;
  while (std::getline(st, l)) if (l.rfind("VmHWM:", 0) == 0) hwm = atol(l.c_str() + 6);
  printf("compile %s: %s %lld ms, peak RSS %ld MB %s\n", jsonPath.c_str(), r.ok ? "ok" : "FAILED",
         (long long)(g_get_monotonic_time() - t0) / 1000, hwm / 1024, r.err.c_str());
  g_bytes_unref(b);
  return r.ok ? 0 : 1;
}
