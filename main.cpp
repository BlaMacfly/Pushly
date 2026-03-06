// clang-format off
#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include <dwmapi.h>
#include <iostream>
// clang-format off
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")
// clang-format on
#include <cmath>
#include <mmsystem.h>
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
HWND hChkMute = NULL;
HWND hSliderVolume = NULL;

// Hover tracking
HWND hHoveredButton = NULL;
bool isDraggingVolume = false;

std::atomic<bool> isSpamming(false);
std::atomic<bool> appRunning(true);
std::thread spamThread;

char targetKey = '1';
int spamDelayMs = 50;

WORD startVK = VK_F9;
WORD startMods = 0;
WORD stopVK = VK_F10;
WORD stopMods = 0;

bool soundMuted = false;
int soundVolume = 300; // MCI volume 0-1000 (default 30%)

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void SaveConfig();
void LoadConfig();

// Custom volume bar subclass
int VolumeFromMouse(HWND hwnd, int mouseX) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  // Reserve 45px on the right for the percentage text so the track doesn't
  // overlap it
  int trackW = (rc.right - rc.left) - 45;
  if (trackW < 10)
    trackW = 10;
  int pos = mouseX;
  if (pos < 0)
    pos = 0;
  if (pos > trackW)
    pos = trackW;
  return (pos * 100) / trackW;
}

LRESULT CALLBACK VolumeBarProc(HWND hwnd, UINT msg, WPARAM wParam,
                               LPARAM lParam, UINT_PTR, DWORD_PTR) {
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    // Fill background
    HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(hdc, &rc, bgBrush);
    DeleteObject(bgBrush);

    int trackH = 6;
    int trackY = (rc.bottom - rc.top) / 2 - trackH / 2;
    // Reserve 45px on the right for the text
    int trackW = (rc.right - rc.left) - 45;
    if (trackW < 10)
      trackW = 10;
    int vol = soundVolume / 10; // 0-100
    int fillW = (trackW * vol) / 100;

    // Draw track background (dark rounded)
    HBRUSH trackBrush = CreateSolidBrush(RGB(50, 50, 55));
    HPEN trackPen = CreatePen(PS_SOLID, 1, RGB(60, 60, 65));
    SelectObject(hdc, trackBrush);
    SelectObject(hdc, trackPen);
    RoundRect(hdc, 0, trackY, trackW, trackY + trackH, 6, 6);
    DeleteObject(trackBrush);
    DeleteObject(trackPen);

    // Draw filled portion (purple gradient)
    if (fillW > 2) {
      HBRUSH fillBrush = CreateSolidBrush(RGB(110, 60, 210));
      HPEN fillPen = CreatePen(PS_SOLID, 1, RGB(130, 80, 240));
      SelectObject(hdc, fillBrush);
      SelectObject(hdc, fillPen);
      RoundRect(hdc, 0, trackY, fillW, trackY + trackH, 6, 6);
      DeleteObject(fillBrush);
      DeleteObject(fillPen);
    }

    // Draw thumb (glowing circle)
    int thumbR = 8;
    int thumbX = fillW;
    int thumbY = (rc.bottom - rc.top) / 2;

    // Outer glow
    HBRUSH glowBrush = CreateSolidBrush(RGB(130, 80, 240));
    HPEN glowPen = CreatePen(PS_SOLID, 2, RGB(160, 120, 255));
    SelectObject(hdc, glowBrush);
    SelectObject(hdc, glowPen);
    Ellipse(hdc, thumbX - thumbR, thumbY - thumbR, thumbX + thumbR,
            thumbY + thumbR);
    DeleteObject(glowBrush);
    DeleteObject(glowPen);

    // Inner bright core
    HBRUSH coreBrush = CreateSolidBrush(RGB(200, 180, 255));
    HPEN corePen = CreatePen(PS_NULL, 0, 0);
    SelectObject(hdc, coreBrush);
    SelectObject(hdc, corePen);
    Ellipse(hdc, thumbX - 4, thumbY - 4, thumbX + 4, thumbY + 4);
    DeleteObject(coreBrush);
    DeleteObject(corePen);

    // Draw percentage text
    char volText[8];
    sprintf_s(volText, "%d%%", vol);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(200, 200, 220));
    HFONT hFont =
        CreateFont(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                   DEFAULT_PITCH | FF_SWISS, "Consolas");
    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);

    // Position text clearly on the right
    RECT textRc = {trackW + 10, 0, rc.right, rc.bottom};
    DrawTextA(hdc, volText, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    DeleteObject(hFont);

    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_LBUTTONDOWN: {
    SetCapture(hwnd);
    isDraggingVolume = true;
    int vol = VolumeFromMouse(hwnd, (short)LOWORD(lParam));
    soundVolume = vol * 10;
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  }
  case WM_MOUSEMOVE: {
    if (isDraggingVolume) {
      int vol = VolumeFromMouse(hwnd, (short)LOWORD(lParam));
      soundVolume = vol * 10;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    return 0;
  }
  case WM_LBUTTONUP: {
    if (isDraggingVolume) {
      isDraggingVolume = false;
      ReleaseCapture();
      SaveConfig();
    }
    return 0;
  }
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

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

  soundMuted = GetPrivateProfileIntA("Settings", "Muted", 0, path.c_str()) != 0;
  soundVolume = GetPrivateProfileIntA("Settings", "Volume", 300, path.c_str());
  if (soundVolume < 0)
    soundVolume = 0;
  if (soundVolume > 1000)
    soundVolume = 1000;
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

  WritePrivateProfileStringA("Settings", "Muted", soundMuted ? "1" : "0",
                             path.c_str());
  sprintf_s(buf, "%d", soundVolume);
  WritePrivateProfileStringA("Settings", "Volume", buf, path.c_str());
}

void PlayMp3(const char *filename) {
  if (soundMuted)
    return;

  char exePath[MAX_PATH];
  GetModuleFileNameA(NULL, exePath, MAX_PATH);
  std::string dir(exePath);
  dir = dir.substr(0, dir.find_last_of("\\/")) + "\\" + filename;

  // Stop any previously playing sound, then play the new one
  std::string closeCmd = "close pushlySound";
  mciSendStringA(closeCmd.c_str(), NULL, 0, NULL);

  std::string openCmd = "open \"" + dir + "\" type mpegvideo alias pushlySound";
  mciSendStringA(openCmd.c_str(), NULL, 0, NULL);

  // Set volume from user setting (0-1000)
  char volCmd[64];
  sprintf_s(volCmd, "setaudio pushlySound volume to %d", soundVolume);
  mciSendStringA(volCmd, NULL, 0, NULL);

  mciSendStringA("play pushlySound", NULL, 0, NULL);
}

void SpammerWorker() {
  // True hardware entropy source (Windows CryptGenRandom under the hood)
  std::random_device rd;
  std::mt19937 gen(rd());
  int iterationCount = 0;

  // Re-seed threshold: itself randomized to avoid predictable re-seed patterns
  std::uniform_int_distribution<> reseedDist(50, 150);
  int reseedAt = reseedDist(gen);

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

      // --- Gaussian Humanized Jitter ---
      // Standard deviation = 10% of delay (so ~95% of values fall within
      // +/-20%) But outliers CAN happen naturally, just like a real human
      double stddev = spamDelayMs * 0.10;
      if (stddev < 3.0)
        stddev = 3.0; // Minimum stddev of 3ms

      std::normal_distribution<double> gaussDist((double)spamDelayMs, stddev);
      int finalDelay = (int)gaussDist(gen);

      // Clamp to sane minimum (never negative or zero)
      if (finalDelay < 1)
        finalDelay = 1;

      Sleep(finalDelay);

      // --- Anti-Cycle: Re-seed from hardware entropy periodically ---
      // This guarantees no repeating PRNG sequence can ever be detected
      iterationCount++;
      if (iterationCount >= reseedAt) {
        gen.seed(rd());             // Fresh hardware entropy
        reseedAt = reseedDist(gen); // Randomize next re-seed point
        iterationCount = 0;
      }

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
    InvalidateRect(hBtnStatus, NULL, TRUE); // Force redraw with new color
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
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine, int nCmdShow) {
  LoadConfig();

  INITCOMMONCONTROLSEX icex;
  icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
  icex.dwICC = ICC_HOTKEY_CLASS | ICC_BAR_CLASSES;
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
      CW_USEDEFAULT, 370, 435, NULL, NULL, hInstance, NULL);

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
    hBtnStatus = CreateWindow("BUTTON", "STATUT: EN ATTENTE",
                              WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 20, y, 310,
                              38, hwnd, NULL, NULL, NULL);
    EnableWindow(hBtnStatus, FALSE);

    // --- Sound Controls ---
    y += 50;
    hChkMute = CreateWindow("BUTTON", "  Mute (Couper le son)",
                            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 20, y, 200,
                            25, hwnd, (HMENU)101, NULL, NULL);
    if (soundMuted)
      SendMessage(hChkMute, BM_SETCHECK, BST_CHECKED, 0);

    y += 35;
    CreateWindow("STATIC", "Volume :", WS_VISIBLE | WS_CHILD, 20, y, 60, 25,
                 hwnd, NULL, NULL, NULL);
    hSliderVolume =
        CreateWindow("STATIC", "", WS_VISIBLE | WS_CHILD | SS_NOTIFY, 85, y,
                     245, 25, hwnd, (HMENU)102, NULL, NULL);
    SetWindowSubclass(hSliderVolume, VolumeBarProc, 0, 0);

    // Font - Using Consolas for a futuristic monospaced UI look
    HFONT hFont =
        CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                   DEFAULT_PITCH | FF_SWISS, "Consolas");
    SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
    EnumChildWindows(
        hwnd,
        [](HWND child, LPARAM font) -> BOOL {
          SendMessage(child, WM_SETFONT, font, TRUE);
          return TRUE;
        },
        (LPARAM)hFont);

    // Start auto-save timer (fires every 1 second)
    SetTimer(hwnd, 1, 1000, NULL);

    return 0;
  }

  case WM_CTLCOLOREDIT: {
    HDC hdc = (HDC)wParam;
    SetTextColor(hdc, RGB(230, 230, 230));
    SetBkColor(hdc, RGB(45, 45, 50));
    static HBRUSH hbrEdit = CreateSolidBrush(RGB(45, 45, 50));
    return (INT_PTR)hbrEdit;
  }

  case WM_CTLCOLORBTN: {
    return (LRESULT)CreateSolidBrush(RGB(30, 30, 30));
  }

  case WM_CTLCOLORSTATIC: {
    HDC hdcStatic = (HDC)wParam;
    SetTextColor(hdcStatic, RGB(200, 200, 210));
    SetBkMode(hdcStatic, TRANSPARENT);
    static HBRUSH hbrStatic = CreateSolidBrush(RGB(30, 30, 30));
    return (LRESULT)hbrStatic;
  }

  case WM_DRAWITEM: {
    LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
    if (dis->CtlType != ODT_BUTTON)
      break;

    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    // Pick colors based on which button
    COLORREF bgColor, textColor, borderColor;
    bool isStatus = (dis->hwndItem == hBtnStatus);
    bool isHovered = (dis->hwndItem == hHoveredButton);

    if (isStatus) {
      if (isSpamming) {
        bgColor = RGB(30, 120, 50); // Green
        borderColor = RGB(50, 180, 80);
      } else {
        bgColor = RGB(60, 60, 65);
        borderColor = RGB(80, 80, 90);
      }
      textColor = RGB(200, 200, 200);
    } else {
      // Apply button - accent purple
      bgColor = isHovered ? RGB(110, 70, 200) : RGB(88, 55, 170);
      borderColor = RGB(130, 90, 220);
      textColor = RGB(255, 255, 255);
    }

    // Fill rounded rectangle
    HBRUSH hBrush = CreateSolidBrush(bgColor);
    HPEN hPen = CreatePen(PS_SOLID, 2, borderColor);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    HPEN oldPen = (HPEN)SelectObject(hdc, hPen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 14, 14);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);

    // Draw text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    HFONT hFont =
        CreateFont(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                   DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
    char btnText[128];
    GetWindowTextA(dis->hwndItem, btnText, 128);
    DrawTextA(hdc, btnText, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
    DeleteObject(hFont);

    return TRUE;
  }

  case WM_COMMAND: {
    if (LOWORD(wParam) == 101) { // Mute checkbox toggled
      soundMuted = (SendMessage(hChkMute, BM_GETCHECK, 0, 0) == BST_CHECKED);
      SaveConfig();
    }
    return 0;
  }

  case WM_HSCROLL: {
    return 0;
  }

  case WM_TIMER: {
    if (wParam == 1) {
      // Auto-save: read all UI fields and apply silently
      soundMuted = (SendMessage(hChkMute, BM_GETCHECK, 0, 0) == BST_CHECKED);
      // soundVolume is already kept in sync by the custom volume bar
      ApplySettings();
      // Repaint volume bar in case volume changed externally
      InvalidateRect(hSliderVolume, NULL, FALSE);
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
      // Play custom start sound
      std::thread([]() { PlayMp3("start.mp3"); }).detach();
    } else if (wParam == 2 && isSpamming) { // Stop
      isSpamming = false;
      UpdateStatusUI();
      // Play custom stop sound
      std::thread([]() { PlayMp3("stop.mp3"); }).detach();
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
