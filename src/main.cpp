// main.cpp -- Palworld Hardcore Restorer (native Win32 GUI).
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <shlobj.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "palsave.hpp"

// Link the Win32 libraries we use, and request themed (v6) common controls.
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' "                     \
                        "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' " \
                        "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
                        "language='*'\"")

namespace fs = std::filesystem;

// ---- control IDs ----------------------------------------------------------
enum {
    IDC_DIR = 1001, IDC_BROWSE, IDC_REFRESH, IDC_LIST,
    IDC_STATUS, IDC_ENABLE, IDC_DISABLE, IDC_RESTORE, IDC_LOG
};

static HWND g_dir, g_list, g_status, g_enable, g_disable, g_restore, g_log, g_warn;
static std::vector<fs::path> g_worlds;
static HFONT g_font, g_titleFont;
static HBRUSH g_header;

constexpr int HEADER_H = 62;
static const COLORREF CLR_HEADER = RGB(28, 30, 34);   // charcoal band
static const COLORREF CLR_TEXT   = RGB(28, 28, 28);   // body text
static const COLORREF CLR_SUB    = RGB(176, 178, 182); // subtitle
static const COLORREF CLR_WARN   = RGB(198, 64, 64);  // warning line

// ---- helpers --------------------------------------------------------------
static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static void setText(HWND h, const std::wstring& s) { SetWindowTextW(h, s.c_str()); }

static void logLine(const std::wstring& s) {
    int len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, len, len);
    std::wstring line = s + L"\r\n";
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
}

static std::wstring dirText() {
    int n = GetWindowTextLengthW(g_dir);
    std::wstring s(n, 0);
    GetWindowTextW(g_dir, &s[0], n + 1);
    return s;
}

// ---- world list / status --------------------------------------------------
static void refresh() {
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    g_worlds = pal::findWorlds(fs::path(dirText()));
    if (g_worlds.empty()) {
        logLine(L"No worlds found under: " + dirText());
    }
    for (auto& w : g_worlds) {
        pal::Status s = pal::readStatus(w);
        std::wstring tag = !s.ok ? L"[error!] " : (s.hardcore ? L"[HARDCORE] " : L"[ normal ] ");
        SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)(tag + w.filename().wstring()).c_str());
    }
    setText(g_status, L"Select a world above.");
    EnableWindow(g_enable, FALSE);
    EnableWindow(g_disable, FALSE);
    EnableWindow(g_restore, FALSE);
    logLine(L"Found " + std::to_wstring(g_worlds.size()) + L" world(s).");
}

static int selected() {
    LRESULT i = SendMessageW(g_list, LB_GETCURSEL, 0, 0);
    return (i == LB_ERR) ? -1 : (int)i;
}

static void showSelected() {
    int i = selected();
    if (i < 0 || i >= (int)g_worlds.size()) return;
    pal::Status s = pal::readStatus(g_worlds[i]);
    std::wstring txt;
    if (!s.ok) {
        txt = L"Could not read world:\r\n" + widen(s.error);
        EnableWindow(g_enable, FALSE);
        EnableWindow(g_disable, FALSE);
    } else {
        txt = L"Folder:  " + g_worlds[i].wstring() + L"\r\n"
              L"Hardcore:  " + (s.hardcore ? L"ON  (permadeath active)" : L"OFF") + L"\r\n"
              L"Difficulty:  " + widen(s.difficulty) + L"\r\n"
              L"DeathPenalty:  " + widen(s.deathPenalty);
        EnableWindow(g_enable, !s.hardcore);
        EnableWindow(g_disable, s.hardcore);
    }
    EnableWindow(g_restore, pal::hasBackup(g_worlds[i]));
    setText(g_status, txt);
}

static bool guard() {
    if (pal::palworldRunning()) {
        int r = MessageBoxW(nullptr,
            L"Palworld looks like it's running. Changes may be overwritten or ignored "
            L"until you fully close the game.\n\nApply anyway?",
            L"Palworld appears to be running", MB_YESNO | MB_ICONWARNING);
        return r == IDYES;
    }
    return true;
}

static void doSet(bool enabled) {
    int i = selected();
    if (i < 0) return;
    if (!guard()) return;
    std::string summary, err; fs::path bak;
    if (!pal::setHardcore(g_worlds[i], enabled, summary, bak, err)) {
        MessageBoxW(nullptr, widen(err).c_str(), L"Error", MB_OK | MB_ICONERROR);
        logLine(L"ERROR: " + widen(err));
        return;
    }
    if (summary.empty()) {
        MessageBoxW(nullptr, L"This world is already in that state.", L"No change", MB_OK | MB_ICONINFORMATION);
        return;
    }
    logLine(L"backup : " + bak.wstring());
    logLine(L"change : " + widen(summary));
    MessageBoxW(nullptr,
        (widen(summary) + L"\n\nA backup was saved. Launch Palworld and load the world.").c_str(),
        enabled ? L"Hardcore enabled" : L"Hardcore disabled", MB_OK | MB_ICONINFORMATION);
    refresh();
    SendMessageW(g_list, LB_SETCURSEL, i, 0);
    showSelected();
}

static void doRestore() {
    int i = selected();
    if (i < 0) return;
    fs::path from; std::string err;
    if (!pal::restoreLatest(g_worlds[i], from, err)) {
        MessageBoxW(nullptr, widen(err).c_str(), L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    logLine(L"restored : " + from.wstring());
    MessageBoxW(nullptr, L"Restored the most recent backup for this world.", L"Restored",
                MB_OK | MB_ICONINFORMATION);
    showSelected();
}

static void browse() {
    BROWSEINFOW bi = {};
    bi.lpszTitle = L"Select your Palworld SaveGames folder (or a single world folder)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) { setText(g_dir, path); refresh(); }
        CoTaskMemFree(pidl);
    }
}

// ---- window ---------------------------------------------------------------
static HWND mk(const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int w, int h,
               HWND parent, int id) {
    HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        // Native UI font (Segoe UI 9 on Win10/11) + a larger semibold title font.
        NONCLIENTMETRICSW ncm; ncm.cbSize = sizeof(ncm);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            g_font = CreateFontIndirectW(&ncm.lfMessageFont);
        else
            g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_titleFont = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_header = CreateSolidBrush(CLR_HEADER);

        const int Y = HEADER_H;  // push all controls below the header band
        mk(L"STATIC", L"Save folder:", SS_LEFT, 16, Y + 18, 78, 20, hwnd, 0);
        g_dir = mk(L"EDIT", pal::defaultSaveRoot().wstring().c_str(),
                   WS_BORDER | ES_AUTOHSCROLL, 98, Y + 15, 296, 24, hwnd, IDC_DIR);
        mk(L"BUTTON", L"Browse…", BS_PUSHBUTTON, 400, Y + 14, 70, 26, hwnd, IDC_BROWSE);
        mk(L"BUTTON", L"Refresh", BS_PUSHBUTTON, 474, Y + 14, 70, 26, hwnd, IDC_REFRESH);

        mk(L"STATIC", L"Worlds", SS_LEFT, 16, Y + 50, 200, 18, hwnd, 0);
        g_list = mk(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                    16, Y + 70, 528, 128, hwnd, IDC_LIST);

        g_status = mk(L"EDIT", L"Select a world above.",
                      WS_BORDER | ES_MULTILINE | ES_READONLY, 16, Y + 208, 528, 88, hwnd, IDC_STATUS);

        g_enable  = mk(L"BUTTON", L"Enable Hardcore", BS_PUSHBUTTON, 16, Y + 306, 168, 32, hwnd, IDC_ENABLE);
        g_disable = mk(L"BUTTON", L"Disable Hardcore", BS_PUSHBUTTON, 196, Y + 306, 168, 32, hwnd, IDC_DISABLE);
        g_restore = mk(L"BUTTON", L"Restore Last Backup", BS_PUSHBUTTON, 376, Y + 306, 168, 32, hwnd, IDC_RESTORE);

        g_warn = mk(L"STATIC", L"⚠  Close Palworld before applying changes.", SS_LEFT,
                    16, Y + 350, 528, 18, hwnd, 0);

        g_log = mk(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                   16, Y + 374, 528, 84, hwnd, IDC_LOG);

        EnableWindow(g_enable, FALSE);
        EnableWindow(g_disable, FALSE);
        EnableWindow(g_restore, FALSE);
        refresh();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        RECT band = { 0, 0, rc.right, HEADER_H };
        FillRect(dc, &band, g_header);
        SetBkMode(dc, TRANSPARENT);
        HFONT old = (HFONT)SelectObject(dc, g_titleFont);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT tr = { 18, 9, rc.right - 12, 40 };
        DrawTextW(dc, L"Palworld Hardcore Restorer", -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(dc, g_font);
        SetTextColor(dc, CLR_SUB);
        RECT sr = { 20, 38, rc.right - 12, HEADER_H };
        DrawTextW(dc, L"Re-enable Hardcore mode on a single-player world", -1,
                  &sr, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(dc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, RGB(255, 255, 255));
        SetTextColor(dc, (HWND)lp == g_warn ? CLR_WARN : CLR_TEXT);
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDC_LIST && code == LBN_SELCHANGE) showSelected();
        else if (id == IDC_REFRESH) refresh();
        else if (id == IDC_BROWSE) browse();
        else if (id == IDC_ENABLE) doSet(true);
        else if (id == IDC_DISABLE) doSet(false);
        else if (id == IDC_RESTORE) doRestore();
        return 0;
    }
    case WM_DESTROY:
        if (g_titleFont) DeleteObject(g_titleFont);
        if (g_font) DeleteObject(g_font);
        if (g_header) DeleteObject(g_header);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- hidden self-test (headless verification) -----------------------------
static int selftest() {
    // --selftest status <folder> <report>   |   --selftest set <folder> <0|1> <report>
    std::wstring mode = __wargv[2];
    if (mode == L"status" && __argc >= 5) {
        pal::Status s = pal::readStatus(__wargv[3]);
        std::ofstream f(__wargv[4]);
        f << "ok=" << s.ok << " hardcore=" << s.hardcore
          << " diff=" << s.difficulty << " dp=" << s.deathPenalty
          << " err=" << s.error << "\n";
        return s.ok ? 0 : 1;
    }
    if (mode == L"set" && __argc >= 6) {
        bool en = std::wcstol(__wargv[4], nullptr, 10) != 0;
        std::string summary, err; fs::path bak;
        bool ok = pal::setHardcore(__wargv[3], en, summary, bak, err);
        std::ofstream f(__wargv[5]);
        f << "ok=" << ok << " summary=" << summary
          << " backup=" << bak.string() << " err=" << err << "\n";
        return ok ? 0 : 1;
    }
    return 2;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    if (__argc >= 2 && std::wcscmp(__wargv[1], L"--selftest") == 0)
        return selftest();

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"PalHardcoreRestorer";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Palworld Hardcore Restorer",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 576, 584, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(hwnd, &msg)) continue;  // Tab navigation
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
