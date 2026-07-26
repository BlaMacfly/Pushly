// Pushly — portage Linux (GTK3 + GStreamer)
// Deux backends d'injection/raccourcis :
//  - Wayland : clavier virtuel uinput (/dev/uinput) + lecture des raccourcis
//    globaux via evdev (/dev/input/event*, nécessite le groupe "input")
//  - X11     : XTest + XGrabKey (aucune permission particulière)
// Le backend est choisi automatiquement selon la session (PUSHLY_BACKEND=x11
// ou uinput pour forcer).

#include <gtk/gtk.h>
#include <gst/gst.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <linux/uinput.h>
#include <linux/input-event-codes.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <random>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define PUSHLY_VERSION "1.0.1"

// ----------------------------------------------------------------------------
// État global
// ----------------------------------------------------------------------------
static std::atomic<bool> isSpamming(false);
static std::atomic<bool> appRunning(true);
static std::atomic<bool> hotkeysChanged(true);
static std::thread spamThread;
static std::thread hotkeyThread;

static std::string targetKey = "1";
static std::atomic<int> spamDelayMs(50);

// Raccourcis stockés en keyval/mods GDK (les keyvals GDK sont des keysyms X)
static std::atomic<guint> startKeyval(GDK_KEY_F9);
static std::atomic<guint> startMods(0);
static std::atomic<guint> stopKeyval(GDK_KEY_F10);
static std::atomic<guint> stopMods(0);

static bool soundMuted = false;
static int soundVolume = 300; // 0-1000 comme la version Windows

static GtkWidget *mainWindow = nullptr;
static GtkWidget *entryKey = nullptr;
static GtkWidget *spinDelay = nullptr;
static GtkWidget *btnHotkeyStart = nullptr;
static GtkWidget *btnHotkeyStop = nullptr;
static GtkWidget *btnStatus = nullptr;
static GtkWidget *chkMute = nullptr;
static GtkWidget *scaleVolume = nullptr;
static GtkWidget *lblBackend = nullptr;

static int capturingHotkey = 0; // 0 aucun, 1 Démarrer, 2 Arrêter
static bool loadingConfig = false;

static GstElement *soundPlayer = nullptr;
static std::string resourceDir;

static bool useUinput = false;
static int uinputFd = -1;

// ----------------------------------------------------------------------------
// Ressources (start.mp3 / stop.mp3)
// ----------------------------------------------------------------------------
static std::string FindResourceDir() {
  char exePath[PATH_MAX] = {0};
  ssize_t n = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
  std::string dir = ".";
  if (n > 0) {
    exePath[n] = '\0';
    std::string p(exePath);
    size_t slash = p.rfind('/');
    if (slash != std::string::npos)
      dir = p.substr(0, slash);
  }
  const std::string candidates[] = {dir + "/../share/pushly", dir};
  for (const auto &c : candidates) {
    std::string probe = c + "/start.mp3";
    if (access(probe.c_str(), R_OK) == 0)
      return c;
  }
  return dir;
}

static void PlaySound(const char *file) {
  if (soundMuted || !soundPlayer)
    return;
  std::string path = resourceDir + "/" + file;
  if (access(path.c_str(), R_OK) != 0)
    return;
  gst_element_set_state(soundPlayer, GST_STATE_NULL);
  gchar *uri = gst_filename_to_uri(path.c_str(), nullptr);
  g_object_set(soundPlayer, "uri", uri, "volume", soundVolume / 1000.0, NULL);
  g_free(uri);
  gst_element_set_state(soundPlayer, GST_STATE_PLAYING);
}

// ----------------------------------------------------------------------------
// Configuration (~/.config/pushly/config.ini)
// ----------------------------------------------------------------------------
static std::string ConfigPath() {
  std::string dir = std::string(g_get_user_config_dir()) + "/pushly";
  g_mkdir_with_parents(dir.c_str(), 0755);
  return dir + "/config.ini";
}

static void SaveConfig() {
  if (loadingConfig)
    return;
  GKeyFile *kf = g_key_file_new();
  g_key_file_set_string(kf, "Settings", "Key", targetKey.c_str());
  g_key_file_set_integer(kf, "Settings", "Delay", spamDelayMs.load());
  gchar *startAccel = gtk_accelerator_name(startKeyval.load(),
                                           (GdkModifierType)startMods.load());
  gchar *stopAccel = gtk_accelerator_name(stopKeyval.load(),
                                          (GdkModifierType)stopMods.load());
  g_key_file_set_string(kf, "Settings", "StartKey", startAccel);
  g_key_file_set_string(kf, "Settings", "StopKey", stopAccel);
  g_free(startAccel);
  g_free(stopAccel);
  g_key_file_set_integer(kf, "Settings", "Muted", soundMuted ? 1 : 0);
  g_key_file_set_integer(kf, "Settings", "Volume", soundVolume);
  g_key_file_save_to_file(kf, ConfigPath().c_str(), nullptr);
  g_key_file_free(kf);
}

static void LoadConfig() {
  GKeyFile *kf = g_key_file_new();
  if (g_key_file_load_from_file(kf, ConfigPath().c_str(), G_KEY_FILE_NONE,
                                nullptr)) {
    gchar *key = g_key_file_get_string(kf, "Settings", "Key", nullptr);
    if (key && *key) {
      targetKey = key;
    }
    g_free(key);
    GError *err = nullptr;
    int d = g_key_file_get_integer(kf, "Settings", "Delay", &err);
    if (!err && d > 0)
      spamDelayMs = d;
    g_clear_error(&err);
    gchar *accel = g_key_file_get_string(kf, "Settings", "StartKey", nullptr);
    if (accel) {
      guint kv;
      GdkModifierType mods;
      gtk_accelerator_parse(accel, &kv, &mods);
      if (kv) {
        startKeyval = kv;
        startMods = (guint)mods;
      }
      g_free(accel);
    }
    accel = g_key_file_get_string(kf, "Settings", "StopKey", nullptr);
    if (accel) {
      guint kv;
      GdkModifierType mods;
      gtk_accelerator_parse(accel, &kv, &mods);
      if (kv) {
        stopKeyval = kv;
        stopMods = (guint)mods;
      }
      g_free(accel);
    }
    soundMuted = g_key_file_get_integer(kf, "Settings", "Muted", nullptr) != 0;
    int v = g_key_file_get_integer(kf, "Settings", "Volume", &err);
    if (!err && v >= 0 && v <= 1000)
      soundVolume = v;
    g_clear_error(&err);
  }
  g_key_file_free(kf);
}

// ----------------------------------------------------------------------------
// Conversion keysym/keyval -> code evdev
// ----------------------------------------------------------------------------
static KeySym KeysymFromTarget(const std::string &s) {
  if (s.empty())
    return NoSymbol;
  KeySym ks = XStringToKeysym(s.c_str());
  if (ks == NoSymbol && s.size() == 1)
    ks = (KeySym)(unsigned char)s[0];
  return ks;
}

// Table de secours (QWERTY/positions communes) si aucun serveur X n'est
// joignable pour la conversion tenant compte de la disposition clavier.
static int FallbackEvdevCode(KeySym ks) {
  if (ks >= XK_F1 && ks <= XK_F10)
    return KEY_F1 + (int)(ks - XK_F1);
  if (ks == XK_F11)
    return KEY_F11;
  if (ks == XK_F12)
    return KEY_F12;
  if (ks >= XK_1 && ks <= XK_9)
    return KEY_1 + (int)(ks - XK_1);
  if (ks == XK_0)
    return KEY_0;
  if (ks >= XK_A && ks <= XK_Z)
    ks = ks - XK_A + XK_a;
  static const int letters[26] = {
      KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
      KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
      KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
  if (ks >= XK_a && ks <= XK_z)
    return letters[ks - XK_a];
  switch (ks) {
  case XK_space:
    return KEY_SPACE;
  case XK_Return:
    return KEY_ENTER;
  case XK_Tab:
    return KEY_TAB;
  case XK_Escape:
    return KEY_ESC;
  case XK_BackSpace:
    return KEY_BACKSPACE;
  case XK_Up:
    return KEY_UP;
  case XK_Down:
    return KEY_DOWN;
  case XK_Left:
    return KEY_LEFT;
  case XK_Right:
    return KEY_RIGHT;
  }
  return 0;
}

// Convertit un keysym en code evdev en respectant la disposition clavier
// (AZERTY...) via le serveur X / XWayland : keycode X = code evdev + 8.
static int EvdevCodeFromKeysym(KeySym ks) {
  if (ks == NoSymbol)
    return 0;
  static Display *mapDpy = XOpenDisplay(nullptr);
  if (mapDpy) {
    KeyCode kc = XKeysymToKeycode(mapDpy, ks);
    if (kc > 8)
      return (int)kc - 8;
  }
  return FallbackEvdevCode(ks);
}

// ----------------------------------------------------------------------------
// Backend uinput : clavier virtuel
// ----------------------------------------------------------------------------
static const char *kVirtualName = "Pushly Virtual Keyboard";

static int OpenUinput() {
  int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd < 0)
    return -1;
  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  ioctl(fd, UI_SET_EVBIT, EV_SYN);
  for (int code = 1; code <= 248; code++)
    ioctl(fd, UI_SET_KEYBIT, code);

  struct uinput_setup setup;
  memset(&setup, 0, sizeof(setup));
  setup.id.bustype = BUS_VIRTUAL;
  setup.id.vendor = 0x5075; // "Pu"
  setup.id.product = 0x736c; // "sl"
  snprintf(setup.name, sizeof(setup.name), "%s", kVirtualName);
  if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
    close(fd);
    return -1;
  }
  usleep(300000); // laisse le compositeur enregistrer le périphérique
  return fd;
}

static void UinputEmit(int fd, int type, int code, int value) {
  struct input_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = (unsigned short)type;
  ev.code = (unsigned short)code;
  ev.value = value;
  (void)!write(fd, &ev, sizeof(ev));
}

static void UinputTapKey(int fd, int code) {
  UinputEmit(fd, EV_KEY, code, 1);
  UinputEmit(fd, EV_SYN, SYN_REPORT, 0);
  UinputEmit(fd, EV_KEY, code, 0);
  UinputEmit(fd, EV_SYN, SYN_REPORT, 0);
}

// ----------------------------------------------------------------------------
// Thread de spam
// ----------------------------------------------------------------------------
static void SpamLoop() {
  Display *xDpy = nullptr;
  KeyCode xKc = 0;
  int evCode = 0;

  if (useUinput) {
    evCode = EvdevCodeFromKeysym(KeysymFromTarget(targetKey));
    if (evCode == 0 || uinputFd < 0) {
      isSpamming = false;
      return;
    }
  } else {
    xDpy = XOpenDisplay(nullptr);
    KeySym ks = KeysymFromTarget(targetKey);
    xKc = (xDpy && ks != NoSymbol) ? XKeysymToKeycode(xDpy, ks) : 0;
    if (!xKc) {
      if (xDpy)
        XCloseDisplay(xDpy);
      isSpamming = false;
      return;
    }
  }

  std::mt19937 rng{std::random_device{}()};
  int sinceReseed = 0;

  while (isSpamming && appRunning) {
    if (useUinput) {
      UinputTapKey(uinputFd, evCode);
    } else {
      XTestFakeKeyEvent(xDpy, xKc, True, CurrentTime);
      XTestFakeKeyEvent(xDpy, xKc, False, CurrentTime);
      XFlush(xDpy);
    }

    // Jitter gaussien : écart-type = 10 % du délai, re-seed périodique
    double mean = (double)spamDelayMs.load();
    std::normal_distribution<double> dist(mean, mean * 0.10);
    double ms = dist(rng);
    if (ms < 1.0)
      ms = 1.0;
    usleep((useconds_t)(ms * 1000.0));

    if (++sinceReseed >= 500) {
      rng.seed(std::random_device{}());
      sinceReseed = 0;
    }
  }
  if (xDpy)
    XCloseDisplay(xDpy);
}

// ----------------------------------------------------------------------------
// Démarrage / arrêt (thread principal GTK uniquement)
// ----------------------------------------------------------------------------
static void UpdateStatusLabel() {
  gtk_button_set_label(GTK_BUTTON(btnStatus),
                       isSpamming ? "STATUT: EN COURS (Arreter avec Raccourci)"
                                  : "STATUT: EN ATTENTE");
  GtkStyleContext *ctx = gtk_widget_get_style_context(btnStatus);
  if (isSpamming)
    gtk_style_context_add_class(ctx, "running");
  else
    gtk_style_context_remove_class(ctx, "running");
}

static void StartSpam() {
  if (isSpamming)
    return;
  if (KeysymFromTarget(targetKey) == NoSymbol)
    return;
  isSpamming = true;
  if (spamThread.joinable())
    spamThread.join();
  spamThread = std::thread(SpamLoop);
  UpdateStatusLabel();
  PlaySound("start.mp3");
}

static void StopSpam() {
  if (!isSpamming)
    return;
  isSpamming = false;
  if (spamThread.joinable())
    spamThread.join();
  UpdateStatusLabel();
  PlaySound("stop.mp3");
}

static gboolean OnHotkeyStart(gpointer) {
  StartSpam();
  return G_SOURCE_REMOVE;
}
static gboolean OnHotkeyStop(gpointer) {
  StopSpam();
  return G_SOURCE_REMOVE;
}

static gboolean SetBackendWarning(gpointer text) {
  if (lblBackend)
    gtk_label_set_markup(GTK_LABEL(lblBackend), (const char *)text);
  return G_SOURCE_REMOVE;
}

// ----------------------------------------------------------------------------
// Raccourcis globaux — backend evdev (Wayland)
// ----------------------------------------------------------------------------
struct EvdevDev {
  int fd;
};

static bool IsKeyboardFd(int fd) {
  unsigned long evbits = 0;
  if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), &evbits) < 0)
    return false;
  if (!(evbits & (1UL << EV_KEY)))
    return false;
  unsigned long keybits[(KEY_MAX + 1) / (8 * sizeof(long)) + 1] = {0};
  if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0)
    return false;
  auto has = [&](int code) {
    return (keybits[code / (8 * sizeof(long))] >>
            (code % (8 * sizeof(long)))) &
           1UL;
  };
  return has(KEY_A) && has(KEY_SPACE);
}

static std::vector<EvdevDev> ScanKeyboards() {
  std::vector<EvdevDev> devs;
  DIR *d = opendir("/dev/input");
  if (!d)
    return devs;
  struct dirent *ent;
  while ((ent = readdir(d))) {
    if (strncmp(ent->d_name, "event", 5) != 0)
      continue;
    std::string path = std::string("/dev/input/") + ent->d_name;
    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0)
      continue;
    char name[256] = {0};
    ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
    // Ignore notre propre clavier virtuel
    if (strstr(name, "Pushly") || !IsKeyboardFd(fd)) {
      close(fd);
      continue;
    }
    devs.push_back({fd});
  }
  closedir(d);
  return devs;
}

static guint HeldModsFromState(const bool *held) {
  guint m = 0;
  if (held[KEY_LEFTSHIFT] || held[KEY_RIGHTSHIFT])
    m |= GDK_SHIFT_MASK;
  if (held[KEY_LEFTCTRL] || held[KEY_RIGHTCTRL])
    m |= GDK_CONTROL_MASK;
  if (held[KEY_LEFTALT] || held[KEY_RIGHTALT])
    m |= GDK_MOD1_MASK;
  if (held[KEY_LEFTMETA] || held[KEY_RIGHTMETA])
    m |= GDK_SUPER_MASK;
  return m;
}

static const guint kGdkModMask =
    GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK;

static void EvdevHotkeyLoop() {
  std::vector<EvdevDev> devs = ScanKeyboards();
  if (devs.empty()) {
    g_idle_add(SetBackendWarning,
               (gpointer) "<span foreground='#e0a030' size='small'>⚠ "
                          "Raccourcis globaux indisponibles : ajoutez-vous au "
                          "groupe input\n(sudo usermod -aG input $USER puis "
                          "reconnectez-vous)</span>");
  }

  bool held[KEY_MAX + 1] = {false};
  int startCode = 0, stopCode = 0;
  guint startM = 0, stopM = 0;
  int rescanMs = 0;

  while (appRunning) {
    if (hotkeysChanged.exchange(false)) {
      startCode = EvdevCodeFromKeysym((KeySym)startKeyval.load());
      stopCode = EvdevCodeFromKeysym((KeySym)stopKeyval.load());
      startM = startMods.load() & kGdkModMask;
      stopM = stopMods.load() & kGdkModMask;
    }

    if (rescanMs >= 3000) { // hotplug clavier
      rescanMs = 0;
      size_t before = devs.size();
      for (auto &dv : devs)
        close(dv.fd);
      devs = ScanKeyboards();
      if (devs.size() != before)
        memset(held, 0, sizeof(held));
    }

    std::vector<struct pollfd> pfds;
    for (auto &dv : devs)
      pfds.push_back({dv.fd, POLLIN, 0});
    int timeout = 200;
    if (!pfds.empty())
      poll(pfds.data(), pfds.size(), timeout);
    else
      usleep(timeout * 1000);
    rescanMs += timeout;

    for (size_t i = 0; i < pfds.size(); i++) {
      if (!(pfds[i].revents & POLLIN))
        continue;
      struct input_event ev;
      while (read(devs[i].fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type != EV_KEY || ev.code > KEY_MAX)
          continue;
        if (ev.value == 1)
          held[ev.code] = true;
        else if (ev.value == 0)
          held[ev.code] = false;
        if (ev.value != 1)
          continue;
        guint mods = HeldModsFromState(held);
        bool isStart = (startCode && (int)ev.code == startCode &&
                        mods == startM);
        bool isStop =
            (stopCode && (int)ev.code == stopCode && mods == stopM);
        if (isStart && isStop)
          g_idle_add(isSpamming ? OnHotkeyStop : OnHotkeyStart, nullptr);
        else if (isStart)
          g_idle_add(OnHotkeyStart, nullptr);
        else if (isStop)
          g_idle_add(OnHotkeyStop, nullptr);
      }
    }
  }
  for (auto &dv : devs)
    close(dv.fd);
}

// ----------------------------------------------------------------------------
// Raccourcis globaux — backend X11 (XGrabKey)
// ----------------------------------------------------------------------------
static int IgnoreXError(Display *, XErrorEvent *) { return 0; }

static const unsigned int kXModMask =
    ShiftMask | ControlMask | Mod1Mask | Mod4Mask;

static void GrabCombo(Display *dpy, Window root, KeyCode kc,
                      unsigned int mods) {
  const unsigned int lockCombos[] = {0, LockMask, Mod2Mask,
                                     LockMask | Mod2Mask};
  for (unsigned int lock : lockCombos)
    XGrabKey(dpy, kc, mods | lock, root, False, GrabModeAsync, GrabModeAsync);
}

static void X11HotkeyLoop() {
  Display *dpy = XOpenDisplay(nullptr);
  if (!dpy)
    return;
  XSetErrorHandler(IgnoreXError);
  Window root = DefaultRootWindow(dpy);

  KeyCode startKc = 0, stopKc = 0;
  unsigned int startM = 0, stopM = 0;

  while (appRunning) {
    if (hotkeysChanged.exchange(false)) {
      XUngrabKey(dpy, AnyKey, AnyModifier, root);
      startKc = XKeysymToKeycode(dpy, (KeySym)startKeyval.load());
      stopKc = XKeysymToKeycode(dpy, (KeySym)stopKeyval.load());
      startM = startMods.load() & kXModMask;
      stopM = stopMods.load() & kXModMask;
      if (startKc)
        GrabCombo(dpy, root, startKc, startM);
      if (stopKc && (stopKc != startKc || stopM != startM))
        GrabCombo(dpy, root, stopKc, stopM);
      XSync(dpy, False);
    }
    while (XPending(dpy)) {
      XEvent ev;
      XNextEvent(dpy, &ev);
      if (ev.type == KeyPress) {
        unsigned int state = ev.xkey.state & kXModMask;
        KeyCode kc = (KeyCode)ev.xkey.keycode;
        if (kc == startKc && state == startM && kc == stopKc &&
            state == stopM) {
          g_idle_add(isSpamming ? OnHotkeyStop : OnHotkeyStart, nullptr);
        } else if (kc == startKc && state == startM) {
          g_idle_add(OnHotkeyStart, nullptr);
        } else if (kc == stopKc && state == stopM) {
          g_idle_add(OnHotkeyStop, nullptr);
        }
      }
    }
    usleep(30000);
  }
  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  XCloseDisplay(dpy);
}

// ----------------------------------------------------------------------------
// Callbacks GTK
// ----------------------------------------------------------------------------
static void SetHotkeyButtonLabel(GtkWidget *btn, guint keyval, guint mods) {
  gchar *label = gtk_accelerator_get_label(keyval, (GdkModifierType)mods);
  gtk_button_set_label(GTK_BUTTON(btn), (label && *label) ? label : "?");
  g_free(label);
}

static void OnKeyChanged(GtkEditable *, gpointer) {
  const gchar *txt = gtk_entry_get_text(GTK_ENTRY(entryKey));
  if (txt && *txt)
    targetKey = txt;
  SaveConfig();
}

static void OnDelayChanged(GtkSpinButton *spin, gpointer) {
  int v = gtk_spin_button_get_value_as_int(spin);
  if (v > 0)
    spamDelayMs = v;
  SaveConfig();
}

static void OnHotkeyButtonClicked(GtkButton *btn, gpointer which) {
  capturingHotkey = GPOINTER_TO_INT(which);
  gtk_button_set_label(btn, "Appuyez sur une touche...");
}

static gboolean OnWindowKeyPress(GtkWidget *, GdkEventKey *ev, gpointer) {
  if (capturingHotkey == 0)
    return FALSE;

  switch (ev->keyval) { // modificateurs seuls : on attend la touche finale
  case GDK_KEY_Shift_L:
  case GDK_KEY_Shift_R:
  case GDK_KEY_Control_L:
  case GDK_KEY_Control_R:
  case GDK_KEY_Alt_L:
  case GDK_KEY_Alt_R:
  case GDK_KEY_Super_L:
  case GDK_KEY_Super_R:
  case GDK_KEY_Meta_L:
  case GDK_KEY_Meta_R:
    return TRUE;
  }

  int which = capturingHotkey;
  capturingHotkey = 0;

  if (ev->keyval != GDK_KEY_Escape) {
    guint mods = ev->state & kGdkModMask;
    if (which == 1) {
      startKeyval = ev->keyval;
      startMods = mods;
    } else {
      stopKeyval = ev->keyval;
      stopMods = mods;
    }
    hotkeysChanged = true;
    SaveConfig();
  }
  SetHotkeyButtonLabel(btnHotkeyStart, startKeyval.load(), startMods.load());
  SetHotkeyButtonLabel(btnHotkeyStop, stopKeyval.load(), stopMods.load());
  return TRUE;
}

static void OnStatusClicked(GtkButton *, gpointer) {
  if (isSpamming)
    StopSpam();
  else
    StartSpam();
}

static void OnMuteToggled(GtkToggleButton *b, gpointer) {
  soundMuted = gtk_toggle_button_get_active(b);
  SaveConfig();
}

static void OnVolumeChanged(GtkRange *range, gpointer) {
  soundVolume = (int)(gtk_range_get_value(range) * 10.0); // 0-100 -> 0-1000
  SaveConfig();
}

static void OnDestroy(GtkWidget *, gpointer) {
  appRunning = false;
  isSpamming = false;
  if (spamThread.joinable())
    spamThread.join();
  if (hotkeyThread.joinable())
    hotkeyThread.join();
  SaveConfig();
  if (uinputFd >= 0) {
    ioctl(uinputFd, UI_DEV_DESTROY);
    close(uinputFd);
    uinputFd = -1;
  }
  if (soundPlayer) {
    gst_element_set_state(soundPlayer, GST_STATE_NULL);
    gst_object_unref(soundPlayer);
    soundPlayer = nullptr;
  }
  gtk_main_quit();
}

// ----------------------------------------------------------------------------
// Interface
// ----------------------------------------------------------------------------
static void ApplyDarkTheme() {
  g_object_set(gtk_settings_get_default(),
               "gtk-application-prefer-dark-theme", TRUE, NULL);
  const char *css =
      "window { background-color: #1e1e1e; }"
      "label { color: #e6e6e6; }"
      "entry, spinbutton { background: #2a2a2e; color: #e6e6e6;"
      "  border: 1px solid #444; }"
      "button { background: #2a2a2e; color: #e6e6e6;"
      "  border: 1px solid #555; border-radius: 6px; }"
      "button:hover { background: #3a3a40; }"
      "button.status { background: #6E3CD2; color: #ffffff;"
      "  font-weight: bold; }"
      "button.status.running { background: #2fbf5f; color: #101010; }"
      "checkbutton { color: #e6e6e6; }"
      "scale trough { background: #32323a; }"
      "scale highlight { background: #6E3CD2; }";
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(provider, css, -1, nullptr);
  gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

static GtkWidget *MakeLabel(const char *text) {
  GtkWidget *l = gtk_label_new(text);
  gtk_widget_set_halign(l, GTK_ALIGN_START);
  return l;
}

int main(int argc, char **argv) {
  XInitThreads();
  g_set_prgname("pushly");
  gtk_init(&argc, &argv);
  gtk_window_set_default_icon_name("pushly");
  gst_init(&argc, &argv);

  // Sélection du backend : uinput sous Wayland, XTest sous X11
  const char *forced = getenv("PUSHLY_BACKEND");
  const char *waylandDisplay = getenv("WAYLAND_DISPLAY");
  if (forced && strcmp(forced, "x11") == 0)
    useUinput = false;
  else if (forced && strcmp(forced, "uinput") == 0)
    useUinput = true;
  else
    useUinput = (waylandDisplay && *waylandDisplay);

  std::string backendMsg;
  if (useUinput) {
    uinputFd = OpenUinput();
    if (uinputFd < 0) {
      useUinput = false;
      backendMsg = "<span foreground='#e0a030' size='small'>⚠ /dev/uinput "
                   "inaccessible : repli sur X11 (les applis natives Wayland "
                   "ne recevront pas les frappes).\nAjoutez-vous au groupe "
                   "input : sudo usermod -aG input $USER</span>";
    } else {
      backendMsg = "<span foreground='#7a7a85' size='small'>Backend : uinput "
                   "(Wayland)</span>";
    }
  } else {
    backendMsg =
        "<span foreground='#7a7a85' size='small'>Backend : X11 (XTest)</span>";
  }

  resourceDir = FindResourceDir();
  soundPlayer = gst_element_factory_make("playbin", "pushly-sound");
  LoadConfig();
  ApplyDarkTheme();

  mainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(mainWindow),
                       "Pushly v" PUSHLY_VERSION " — by BlaMacfly");
  gtk_window_set_default_size(GTK_WINDOW(mainWindow), 420, 380);
  gtk_window_set_resizable(GTK_WINDOW(mainWindow), FALSE);
  gtk_container_set_border_width(GTK_CONTAINER(mainWindow), 16);

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
  gtk_container_add(GTK_CONTAINER(mainWindow), grid);

  int row = 0;

  std::string logoPath = resourceDir + "/PushlyLogo.png";
  if (access(logoPath.c_str(), R_OK) == 0) {
    GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(logoPath.c_str(), 96, 96,
                                                      TRUE, nullptr);
    if (pb) {
      GtkWidget *img = gtk_image_new_from_pixbuf(pb);
      gtk_widget_set_halign(img, GTK_ALIGN_CENTER);
      gtk_grid_attach(GTK_GRID(grid), img, 0, row++, 2, 1);
      gtk_window_set_icon(GTK_WINDOW(mainWindow), pb);
      g_object_unref(pb);
    }
  }

  gtk_grid_attach(GTK_GRID(grid), MakeLabel("Touche a spammer :"), 0, row, 1,
                  1);
  entryKey = gtk_entry_new();
  gtk_entry_set_text(GTK_ENTRY(entryKey), targetKey.c_str());
  gtk_entry_set_width_chars(GTK_ENTRY(entryKey), 10);
  gtk_widget_set_hexpand(entryKey, TRUE);
  gtk_grid_attach(GTK_GRID(grid), entryKey, 1, row++, 1, 1);

  gtk_grid_attach(GTK_GRID(grid), MakeLabel("Delai/Intervalle (ms) :"), 0, row,
                  1, 1);
  spinDelay = gtk_spin_button_new_with_range(1, 60000, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinDelay), spamDelayMs.load());
  gtk_grid_attach(GTK_GRID(grid), spinDelay, 1, row++, 1, 1);

  gtk_grid_attach(GTK_GRID(grid), MakeLabel("Touche Demarrer :"), 0, row, 1, 1);
  btnHotkeyStart = gtk_button_new();
  SetHotkeyButtonLabel(btnHotkeyStart, startKeyval.load(), startMods.load());
  gtk_grid_attach(GTK_GRID(grid), btnHotkeyStart, 1, row++, 1, 1);

  gtk_grid_attach(GTK_GRID(grid), MakeLabel("Touche Arreter :"), 0, row, 1, 1);
  btnHotkeyStop = gtk_button_new();
  SetHotkeyButtonLabel(btnHotkeyStop, stopKeyval.load(), stopMods.load());
  gtk_grid_attach(GTK_GRID(grid), btnHotkeyStop, 1, row++, 1, 1);

  btnStatus = gtk_button_new_with_label("STATUT: EN ATTENTE");
  gtk_style_context_add_class(gtk_widget_get_style_context(btnStatus),
                              "status");
  gtk_widget_set_size_request(btnStatus, -1, 42);
  gtk_grid_attach(GTK_GRID(grid), btnStatus, 0, row++, 2, 1);

  chkMute = gtk_check_button_new_with_label("Mute (Couper le son)");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chkMute), soundMuted);
  gtk_grid_attach(GTK_GRID(grid), chkMute, 0, row++, 2, 1);

  gtk_grid_attach(GTK_GRID(grid), MakeLabel("Volume :"), 0, row, 1, 1);
  scaleVolume = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_range_set_value(GTK_RANGE(scaleVolume), soundVolume / 10.0);
  gtk_scale_set_value_pos(GTK_SCALE(scaleVolume), GTK_POS_RIGHT);
  gtk_widget_set_hexpand(scaleVolume, TRUE);
  gtk_grid_attach(GTK_GRID(grid), scaleVolume, 1, row++, 1, 1);

  lblBackend = gtk_label_new(nullptr);
  gtk_label_set_markup(GTK_LABEL(lblBackend), backendMsg.c_str());
  gtk_label_set_line_wrap(GTK_LABEL(lblBackend), TRUE);
  gtk_label_set_justify(GTK_LABEL(lblBackend), GTK_JUSTIFY_CENTER);
  gtk_widget_set_halign(lblBackend, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), lblBackend, 0, row++, 2, 1);

  g_signal_connect(entryKey, "changed", G_CALLBACK(OnKeyChanged), nullptr);
  g_signal_connect(spinDelay, "value-changed", G_CALLBACK(OnDelayChanged),
                   nullptr);
  g_signal_connect(btnHotkeyStart, "clicked",
                   G_CALLBACK(OnHotkeyButtonClicked), GINT_TO_POINTER(1));
  g_signal_connect(btnHotkeyStop, "clicked", G_CALLBACK(OnHotkeyButtonClicked),
                   GINT_TO_POINTER(2));
  g_signal_connect(mainWindow, "key-press-event",
                   G_CALLBACK(OnWindowKeyPress), nullptr);
  g_signal_connect(btnStatus, "clicked", G_CALLBACK(OnStatusClicked), nullptr);
  g_signal_connect(chkMute, "toggled", G_CALLBACK(OnMuteToggled), nullptr);
  g_signal_connect(scaleVolume, "value-changed", G_CALLBACK(OnVolumeChanged),
                   nullptr);
  g_signal_connect(mainWindow, "destroy", G_CALLBACK(OnDestroy), nullptr);

  hotkeyThread = std::thread(useUinput ? EvdevHotkeyLoop : X11HotkeyLoop);

  gtk_widget_show_all(mainWindow);
  gtk_main();
  return 0;
}
