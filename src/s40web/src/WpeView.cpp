#include "s40tr.h"
#include "WpeView.h"

#include <wpe/webkit.h>
#include <wpe/fdo.h>
#include <wpe/unstable/fdo-shm.h>
#include <wayland-server.h>
#include <glib.h>
#include <sys/time.h>
#include <cstring>

static uint32_t nowMs() { struct timeval tv; gettimeofday(&tv, nullptr); return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000); }

static void shmBufferCb(void* data, struct wpe_fdo_shm_exported_buffer* buffer) { static_cast<WpeView*>(data)->onShm(buffer); }

static struct wpe_view_backend_exportable_fdo_client s_client = {
    nullptr,      // export_buffer_resource
    nullptr,      // export_dmabuf_resource
    shmBufferCb,  // export_shm_buffer
    nullptr, nullptr,
};

void WpeView::onShm(void* exported) {
  auto* buf = static_cast<struct wpe_fdo_shm_exported_buffer*>(exported);
  struct wl_shm_buffer* shm = wpe_fdo_shm_exported_buffer_get_shm_buffer(buf);
  wl_shm_buffer_begin_access(shm);
  int w = wl_shm_buffer_get_width(shm), h = wl_shm_buffer_get_height(shm), stride = wl_shm_buffer_get_stride(shm);
  const uint8_t* src = static_cast<const uint8_t*>(wl_shm_buffer_get_data(shm));
  if (w > 0 && h > 0 && src) {
    if (w != m_fw || h != m_fh) { m_fw = w; m_fh = h; m_pixels.assign((size_t)w * h, 0xffffffff); }
    for (int y = 0; y < h; y++) memcpy(&m_pixels[(size_t)y * w], src + (size_t)y * stride, (size_t)w * 4);
    m_dirty = true;
  }
  wl_shm_buffer_end_access(shm);
  auto* exp = static_cast<struct wpe_view_backend_exportable_fdo*>(m_exportable);
  wpe_view_backend_exportable_fdo_dispatch_release_shm_exported_buffer(exp, buf);
  m_framePending = true;  // frame_complete is sent after the shell presents (vsync-paced)
}

void WpeView::framePresented() {
  if (!m_framePending) return;
  m_framePending = false;
  wpe_view_backend_exportable_fdo_dispatch_frame_complete(static_cast<struct wpe_view_backend_exportable_fdo*>(m_exportable));
}

void WpeView::onLoad(int event) {
  if (event == WEBKIT_LOAD_STARTED) { m_loading = true; m_error.clear(); if (onLoadStart) onLoadStart(uri()); }
  else if (event == WEBKIT_LOAD_FINISHED) m_loading = false;
}
void WpeView::onFailed(const std::string& msg) { m_loading = false; m_error = msg; }
void WpeView::onCrashed(bool memory) {
  m_loading = false;
  m_error = memory ? tl("Trang dùng quá nhiều bộ nhớ nên đã bị đóng") : tl("Bộ hiển thị trang gặp lỗi và đã dừng");
  if (m_fullscreen) onFullscreenRequest(false);
}

// ---- downloads ----
void WpeView::addDownload(void* p) {
  auto* d = static_cast<WebKitDownload*>(p);
  for (auto& x : downloads) if (x->dl == d) return;
  auto info = std::make_shared<Download>();
  info->dl = g_object_ref(d);
  info->owner = this;
  WebKitURIRequest* r = webkit_download_get_request(d);
  info->url = r ? webkit_uri_request_get_uri(r) : "";
  downloads.push_back(info);
  Download* i = info.get();
  g_signal_connect(d, "decide-destination", G_CALLBACK(+[](WebKitDownload* d, gchar* suggested, gpointer p) -> gboolean {
                     auto* i = static_cast<Download*>(p);
                     std::string name = suggested && *suggested ? suggested : "download";
                     i->path = i->owner->downloadDest ? i->owner->downloadDest(name, i->url) : "/tmp/" + name;
                     i->name = i->path.substr(i->path.rfind('/') + 1);
                     webkit_download_set_allow_overwrite(d, FALSE);
                     webkit_download_set_destination(d, i->path.c_str());
                     if (i->owner->onDownloadEvent) i->owner->onDownloadEvent(*i);
                     return TRUE;
                   }), i);
  g_signal_connect(d, "received-data", G_CALLBACK(+[](WebKitDownload* d, guint64, gpointer p) {
                     auto* i = static_cast<Download*>(p);
                     i->received = webkit_download_get_received_data_length(d);
                     WebKitURIResponse* r = webkit_download_get_response(d);
                     if (r) i->total = webkit_uri_response_get_content_length(r);
                   }), i);
  g_signal_connect(d, "failed", G_CALLBACK(+[](WebKitDownload*, GError* e, gpointer p) {
                     auto* i = static_cast<Download*>(p);
                     bool cancelled = e && e->domain == WEBKIT_DOWNLOAD_ERROR && e->code == WEBKIT_DOWNLOAD_ERROR_CANCELLED_BY_USER;
                     i->state = cancelled ? Download::Cancelled : Download::Failed;
                     i->error = e ? e->message : "";
                   }), i);
  g_signal_connect(d, "finished", G_CALLBACK(+[](WebKitDownload*, gpointer p) {
                     auto* i = static_cast<Download*>(p);
                     if (i->state == Download::Running) { i->state = Download::Done; if (i->total == 0) i->total = i->received; }
                     if (i->owner->onDownloadEvent) i->owner->onDownloadEvent(*i);
                   }), i);
}
void WpeView::download(const std::string& uri) {
  WebKitDownload* d = webkit_network_session_download_uri(static_cast<WebKitNetworkSession*>(m_session), uri.c_str());
  if (d) { addDownload(d); g_object_unref(d); }
}
void WpeView::cancelDownload(Download& d) {
  if (d.state == Download::Running && d.dl) webkit_download_cancel(static_cast<WebKitDownload*>(d.dl));
}

// ---- fullscreen ----
void WpeView::onFullscreenRequest(bool enable) {
  if (enable == m_fullscreen) return;
  m_fullscreen = enable;
  if (onFullscreen) onFullscreen(enable);  // shell resizes the view
  g_idle_add(+[](gpointer self) -> gboolean {
               auto* v = static_cast<WpeView*>(self);
               auto* b = static_cast<struct wpe_view_backend*>(v->m_backend);
               if (v->m_fullscreen) wpe_view_backend_dispatch_did_enter_fullscreen(b);
               else wpe_view_backend_dispatch_did_exit_fullscreen(b);
               return G_SOURCE_REMOVE;
             }, this);
}
void WpeView::exitFullscreen() {
  if (m_fullscreen) wpe_view_backend_dispatch_request_exit_fullscreen(static_cast<struct wpe_view_backend*>(m_backend));
}

bool WpeView::init(int width, int height, const std::string& dataDir, const std::string& cacheDir) {
  if (!wpe_loader_init("libWPEBackend-fdo-1.0.so.1")) return false;
  if (!wpe_fdo_initialize_shm()) return false;
  auto* exp = wpe_view_backend_exportable_fdo_create(&s_client, this, width, height);
  if (!exp) return false;
  m_exportable = exp;
  auto* vb = wpe_view_backend_exportable_fdo_get_view_backend(exp);
  m_backend = vb;
  WebKitWebViewBackend* wvb = webkit_web_view_backend_new(vb, (GDestroyNotify)wpe_view_backend_exportable_fdo_destroy, exp);

  // 1 GB device: make WebKit release caches early and kill runaway web processes.
  WebKitMemoryPressureSettings* mps = webkit_memory_pressure_settings_new();
  const char* lim = getenv("S40WEB_MEMLIMIT_MB");                         // override (tests / heavy sites)
  webkit_memory_pressure_settings_set_memory_limit(mps, lim && atoi(lim) > 0 ? atoi(lim) : 320);  // MB per web process
  webkit_memory_pressure_settings_set_conservative_threshold(mps, 0.35);
  webkit_memory_pressure_settings_set_strict_threshold(mps, 0.6);
  webkit_memory_pressure_settings_set_kill_threshold(mps, 1.4);          // > limit*1.4 -> web process killed, page reports an error
  webkit_memory_pressure_settings_set_poll_interval(mps, 2.0);
  webkit_network_session_set_memory_pressure_settings(mps);
  WebKitWebContext* ctx = WEBKIT_WEB_CONTEXT(g_object_new(WEBKIT_TYPE_WEB_CONTEXT, "memory-pressure-settings", mps, nullptr));
  webkit_memory_pressure_settings_free(mps);
  if (!extensionsDir.empty()) {
    webkit_web_context_set_web_process_extensions_directory(ctx, extensionsDir.c_str());
    webkit_web_context_set_web_process_extensions_initialization_user_data(ctx, g_variant_new_string(extensionData.c_str()));
  }
  webkit_web_context_set_cache_model(ctx, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);  // smallest in-memory caches
  WebKitNetworkSession* ns = webkit_network_session_new(dataDir.c_str(), cacheDir.c_str());
  webkit_cookie_manager_set_persistent_storage(webkit_network_session_get_cookie_manager(ns), (dataDir + "/cookies.sqlite").c_str(),
                                               WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
  WebKitSettings* st = webkit_settings_new();
  webkit_settings_set_enable_developer_extras(st, FALSE);
  webkit_settings_set_enable_page_cache(st, FALSE);  // keep RAM low on 1 GB devices
  webkit_settings_set_enable_smooth_scrolling(st, FALSE);
  webkit_settings_set_media_playback_requires_user_gesture(st, TRUE);
  m_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "backend", wvb, "web-context", ctx, "network-session", ns, "settings", st, nullptr));
  g_object_unref(ctx);
  m_session = ns;  // keep our reference: downloads are started on the session
  g_object_unref(st);
  if (!m_view) return false;

  g_signal_connect(m_view, "load-changed", G_CALLBACK(+[](WebKitWebView*, WebKitLoadEvent ev, gpointer self) { static_cast<WpeView*>(self)->onLoad((int)ev); }), this);
  g_signal_connect(m_view, "load-failed", G_CALLBACK(+[](WebKitWebView*, WebKitLoadEvent, gchar*, GError* err, gpointer self) -> gboolean {
                     if (err && err->code != WEBKIT_NETWORK_ERROR_CANCELLED) static_cast<WpeView*>(self)->onFailed(err->message);
                     return FALSE;
                   }), this);
  g_signal_connect(m_view, "web-process-terminated", G_CALLBACK(+[](WebKitWebView*, WebKitWebProcessTerminationReason r, gpointer self) {
                     static_cast<WpeView*>(self)->onCrashed(r == WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT);
                   }), this);
  // download what WebKit cannot show (zip, apk, jar, ...) and anything sent as an attachment
  g_signal_connect(m_view, "decide-policy", G_CALLBACK(+[](WebKitWebView*, WebKitPolicyDecision* dec, WebKitPolicyDecisionType type, gpointer) -> gboolean {
                     if (type != WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
                       // "open in app" links (snssdk://, intent://, market://...) cannot work here: ignore them
                       // instead of navigating to an error page
                       auto* nd = WEBKIT_NAVIGATION_POLICY_DECISION(dec);
                       WebKitURIRequest* req = webkit_navigation_action_get_request(webkit_navigation_policy_decision_get_navigation_action(nd));
                       const char* u = req ? webkit_uri_request_get_uri(req) : nullptr;
                       if (u) {
                         std::string s(u);
                         std::string scheme = s.substr(0, s.find(':'));
                         if (scheme != "http" && scheme != "https" && scheme != "file" && scheme != "about" && scheme != "data" && scheme != "blob") {
                           webkit_policy_decision_ignore(dec);
                           return TRUE;
                         }
                       }
                       return FALSE;
                     }
                     auto* rd = WEBKIT_RESPONSE_POLICY_DECISION(dec);
                     if (!webkit_response_policy_decision_is_main_frame_main_resource(rd)) return FALSE;
                     WebKitURIResponse* resp = webkit_response_policy_decision_get_response(rd);
                     SoupMessageHeaders* h = webkit_uri_response_get_http_headers(resp);
                     const char* cd = h ? soup_message_headers_get_one(h, "Content-Disposition") : nullptr;
                     bool attachment = cd && g_ascii_strncasecmp(cd, "attachment", 10) == 0;
                     if (attachment || !webkit_response_policy_decision_is_mime_type_supported(rd)) {
                       webkit_policy_decision_download(dec);
                       return TRUE;
                     }
                     return FALSE;
                   }), this);
  g_signal_connect(ns, "download-started", G_CALLBACK(+[](WebKitNetworkSession*, WebKitDownload* d, gpointer self) {
                     static_cast<WpeView*>(self)->addDownload(d);
                   }), this);
  wpe_view_backend_set_fullscreen_handler(vb, +[](void* self, bool enable) -> bool {
                                            static_cast<WpeView*>(self)->onFullscreenRequest(enable);
                                            return true;
                                          }, this);

  WebKitFindController* fc = webkit_web_view_get_find_controller(m_view);
  g_signal_connect(fc, "found-text", G_CALLBACK(+[](WebKitFindController*, guint n, gpointer self) { static_cast<WpeView*>(self)->m_findCount = (int)n; }), this);
  g_signal_connect(fc, "failed-to-find-text", G_CALLBACK(+[](WebKitFindController*, gpointer self) { static_cast<WpeView*>(self)->m_findCount = 0; }), this);

  // visible + focused so the page renders and receives keys
  wpe_view_backend_add_activity_state((struct wpe_view_backend*)m_backend, wpe_view_activity_state_visible | wpe_view_activity_state_focused | wpe_view_activity_state_in_window);
  m_fw = width; m_fh = height;
  m_pixels.assign((size_t)width * height, 0xffffffff);
  return true;
}

void WpeView::shutdown() {
  if (m_view) { g_object_unref(m_view); m_view = nullptr; }
}
void WpeView::pump() {
  for (int i = 0; i < 50 && g_main_context_iteration(nullptr, FALSE); i++) {}
}
void WpeView::resize(int w, int h) { wpe_view_backend_dispatch_set_size((struct wpe_view_backend*)m_backend, w, h); }

void WpeView::load(const std::string& uri) { m_error.clear(); webkit_web_view_load_uri(m_view, uri.c_str()); }
void WpeView::loadHtml(const std::string& html, const std::string& base) { webkit_web_view_load_html(m_view, html.c_str(), base.c_str()); }
void WpeView::back() { webkit_web_view_go_back(m_view); }
void WpeView::forward() { webkit_web_view_go_forward(m_view); }
bool WpeView::canBack() const { return webkit_web_view_can_go_back(m_view); }
bool WpeView::canForward() const { return webkit_web_view_can_go_forward(m_view); }
void WpeView::reload() { webkit_web_view_reload(m_view); }
void WpeView::stop() { webkit_web_view_stop_loading(m_view); }
std::string WpeView::uri() const { const char* u = webkit_web_view_get_uri(m_view); return u ? u : ""; }
std::string WpeView::title() const { const char* t = webkit_web_view_get_title(m_view); return t ? t : ""; }
double WpeView::progress() const { return webkit_web_view_get_estimated_load_progress(m_view); }
bool WpeView::loading() const { return webkit_web_view_is_loading(m_view); }

void WpeView::pointerMove(int x, int y) {
  struct wpe_input_pointer_event ev = {wpe_input_pointer_event_type_motion, nowMs(), x, y, 0, 0, 0};
  wpe_view_backend_dispatch_pointer_event((struct wpe_view_backend*)m_backend, &ev);
}
void WpeView::click(int x, int y) {
  pointerMove(x, y);
  struct wpe_input_pointer_event down = {wpe_input_pointer_event_type_button, nowMs(), x, y, 1, 1, wpe_input_pointer_modifier_button1};
  wpe_view_backend_dispatch_pointer_event((struct wpe_view_backend*)m_backend, &down);
  struct wpe_input_pointer_event up = {wpe_input_pointer_event_type_button, nowMs(), x, y, 1, 0, 0};
  wpe_view_backend_dispatch_pointer_event((struct wpe_view_backend*)m_backend, &up);
}
void WpeView::scroll(int x, int y, int dy) {
  struct wpe_input_axis_2d_event ev = {};
  ev.base.type = (enum wpe_input_axis_event_type)(wpe_input_axis_event_type_mask_2d | wpe_input_axis_event_type_motion_smooth);
  ev.base.time = nowMs();
  ev.base.x = x;
  ev.base.y = y;
  ev.x_axis = 0;
  ev.y_axis = dy;
  wpe_view_backend_dispatch_axis_event((struct wpe_view_backend*)m_backend, &ev.base);
}
void WpeView::key(uint32_t keysym, bool pressed) {
  struct wpe_input_keyboard_event ev = {nowMs(), keysym, 0, pressed, 0};
  wpe_view_backend_dispatch_keyboard_event((struct wpe_view_backend*)m_backend, &ev);
}
void WpeView::keyTap(uint32_t keysym) { key(keysym, true); key(keysym, false); }

struct JsCall { std::function<void(const std::string&)> done; };
void WpeView::runJs(const std::string& script, std::function<void(const std::string&)> done) {
  auto* call = new JsCall{std::move(done)};
  webkit_web_view_evaluate_javascript(m_view, script.c_str(), (gssize)script.size(), nullptr, nullptr, nullptr,
      +[](GObject* obj, GAsyncResult* res, gpointer data) {
        auto* c = static_cast<JsCall*>(data);
        GError* err = nullptr;
        JSCValue* v = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(obj), res, &err);
        std::string out;
        if (v) { if (!jsc_value_is_undefined(v) && !jsc_value_is_null(v)) { char* s = jsc_value_to_string(v); out = s ? s : ""; g_free(s); } g_object_unref(v); }
        if (err) g_error_free(err);
        if (c->done) c->done(out);
        delete c;
      },
      call);
}

void WpeView::find(const std::string& text) {
  m_findCount = -1;
  webkit_find_controller_search(webkit_web_view_get_find_controller(m_view), text.c_str(),
                                WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND, G_MAXUINT);
}
void WpeView::findNext() { webkit_find_controller_search_next(webkit_web_view_get_find_controller(m_view)); }
void WpeView::findFinish() { webkit_find_controller_search_finish(webkit_web_view_get_find_controller(m_view)); }

void WpeView::setMobile(bool mobile) {
  WebKitSettings* st = webkit_web_view_get_settings(m_view);
  if (mobile)
    webkit_settings_set_user_agent(st, "Mozilla/5.0 (Linux; Android 10; R36S) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile Safari/605.1.15");
  else
    webkit_settings_set_user_agent(st, "Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15");
}
void WpeView::setImages(bool on) { webkit_settings_set_auto_load_images(webkit_web_view_get_settings(m_view), on); }
void WpeView::setJs(bool on) { webkit_settings_set_enable_javascript(webkit_web_view_get_settings(m_view), on); }
void WpeView::setZoom(double z) { webkit_web_view_set_zoom_level(m_view, z); }
