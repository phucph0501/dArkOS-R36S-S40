// ytpad: gamepad -> mpv for the S40 YouTube app. Reads the pad directly (SDL game controller, same mapping file as the
// launcher) and sends commands to mpv's IPC socket. (Keyboard emulation through gptokeyb never reaches mpv when it draws
// straight to the screen: there is no window or terminal for the keys to go to.)
//
//   D-pad / left stick  LEFT, RIGHT : seek 10 s per press; held: 20, 30, 60 s per repeat (60 s is the maximum)
//                       UP, DOWN    : video brightness + / -   (held: repeats)
//   A pause / play,  B back (quits mpv),  L1 / R1 volume - / +,  X mute
//
// usage: ytpad <mpv-ipc-socket>            YTPAD_SIM=1 ytpad <socket> < script   (test: "t_ms press|release left|right|up|down|a|b")
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static const char* SOCK;

static void mpv(const char* json) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return;
  struct sockaddr_un a; memset(&a, 0, sizeof a);
  a.sun_family = AF_UNIX; strncpy(a.sun_path, SOCK, sizeof a.sun_path - 1);
  if (connect(fd, (struct sockaddr*)&a, sizeof a) == 0) {
    char buf[256]; int n = snprintf(buf, sizeof buf, "{\"command\":%s}\n", json);
    if (write(fd, buf, n) < 0) {}
  }
  close(fd);
}

enum { LEFT, RIGHT, UP, DOWN, NDIR };
static const int STEPS[] = {10, 20, 30, 60};   // seconds: the first press, then each repeat while held (capped at 60)
struct Hold { int down; Uint32 next; int rep; };
static struct Hold H[NDIR];

static void act(int d, int first) {
  char j[96];
  if (d == LEFT || d == RIGHT) {
    int idx = first ? 0 : (H[d].rep < 3 ? ++H[d].rep : 3);
    H[d].rep = idx;
    snprintf(j, sizeof j, "[\"osd-auto\",\"seek\",%d,\"relative\"]", (d == LEFT ? -1 : 1) * STEPS[idx]);
  } else {
    snprintf(j, sizeof j, "[\"osd-auto\",\"add\",\"brightness\",%d]", d == UP ? 3 : -3);
  }
  mpv(j);
}
static void press(int d) { if (H[d].down) return; H[d].down = 1; H[d].rep = 0; act(d, 1); H[d].next = SDL_GetTicks() + 450; }
static void release(int d) { H[d].down = 0; }
static void button(int b) {  // b: 'a' pause, 'b' quit, 'l' vol-, 'r' vol+, 'x' mute
  switch (b) {
    case 'a': mpv("[\"osd-auto\",\"cycle\",\"pause\"]"); break;
    case 'b': mpv("[\"quit\"]"); break;
    case 'l': mpv("[\"osd-auto\",\"add\",\"volume\",-5]"); break;
    case 'r': mpv("[\"osd-auto\",\"add\",\"volume\",5]"); break;
    case 'x': mpv("[\"osd-auto\",\"cycle\",\"mute\"]"); break;
  }
}
static void repeats(void) {
  Uint32 now = SDL_GetTicks();
  for (int d = 0; d < NDIR; d++)
    if (H[d].down && now >= H[d].next) { act(d, 0); H[d].next = now + ((d == LEFT || d == RIGHT) ? 380 : 110); }
}

struct Sim { unsigned t; int press; int what; };   // what: dir 0..3 or 100+char for a button
static int simMain(void) {
  struct Sim s[256]; int n = 0; char line[128];
  while (n < 256 && fgets(line, sizeof line, stdin)) {
    unsigned t; char act[16], what[16];
    if (sscanf(line, "%u %15s %15s", &t, act, what) != 3) continue;
    int w = !strcmp(what, "left") ? LEFT : !strcmp(what, "right") ? RIGHT : !strcmp(what, "up") ? UP : !strcmp(what, "down") ? DOWN : 100 + what[0];
    s[n].t = t; s[n].press = !strcmp(act, "press"); s[n].what = w; n++;
  }
  Uint32 t0 = SDL_GetTicks(); int i = 0;
  while (i < n || 1) {
    unsigned el = SDL_GetTicks() - t0;
    while (i < n && s[i].t <= el) {
      if (s[i].what < NDIR) { if (s[i].press) press(s[i].what); else release(s[i].what); }
      else if (s[i].press) button(s[i].what - 100);
      i++;
    }
    repeats();
    if (i >= n && el > s[n ? n - 1 : 0].t + 600) break;
    SDL_Delay(10);
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: ytpad <mpv-ipc-socket>\n"); return 2; }
  SOCK = argv[1];
  if (getenv("YTPAD_SIM")) { SDL_Init(SDL_INIT_TIMER); return simMain(); }
  const char* db = getenv("SDL_GAMECONTROLLERCONFIG_FILE");
  if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  if (db) SDL_GameControllerAddMappingsFromFile(db);
  int opened = 0;
  for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i) && SDL_GameControllerOpen(i)) opened++;
  fprintf(stderr, "ytpad: %d game controller(s) of %d joystick(s)\n", opened, SDL_NumJoysticks());
  int axis[2] = {0, 0};   // left stick state: -1, 0, 1 for X then Y
  SDL_Event e;
  for (;;) {
    while (SDL_WaitEventTimeout(&e, 15)) {
      if (e.type == SDL_QUIT) return 0;
      if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
      if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) {
        int dn = e.type == SDL_CONTROLLERBUTTONDOWN;
        switch (e.cbutton.button) {
          case SDL_CONTROLLER_BUTTON_DPAD_LEFT: dn ? press(LEFT) : release(LEFT); break;
          case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: dn ? press(RIGHT) : release(RIGHT); break;
          case SDL_CONTROLLER_BUTTON_DPAD_UP: dn ? press(UP) : release(UP); break;
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN: dn ? press(DOWN) : release(DOWN); break;
          case SDL_CONTROLLER_BUTTON_A: if (dn) button('a'); break;
          case SDL_CONTROLLER_BUTTON_B: if (dn) button('b'); break;
          case SDL_CONTROLLER_BUTTON_X: if (dn) button('x'); break;
          case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: if (dn) button('l'); break;
          case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: if (dn) button('r'); break;
          case SDL_CONTROLLER_BUTTON_START: if (dn) button('b'); break;
        }
      } else if (e.type == SDL_CONTROLLERAXISMOTION) {
        int ax = e.caxis.axis, v = e.caxis.value, s = v > 16000 ? 1 : v < -16000 ? -1 : 0;
        if (ax == SDL_CONTROLLER_AXIS_LEFTX && s != axis[0]) {
          if (axis[0]) release(axis[0] < 0 ? LEFT : RIGHT);
          axis[0] = s; if (s) press(s < 0 ? LEFT : RIGHT);
        } else if (ax == SDL_CONTROLLER_AXIS_LEFTY && s != axis[1]) {
          if (axis[1]) release(axis[1] < 0 ? UP : DOWN);
          axis[1] = s; if (s) press(s < 0 ? UP : DOWN);
        }
      }
    }
    repeats();
  }
}
