// Terminal: bash in a pseudo-terminal, VT100/xterm emulation by libvterm, drawn with DejaVu Sans Mono.
// Controls: D-pad = arrows, A/Start = Enter, B = Backspace, R = Tab, L = keyboard (type + run),
// X/Y/Select = menu (Ctrl keys, Esc, quick commands, scroll back, font size, quit).
#include "s40.h"
#include "s40kb.h"
#include <SDL2/SDL_ttf.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <deque>
#include <map>
#include <vector>
extern "C" {
#include <vterm.h>
}

namespace s40 {

struct Term : Screen {
  VTerm* vt = nullptr;
  VTermScreen* vs = nullptr;
  int fd = -1;
  pid_t pid = -1;
  bool exited = false;
  int rows = 0, cols = 0, cw = 8, ch = 16, fsize = 14;
  TTF_Font* font = nullptr;
  TTF_Font* fontB = nullptr;
  TTF_Font* cjk = nullptr;
  std::map<uint64_t, SDL_Texture*> glyphs;  // (bold << 32 | codepoint) -> white glyph
  VTermPos cursor{0, 0};
  bool cursorVisible = true;
  std::deque<std::vector<VTermScreenCell>> sb;  // scroll-back, oldest first
  int scroll = 0;                                // lines scrolled back (0 = live)
  static constexpr int HINT = 20;

  Term() { full = true; live = true; capture = true; noIdle = true; idleFps = 10; ic = "term"; }
  ~Term() override {
    if (pid > 0) { kill(pid, SIGHUP); waitpid(pid, nullptr, WNOHANG); }
    if (fd >= 0) close(fd);
    if (vt) vterm_free(vt);
    clearGlyphs();
    if (font) TTF_CloseFont(font);
    if (fontB) TTF_CloseFont(fontB);
    if (cjk) TTF_CloseFont(cjk);
  }
  std::string title() override { return tl("Terminal"); }

  void clearGlyphs() { for (auto& g : glyphs) SDL_DestroyTexture(g.second); glyphs.clear(); }
  void openFonts() {
    clearGlyphs();
    if (font) TTF_CloseFont(font);
    if (fontB) TTF_CloseFont(fontB);
    if (cjk) TTF_CloseFont(cjk);
    font = TTF_OpenFont((G.assetDir + "/fonts/DejaVuSansMono.ttf").c_str(), fsize);
    fontB = TTF_OpenFont((G.assetDir + "/fonts/DejaVuSansMono-Bold.ttf").c_str(), fsize);
    cjk = TTF_OpenFont("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", fsize);
    if (!font) return;
    int adv = 8;
    TTF_GlyphMetrics32(font, 'M', nullptr, nullptr, nullptr, nullptr, &adv);
    cw = std::max(4, adv);
    ch = std::max(8, TTF_FontLineSkip(font));
    cols = W / cw;
    rows = (H - HINT) / ch;
  }

  // ---- libvterm callbacks ----
  static int cbMoveCursor(VTermPos pos, VTermPos, int visible, void* u) {
    auto* t = (Term*)u; t->cursor = pos; t->cursorVisible = visible; return 1;
  }
  static int cbSetProp(VTermProp p, VTermValue* v, void* u) {
    if (p == VTERM_PROP_CURSORVISIBLE) ((Term*)u)->cursorVisible = v->boolean;
    return 1;
  }
  static int cbPushLine(int n, const VTermScreenCell* cells, void* u) {
    auto* t = (Term*)u;
    t->sb.emplace_back(cells, cells + n);
    if (t->sb.size() > 1000) t->sb.pop_front();
    return 1;
  }
  static int cbPopLine(int n, VTermScreenCell* cells, void* u) {
    auto* t = (Term*)u;
    if (t->sb.empty()) return 0;
    auto& l = t->sb.back();
    for (int i = 0; i < n; i++) {
      if (i < (int)l.size()) cells[i] = l[i];
      else { memset(&cells[i], 0, sizeof(cells[i])); cells[i].width = 1; }
    }
    t->sb.pop_back();
    return 1;
  }
  static void cbOutput(const char* s, size_t len, void* u) {
    auto* t = (Term*)u;
    while (len > 0 && t->fd >= 0) {
      ssize_t w = write(t->fd, s, len);
      if (w <= 0) break;
      s += w; len -= w;
    }
  }

  void start() {
    openFonts();
    if (!font) { toast(tl("Thiếu font DejaVuSansMono"), "warn"); return; }
    vt = vterm_new(rows, cols);
    vterm_set_utf8(vt, 1);
    vterm_output_set_callback(vt, &Term::cbOutput, this);
    vs = vterm_obtain_screen(vt);
    static VTermScreenCallbacks cb{};
    cb.movecursor = &Term::cbMoveCursor;
    cb.settermprop = &Term::cbSetProp;
    cb.sb_pushline = &Term::cbPushLine;
    cb.sb_popline = &Term::cbPopLine;
    vterm_screen_set_callbacks(vs, &cb, this);
    vterm_screen_enable_altscreen(vs, 1);
    vterm_screen_reset(vs, 1);

    struct winsize ws{};
    ws.ws_row = rows; ws.ws_col = cols;
    pid = forkpty(&fd, nullptr, nullptr, &ws);
    if (pid == 0) {
      setenv("TERM", "xterm-256color", 1);
      setenv("COLORTERM", "truecolor", 1);
      if (!getenv("LANG")) setenv("LANG", "en_US.UTF-8", 1);
      unsetenv("SDL_VIDEODRIVER");
      const char* home = getenv("HOME");
      if (home && chdir(home) != 0) {}
      execl("/bin/bash", "bash", "-l", (char*)nullptr);
      _exit(127);
    }
    if (pid < 0) { toast(tl("Không mở được shell"), "warn"); fd = -1; return; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  }

  void onEnter() override { if (!vt) start(); }

  void resizeTo(int size) {
    fsize = size;
    openFonts();
    if (!vt) return;
    vterm_set_size(vt, rows, cols);
    struct winsize ws{};
    ws.ws_row = rows; ws.ws_col = cols;
    if (fd >= 0) ioctl(fd, TIOCSWINSZ, &ws);
    scroll = 0;
  }

  void tick(double) override {
    if (fd < 0) return;
    char buf[16384];
    for (int i = 0; i < 16; i++) {
      ssize_t n = read(fd, buf, sizeof buf);
      if (n > 0) { vterm_input_write(vt, buf, n); scroll = 0; extern Uint32 lastInput; lastInput = SDL_GetTicks(); continue; }
      break;
    }
    if (!exited && pid > 0 && waitpid(pid, nullptr, WNOHANG) == pid) {
      exited = true; pid = -1;
      std::string msg = std::string("\r\n\033[33m[") + tl("Shell đã thoát. Nhấn A để đóng, X để mở lại") + "]\033[0m\r\n";
      vterm_input_write(vt, msg.c_str(), msg.size());
    }
  }

  // ---- drawing ----
  Col color(VTermColor c, bool fg) {
    if (fg && VTERM_COLOR_IS_DEFAULT_FG(&c)) return {220, 223, 228};
    if (!fg && VTERM_COLOR_IS_DEFAULT_BG(&c)) return {16, 18, 24};
    vterm_screen_convert_color_to_rgb(vs, &c);
    return {c.rgb.red, c.rgb.green, c.rgb.blue};
  }
  SDL_Texture* glyph(uint32_t cp, bool bold) {
    uint64_t key = ((uint64_t)bold << 32) | cp;
    auto it = glyphs.find(key);
    if (it != glyphs.end()) return it->second;
    TTF_Font* f = bold && fontB ? fontB : font;
    if (!TTF_GlyphIsProvided32(f, cp) && cjk && TTF_GlyphIsProvided32(cjk, cp)) f = cjk;
    SDL_Surface* s = TTF_RenderGlyph32_Blended(f, cp, {255, 255, 255, 255});
    SDL_Texture* t = s ? SDL_CreateTextureFromSurface(G.ren, s) : nullptr;
    if (s) SDL_FreeSurface(s);
    glyphs[key] = t;
    return t;
  }
  void drawCell(const VTermScreenCell& c, int x, int y, bool cursorHere) {
    Col fg = color(c.fg, true), bg = color(c.bg, false);
    if (c.attrs.reverse) std::swap(fg, bg);
    if (cursorHere) std::swap(fg, bg);
    int w = cw * (c.width > 1 ? 2 : 1);
    if (bg.r != 16 || bg.g != 18 || bg.b != 24 || cursorHere) G.rect(x, y, w, ch, bg);
    if (c.chars[0] && c.chars[0] != ' ') {
      if (SDL_Texture* t = glyph(c.chars[0], c.attrs.bold)) {
        int tw, th; SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
        SDL_SetTextureColorMod(t, fg.r, fg.g, fg.b);
        SDL_Rect d{x + (w - tw) / 2, y + (ch - th) / 2, tw, th};
        SDL_RenderCopy(G.ren, t, nullptr, &d);
      }
    }
    if (c.attrs.underline) G.rect(x, y + ch - 2, w, 1, fg);
  }
  void draw(const SDL_Rect&) override {
    G.rect(0, 0, W, H, {16, 18, 24});
    if (!vt) { G.text(tl("Không mở được Terminal"), W / 2, H / 2, 18, {255, 255, 255}, false, 1); return; }
    int sbn = (int)sb.size();
    scroll = std::min(scroll, sbn);
    for (int r = 0; r < rows; r++) {
      int line = r - scroll;  // < 0: from scroll-back
      int y = r * ch;
      for (int c = 0; c < cols;) {
        VTermScreenCell cell{};
        if (line < 0) {
          auto& l = sb[sbn + line];
          if (c < (int)l.size()) cell = l[c]; else { cell.width = 1; cell.fg.type = VTERM_COLOR_DEFAULT_FG; cell.bg.type = VTERM_COLOR_DEFAULT_BG; }
        } else {
          vterm_screen_get_cell(vs, VTermPos{line, c}, &cell);
        }
        bool cur = scroll == 0 && cursorVisible && line == cursor.row && c == cursor.col && (SDL_GetTicks() / 500) % 2 == 0;
        drawCell(cell, c * cw, y, cur);
        c += cell.width > 1 ? 2 : 1;
      }
    }
    // hint bar
    const Theme& t = theme();
    G.grad(0, H - HINT, W, HINT, t.hl1, t.hl2);
    std::string hint = scroll ? tl("Đang xem lịch sử (") + std::to_string(scroll) + tl(" dòng) · ▲▼ cuộn · B về hiện tại")
                              : tl("A Enter · B Xóa · L Gõ lệnh · R Tab · X Menu");
    G.text(hint, W / 2, H - HINT + 2, 13, {255, 255, 255}, false, 1);
  }

  // ---- input ----
  void sendKey(VTermKey k, VTermModifier m = VTERM_MOD_NONE) { if (vt) vterm_keyboard_key(vt, k, m); }
  void sendCtrl(char c) { if (vt) vterm_keyboard_unichar(vt, (uint32_t)c, VTERM_MOD_CTRL); }
  void sendText(const std::string& s) {
    if (!vt) return;
    for (char32_t c : s40kb::toU32(s)) vterm_keyboard_unichar(vt, (uint32_t)c, VTERM_MOD_NONE);
  }
  void typeAndRun(bool run) {
    push(OSK(run ? tl("Gõ lệnh (Xong = chạy)") : tl("Gõ ký tự"), "", false, [this, run](const std::string& s) {
      sendText(s);
      if (run) sendKey(VTERM_KEY_ENTER);
    }));
  }
  void quickCmd(const std::string& c) { sendText(c); sendKey(VTERM_KEY_ENTER); }
  void menu() {
    std::vector<Opt> o = {
      {tl("Ctrl+C (dừng lệnh)"), "power", [this] { sendCtrl('c'); }},
      {tl("Gõ không chạy"), "note", [this] { typeAndRun(false); }},
      {"Esc", "prev", [this] { sendKey(VTERM_KEY_ESCAPE); }},
      {tl("Ctrl+D (thoát shell / EOF)"), "trash", [this] { sendCtrl('d'); }},
      {"Ctrl+Z", "pause", [this] { sendCtrl('z'); }},
      {tl("Ctrl+L (xóa màn hình)"), "restart", [this] { sendCtrl('l'); }},
      {tl("Cuộn lịch sử"), "list", [this] { scroll = std::min<int>(rows, sb.size()); }},
      {"Lệnh: dung lượng (df -h)", "chip", [this] { quickCmd("df -h"); }},
      {tl("Lệnh: bộ nhớ (free -h)"), "chip", [this] { quickCmd("free -h"); }},
      {tl("Lệnh: địa chỉ IP"), "wifi", [this] { quickCmd("ip -br addr"); }},
      {tl("Lệnh: tiến trình (top)"), "timer", [this] { quickCmd("top"); }},
      {std::string(tl("Cỡ chữ: ")) + (fsize == 12 ? tl("nhỏ") : fsize == 14 ? tl("vừa") : tl("lớn")), "sort",
       [this] { resizeTo(fsize == 12 ? 14 : fsize == 14 ? 17 : 12); }},
    };
    if (exited) o.insert(o.begin(), Opt{tl("Mở lại shell"), "restart", [this] { restart(); }});
    o.push_back({tl("Thoát Terminal"), "power", [] { pop(); }});
    openOptions(o, tl("Terminal"));
  }
  void restart() {
    if (fd >= 0) close(fd);
    if (vt) vterm_free(vt);
    vt = nullptr; vs = nullptr; fd = -1; pid = -1; exited = false; sb.clear(); scroll = 0;
    start();
  }
  void key(Key k) override {
    if (scroll > 0) {  // scroll-back mode
      if (k == Key::Up) scroll = std::min<int>(scroll + rows / 2, sb.size());
      else if (k == Key::Down) scroll = std::max(0, scroll - rows / 2);
      else if (k == Key::B || k == Key::OK) scroll = 0;
      else if (k == Key::A) menu();
      return;
    }
    if (exited && k == Key::OK) { pop(); return; }
    switch (k) {
      case Key::Up: sendKey(VTERM_KEY_UP); break;
      case Key::Down: sendKey(VTERM_KEY_DOWN); break;
      case Key::Left: sendKey(VTERM_KEY_LEFT); break;
      case Key::Right: sendKey(VTERM_KEY_RIGHT); break;
      case Key::OK: sendKey(VTERM_KEY_ENTER); break;
      case Key::B: sendKey(VTERM_KEY_BACKSPACE); break;
      case Key::R: sendKey(VTERM_KEY_TAB); break;
      case Key::L: typeAndRun(true); break;
      case Key::A: menu(); break;
      default: break;
    }
  }
};

Screen* TerminalApp() { return new Term; }

}  // namespace s40
