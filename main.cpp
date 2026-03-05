// clang-format off
#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include <dwmapi.h>
#include <iostream>
// clang-format off
#pragma comment(lib, "dwmapi.lib")
// clang-format on
#include <random>
#include <string>
#include <thread>
#include <vector>
// clang-format on

#pragma comment(lib, "comctl32.lib")

#include "resource.h"

// Globals
HWND hMainWindow = NULL;
HWND hEditKey = NULL;
HWND hEditDelay = NULL;
HWND hHotKeyStart = NULL;
HWND hHotKeyStop = NULL;
HWND hBtnApply = NULL;
HWND hBtnStatus = NULL;

std::atomic<bool> isSpamming(false);
std::atomic<bool> appRunning(true);
std::thread spamThread;

char targetKey = '1';
int spamDelayMs = 50;

WORD startVK = VK_F9;
WORD startMods = 0;
WORD stopVK = VK_F10;
WORD stopMods = 0;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

void LoadConfig() {
  char exePath[MAX_PATH];
  GetModuleFileNameA(NULL, exePath, MAX_PATH);
  std::string path(exePath);
  path = path.substr(0, path.find_last_of("\\/")) + "\\config.ini";

  char keyBuf[10];
  GetPrivateProfileStringA("Settings", "Key", "1", keyBuf, 10, path.c_str());
  if (strlen(keyBuf) > 0)
    targetKey = keyBuf[0];

  spamDelayMs = GetPrivateProfileIntA("Settings", "Delay", 50, path.c_str());

  startVK = GetPrivateProfileIntA("Settings", "StartVK", VK_F9, path.c_str());
  startMods = GetPrivateProfileIntA("Settings", "StartMods", 0, path.c_str());
  stopVK = GetPrivateProfileIntA("Settings", "StopVK", VK_F10, path.c_str());
  stopMods = GetPrivateProfileIntA("Settings", "StopMods", 0, path.c_str());
}

void SaveConfig() {
  char exePath[MAX_PATH];
  GetModuleFileNameA(NULL, exePath, MAX_PATH);
  std::string path(exePath);
  path = path.substr(0, path.find_last_of("\\/")) + "\\config.ini";

  char delayBuf[20];
  sprintf_s(delayBuf, "%d", spamDelayMs);
  char keyBuf[2] = {targetKey, '\0'};

  WritePrivateProfileStringA("Settings", "Key", keyBuf, path.c_str());
  WritePrivateProfileStringA("Settings", "Delay", delayBuf, path.c_str());

  char buf[20];
  sprintf_s(buf, "%u", startVK);
  WritePrivateProfileStringA("Settings", "StartVK", buf, path.c_str());
  sprintf_s(buf, "%u", startMods);
  WritePrivateProfileStringA("Settings", "StartMods", buf, path.c_str());
  sprintf_s(buf, "%u", stopVK);
  WritePrivateProfileStringA("Settings", "StopVK", buf, path.c_str());
  sprintf_s(buf, "%u", stopMods);
  WritePrivateProfileStringA("Settings", "StopMods", buf, path.c_str());
}

void SpammerWorker() {
  std::random_device rd;
  std::mt19937 gen(rd());

  while (appRunning) {
    if (isSpamming) {
      INPUT inputs[2] = {};

      inputs[0].type = INPUT_KEYBOARD;
      inputs[0].ki.wVk = VkKeyScanExA(targetKey, GetKeyboardLayout(0)) & 0xFF;

      inputs[1].type = INPUT_KEYBOARD;
      inputs[1].ki.wVk = inputs[0].ki.wVk;
      inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

      if (inputs[0].ki.wVk != 0) {
        SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
      }

      // Calculate Jitter (Humanized execution)
      // +/- 20% of the base delay to evade anti-cheat
      int jitterRange = spamDelayMs * 0.20;
      if (jitterRange < 5)
        jitterRange = 5; // Minimum jitter of 5ms

      std::uniform_int_distribution<> distrib(-jitterRange, jitterRange);
      int finalDelay = spamDelayMs + distrib(gen);
      if (finalDelay < 1)
        finalDelay = 1; // Prevent negative/zero delays

      Sleep(finalDelay);
    } else {
      Sleep(50);
    }
  }
}

void UpdateStatusUI() {
  if (hBtnStatus) {
    if (isSpamming) {
      SetWindowTextA(hBtnStatus, "STATUT: EN COURS (Arreter avec Raccourci)");
    } else {
      SetWindowTextA(hBtnStatus,
                     "STATUT: EN ATTENTE (Demarrer avec Raccourci)");
    }
  }
}

UINT GetWin32Modifiers(WORD hkModifiers) {
  UINT mods = 0;
  if (hkModifiers & HOTKEYF_ALT)
    mods |= MOD_ALT;
  if (hkModifiers & HOTKEYF_CONTROL)
    mods |= MOD_CONTROL;
  if (hkModifiers & HOTKEYF_SHIFT)
    mods |= MOD_SHIFT;
  return mods;
}

void ApplySettings() {
  // Get Target Key
  char buf[10];
  GetWindowText(hEditKey, buf, 10);
  if (strlen(buf) > 0)
    targetKey = buf[0];

  // Get Delay
  char delayBuf[20];
  GetWindowText(hEditDelay, delayBuf, 20);
  int d = atoi(delayBuf);
  if (d > 0)
    spamDelayMs = d;

  // Unregister existing hotkeys
  UnregisterHotKey(hMainWindow, 1);
  UnregisterHotKey(hMainWindow, 2);

  // Get Hotkeys
  LRESULT startHk = SendMessage(hHotKeyStart, HKM_GETHOTKEY, 0, 0);
  startVK = LOBYTE(LOWORD(startHk));
  startMods = HIBYTE(LOWORD(startHk));

  LRESULT stopHk = SendMessage(hHotKeyStop, HKM_GETHOTKEY, 0, 0);
  stopVK = LOBYTE(LOWORD(stopHk));
  stopMods = HIBYTE(LOWORD(stopHk));

  // Register new hotkeys
  if (startVK)
    RegisterHotKey(hMainWindow, 1, GetWin32Modifiers(startMods), startVK);
  if (stopVK)
    RegisterHotKey(hMainWindow, 2, GetWin32Modifiers(stopMods), stopVK);

  SaveConfig();

  MessageBox(hMainWindow, "Parametres et Raccourcis appliques avec succes !",
             "Succes", MB_OK | MB_ICONINFORMATION);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine, int nCmdShow) {
  LoadConfig();

  INITCOMMONCONTROLSEX icex;
  icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
  icex.dwICC = ICC_HOTKEY_CLASS;
  InitCommonControlsEx(&icex);

  const char CLASS_NAME[] = "PushlyAppClass";

  WNDCLASS wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;
  wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
  wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 30)); // Dark Mode BG

  RegisterClass(&wc);

  HWND hwnd = CreateWindowEx(
      0, CLASS_NAME, "Pushly - Key Spammer",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
      CW_USEDEFAULT, 350, 340, NULL, NULL, hInstance, NULL);

  if (hwnd == NULL)
    return 0;
  hMainWindow = hwnd;

  // Modern Dark Title Bar (Windows 10 1809+)
  BOOL useDarkMode = TRUE;
  DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode));
  // 20 is DWMWA_USE_IMMERSIVE_DARK_MODE in older SDKs, fallback to 19 if needed
  // but 20 is safe

  // Setup initial Hotkeys
  RegisterHotKey(hwnd, 1, GetWin32Modifiers(startMods), startVK);
  RegisterHotKey(hwnd, 2, GetWin32Modifiers(stopMods), stopVK);

  spamThread = std::thread(SpammerWorker);

  ShowWindow(hwnd, nCmdShow);

  MSG msg = {};
  while (GetMessage(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  appRunning = false;
  if (spamThread.joinable())
    spamThread.join();
  UnregisterHotKey(hwnd, 1);
  UnregisterHotKey(hwnd, 2);

  return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
  case WM_CREATE: {
    int y = 20;
    CreateWindow("STATIC", "Touche a spammer :", WS_VISIBLE | WS_CHILD, 20, y,
                 150, 25, hwnd, NULL, NULL, NULL);
    char keyStr[2] = {targetKey, '\0'};
    hEditKey = CreateWindow("EDIT", keyStr,
                            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_CENTER, 180,
                            y, 50, 25, hwnd, NULL, NULL, NULL);

    y += 45;
    CreateWindow("STATIC", "Delai/Intervalle (ms) :", WS_VISIBLE | WS_CHILD, 20,
                 y, 150, 25, hwnd, NULL, NULL, NULL);
    char delayStr[20];
    sprintf_s(delayStr, "%d", spamDelayMs);
    hEditDelay =
        CreateWindow("EDIT", delayStr,
                     WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER | ES_CENTER,
                     180, y, 50, 25, hwnd, NULL, NULL, NULL);

    y += 45;
    CreateWindow("STATIC", "Touche Demarrer :", WS_VISIBLE | WS_CHILD, 20, y,
                 150, 25, hwnd, NULL, NULL, NULL);
    hHotKeyStart =
        CreateWindow(HOTKEY_CLASS, "", WS_VISIBLE | WS_CHILD | WS_BORDER, 180,
                     y, 130, 25, hwnd, NULL, NULL, NULL);
    SendMessage(hHotKeyStart, HKM_SETHOTKEY, MAKEWORD(startVK, startMods), 0);

    y += 45;
    CreateWindow("STATIC", "Touche Arreter :", WS_VISIBLE | WS_CHILD, 20, y,
                 150, 25, hwnd, NULL, NULL, NULL);
    hHotKeyStop =
        CreateWindow(HOTKEY_CLASS, "", WS_VISIBLE | WS_CHILD | WS_BORDER, 180,
                     y, 130, 25, hwnd, NULL, NULL, NULL);
    SendMessage(hHotKeyStop, HKM_SETHOTKEY, MAKEWORD(stopVK, stopMods), 0);

    y += 45;
    hBtnApply = CreateWindow("BUTTON", "Appliquer les Parametres",
                             WS_VISIBLE | WS_CHILD | BS_FLAT, 20, y, 290, 35,
                             hwnd, (HMENU)100, NULL, NULL);

    y += 45;
    hBtnStatus = CreateWindow("BUTTON", "STATUT: EN ATTENTE",
                              WS_VISIBLE | WS_CHILD | BS_FLAT, 20, y, 290, 35,
                              hwnd, NULL, NULL, NULL);
    EnableWindow(hBtnStatus, FALSE);

    // Font - Using Segoe UI bold for a modern touch
    HFONT hFont =
        CreateFont(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                   DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
    EnumChildWindows(
        hwnd,
        [](HWND child, LPARAM font) -> BOOL {
          SendMessage(child, WM_SETFONT, font, TRUE);
          return TRUE;
        },
        (LPARAM)hFont);

    return 0;
  }

  case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    SetTextColor(hdc, RGB(255, 255, 255)); // White text
    SetBkColor(hdc, RGB(50, 50, 50));      // Dark grey background
    static HBRUSH hbrBkgnd = CreateSolidBrush(RGB(50, 50, 50));
    return (INT_PTR)hbrBkgnd;
  }

  case WM_CTLCOLORBTN: {
    return (LRESULT)CreateSolidBrush(RGB(30, 30, 30));
  }

  case WM_CTLCOLORSTATIC: {
    HDC hdcStatic = (HDC)wParam;
    SetTextColor(hdcStatic, RGB(220, 220, 220)); // Light grey text
    SetBkMode(hdcStatic, TRANSPARENT);
    static HBRUSH hbrBkgnd = CreateSolidBrush(RGB(30, 30, 30));
    return (LRESULT)hbrBkgnd;
  }

  case WM_COMMAND: {
    if (LOWORD(wParam) == 100) { // Apply button clicked
      ApplySettings();
    }
    return 0;
  }

  case WM_HOTKEY: {
    if (wParam == 1 && !isSpamming) { // Start
      // Refresh interval from UI right before starting
      char delayBuf[20];
      GetWindowText(hEditDelay, delayBuf, 20);
      int d = atoi(delayBuf);
      if (d > 0)
        spamDelayMs = d;

      char buf[10];
      GetWindowText(hEditKey, buf, 10);
      if (strlen(buf) > 0)
        targetKey = buf[0];

      isSpamming = true;
      UpdateStatusUI();
      // Start Beep (High pitched, fast)
      std::thread([]() { Beep(800, 150); }).detach();
    } else if (wParam == 2 && isSpamming) { // Stop
      isSpamming = false;
      UpdateStatusUI();
      // Stop Beep (Low pitched, slower)
      std::thread([]() { Beep(400, 200); }).detach();
    }
    return 0;
  }

  case WM_DESTROY: {
    PostQuitMessage(0);
    return 0;
  }

    // WM_CTLCOLORSTATIC moved above
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
