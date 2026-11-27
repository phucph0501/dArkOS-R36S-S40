// WpeView - embeds a WPE WebKit web view into an SDL application.
// Frames arrive as wl_shm buffers through WPEBackend-fdo's SHM export (no compositor, no EGL needed
// in the UI process) and are copied into an ARGB8888 buffer the SDL shell uploads to a texture.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct _WebKitWebView;

class WpeView {
 public:
  std::string extensionsDir, extensionData;  // web-process extension (set before init)
  bool init(int width, int height, const std::string& dataDir, const std::string& cacheDir);
  void shutdown();
  void pump();  // run pending GLib main-loop work (call every frame)

  // navigation
  void load(const std::string& uri);
  void loadHtml(const std::string& html, const std::string& baseUri);
  void back();
  void forward();
  bool canBack() const;
  bool canForward() const;
  void reload();
  void stop();
  std::string uri() const;
  std::string title() const;
  double progress() const;       // 0..1
  bool loading() const;
  const std::string& error() const { return m_error; }

  // frame
  bool frameDirty() const { return m_dirty; }
  void clearDirty() { m_dirty = false; }
  const uint32_t* pixels() const { return m_pixels.data(); }
  int frameW() const { return m_fw; }
  int frameH() const { return m_fh; }
  void resize(int w, int h);
  void framePresented();  // call after the shell presented a frame: lets WebKit render the next one

  // input (coordinates in view pixels)
  void pointerMove(int x, int y);
  void click(int x, int y);
  void scroll(int x, int y, int dy);  // dy > 0 scrolls the page down
  void key(uint32_t keysym, bool pressed = true);
  void keyTap(uint32_t keysym);

  // features
  void runJs(const std::string& script, std::function<void(const std::string&)> done = nullptr);
  void find(const std::string& text);
  void findNext();
  void findFinish();
  int lastFindCount() const { return m_findCount; }
  void setMobile(bool mobile);
  void setImages(bool on);
  void setJs(bool on);
  void setZoom(double z);

  // downloads (files WebKit cannot display, attachments, or download(uri))
  struct Download {
    std::string url, path, name, error;
    uint64_t received = 0, total = 0;
    enum State { Running, Done, Failed, Cancelled } state = Running;
    void* dl = nullptr;  // WebKitDownload*
    class WpeView* owner = nullptr;
  };
  std::vector<std::shared_ptr<Download>> downloads;
  std::function<std::string(const std::string& suggested, const std::string& url)> downloadDest;  // full path
  std::function<void(const Download&)> onDownloadEvent;  // destination chosen / finished
  void download(const std::string& uri);
  void cancelDownload(Download& d);
  void addDownload(void* webkitDownload);

  // DOM fullscreen (video): the shell resizes the view to the whole screen while it is on
  bool fullscreen() const { return m_fullscreen; }
  void exitFullscreen();
  std::function<void(bool)> onFullscreen;
  void onFullscreenRequest(bool enable);

  // internal (GLib callbacks)
  _WebKitWebView* webView() const { return m_view; }
  std::function<void(const std::string& uri)> onLoadStart;  // fired when a navigation starts

  void onShm(void* exportedBuffer);
  void onLoad(int event);
  void onFailed(const std::string& msg);
  void onCrashed(bool memory);
  int m_findCount = -1;

 private:
  _WebKitWebView* m_view = nullptr;
  void* m_session = nullptr;  // WebKitNetworkSession*
  bool m_fullscreen = false;
  void* m_exportable = nullptr;  // wpe_view_backend_exportable_fdo*
  void* m_backend = nullptr;     // wpe_view_backend*
  std::vector<uint32_t> m_pixels;
  int m_fw = 0, m_fh = 0;
  bool m_dirty = false, m_loading = false, m_framePending = false;
  std::string m_error;
};
