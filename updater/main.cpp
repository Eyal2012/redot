// ReDotUpdater.exe - one-click updater for portable ReDot installs. Plain Win32/GDI, no Qt, no installer.
// Flow: detect ReDot -> check GitHub -> download -> verify signature -> stage -> back up -> install -> launch.
// It only asks for input when ReDot cannot be found, ReDot is running, or something failed.
#include "core.h"
#include "public_key.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

using namespace rdu;

namespace {

constexpr COLORREF kBg = RGB(22, 15, 38), kPanel = RGB(34, 24, 56), kText = RGB(236, 230, 250), kMuted = RGB(170, 158, 196),
                   kAccent = RGB(139, 92, 246), kAccentHot = RGB(160, 120, 255), kBorder = RGB(70, 54, 106), kError = RGB(255, 130, 146);
enum class Ui { Working, NeedFolder, NeedClose, Done, UpToDate, Error };
enum { IDA = 101, IDB = 102, IDC = 103, IDLIST = 104, IDNOTES = 105, WM_REFRESH = WM_APP + 1, TIMER_ANIM = 1 };

struct Shared {
    std::mutex m; std::condition_variable cv;
    Ui ui = Ui::Working; std::wstring title = L"ReDot Updater", status, detail, notes;
    double progress = -1; std::vector<fs::path> candidates; bool launched = false, canClose = true;
    // user -> worker
    bool promptReady = false, retry = false; fs::path promptPath;
} g;

struct Cmd { fs::path installDir, logFile; bool noLaunch = false, checkOnly = false; } cmd;
std::atomic<bool> g_cancel{false};
HWND g_wnd, g_list, g_notes, g_btn[3];
HFONT g_fTitle, g_fBody, g_fSmall;
HBRUSH g_bgBrush, g_panelBrush;
UINT g_dpi = 96;
std::thread g_worker;
fs::path g_installDir;
int g_anim = 0;

int S(int v) { return MulDiv(v, static_cast<int>(g_dpi), 96); }

void logLine(const std::wstring& s) {
    if (cmd.logFile.empty()) return;
    std::ofstream f(cmd.logFile, std::ios::app | std::ios::binary);
    f << (GetTickCount64() / 1000) << "s " << U8(s) << "\r\n";
}
void refresh() { if (g_wnd) PostMessageW(g_wnd, WM_REFRESH, 0, 0); }
void setState(Ui ui, const std::wstring& title, const std::wstring& status, const std::wstring& detail = L"", const std::wstring& notes = L"") {
    { std::lock_guard<std::mutex> l(g.m); g.ui = ui; g.title = title; g.status = status; g.detail = detail; g.notes = notes; }
    logLine(title + L" | " + status + (detail.empty() ? L"" : L" | " + detail));
    refresh();
}

struct UiReporter final : Reporter {
    void status(const std::wstring& t) override {
        { std::lock_guard<std::mutex> l(g.m); g.status = t; g.ui = Ui::Working; g.title = L"Updating ReDot"; g.canClose = t.rfind(L"Installing", 0) != 0; }
        logLine(t); refresh();
    }
    void progress(double f) override {
        { std::lock_guard<std::mutex> l(g.m); if (f >= 0 && static_cast<int>(f * 200) == static_cast<int>(g.progress * 200)) return; g.progress = f; }
        refresh();
    }
    bool waitForReDotClosed(const fs::path& exe) override {
        setState(Ui::NeedClose, L"Please close ReDot",
                 L"ReDot is running. Close it normally (stop any recording first) - the update continues automatically once it has closed.");
        while (isReDotRunning(exe)) {
            if (g_cancel) return false;
            Sleep(500);
        }
        setState(Ui::Working, L"Updating ReDot", L"ReDot closed. Continuing...");
        return !g_cancel;
    }
};

std::wstring knownFolder(const KNOWNFOLDERID& id) {
    PWSTR p = nullptr; std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) r = p;
    CoTaskMemFree(p); return r;
}
std::vector<fs::path> detectionRoots() {
    std::vector<fs::path> roots = runningReDotFolders();
    wchar_t self[MAX_PATH]{}; GetModuleFileNameW(nullptr, self, MAX_PATH);
    roots.push_back(fs::path(self).parent_path());
    for (const auto* id : {&FOLDERID_Desktop, &FOLDERID_Downloads, &FOLDERID_Documents, &FOLDERID_Profile, &FOLDERID_ProgramFiles, &FOLDERID_LocalAppData}) {
        std::wstring p = knownFolder(*id); if (!p.empty()) roots.push_back(p);
    }
    roots.push_back(L"C:\\"); roots.push_back(L"D:\\");
    return roots;
}

// Blocks until the user picks a folder (or cancels). Returns empty on cancel.
fs::path askForFolder(std::vector<fs::path> candidates) {
    std::wstring detail;
    for (;;) {
        {   std::lock_guard<std::mutex> l(g.m);
            g.ui = Ui::NeedFolder; g.title = L"Where is ReDot?"; g.candidates = candidates; g.promptReady = false; g.promptPath.clear();
            g.status = candidates.empty() ? L"ReDot wasn't found automatically. Choose the folder that contains ReDot.exe."
                                          : L"Several ReDot folders were found. Choose the one to update, or browse for another.";
            g.detail = detail; }
        refresh();
        std::unique_lock<std::mutex> l(g.m);
        g.cv.wait(l, [] { return g.promptReady || g_cancel.load(); });
        if (g_cancel) return {};
        fs::path chosen = g.promptPath; l.unlock();
        InstallInfo info; std::wstring why;
        if (identifyInstall(chosen, info, why) == IdentStatus::Ok) return chosen;
        detail = chosen.wstring() + L": " + why;
    }
}

bool launchReDot(const fs::path& dir) {
    fs::path exe = dir / L"ReDot.exe";
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    std::wstring wd = dir.wstring();
    if (!CreateProcessW(exe.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, wd.c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return true;
}

std::wstring crlf(const std::string& utf8) {
    std::wstring w = W(utf8), r;
    for (size_t i = 0; i < w.size(); ++i) { if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) r += L'\r'; r += w[i]; }
    return r;
}

void workerMain() {
    auto net = makeWinHttpNet();
    UiReporter rep;
    for (;;) {
        fs::path dir = cmd.installDir;
        if (dir.empty()) {
            setState(Ui::Working, L"Updating ReDot", L"Looking for your ReDot installation...");
            auto found = findInstallations(detectionRoots());
            if (found.size() == 1) dir = found[0];
            else { dir = askForFolder(found); if (dir.empty()) break; }
        }
        g_installDir = dir;
        { std::lock_guard<std::mutex> l(g.m); g.progress = -1; g.canClose = true; }
        Options o; o.installDir = dir; o.publicKey = kReleasePublicKey; o.publicKeySize = kReleasePublicKeySize; o.cancel = &g_cancel; o.checkOnly = cmd.checkOnly;
        RunResult r = runUpdate(o, *net, rep);
        { std::lock_guard<std::mutex> l(g.m); g.canClose = true; }
        std::wstring notice = r.notice;
        if (r.outcome == Outcome::Updated) {
            bool ok = !cmd.noLaunch && launchReDot(dir);
            { std::lock_guard<std::mutex> l(g.m); g.launched = ok; }
            setState(Ui::Done, L"Update Complete", r.message + (ok ? L" ReDot has been started." : L""), notice, crlf(r.notes));
        } else if (r.outcome == Outcome::UpToDate || r.outcome == Outcome::UpdateAvailable) {
            setState(Ui::UpToDate, r.outcome == Outcome::UpToDate ? L"ReDot is up to date" : L"Update available", r.message, notice);
        } else if (r.outcome == Outcome::Cancelled) {
            break;
        } else {
            setState(Ui::Error, L"The update didn't finish", r.message, (notice.empty() ? L"" : notice + L"\r\n") + r.recovery);
            std::unique_lock<std::mutex> l(g.m);
            g.retry = false;
            g.cv.wait(l, [] { return g.retry || g_cancel.load(); });
            if (g_cancel) break;
            continue;                                              // "Try again"
        }
        break;
    }
    if (g_wnd) PostMessageW(g_wnd, WM_APP + 2, 0, 0);               // worker finished
}

// ---- UI -------------------------------------------------------------------------------------------------
void layout() {
    RECT rc; GetClientRect(g_wnd, &rc);
    int w = rc.right, h = rc.bottom, m = S(28);
    Ui ui; { std::lock_guard<std::mutex> l(g.m); ui = g.ui; }
    int bodyTop = S(150), bodyBottom = h - S(78);
    SetWindowPos(g_list, nullptr, m, bodyTop, w - 2 * m, bodyBottom - bodyTop, SWP_NOZORDER);
    SetWindowPos(g_notes, nullptr, m, bodyTop, w - 2 * m, bodyBottom - bodyTop, SWP_NOZORDER);
    int bw = S(130), bh = S(36), by = h - S(60);
    SetWindowPos(g_btn[0], nullptr, w - m - bw, by, bw, bh, SWP_NOZORDER);
    SetWindowPos(g_btn[1], nullptr, w - m - 2 * bw - S(10), by, bw, bh, SWP_NOZORDER);
    SetWindowPos(g_btn[2], nullptr, m, by, bw, bh, SWP_NOZORDER);
    (void)ui;
}
void makeFonts() {
    for (HFONT f : {g_fTitle, g_fBody, g_fSmall}) if (f) DeleteObject(f);
    g_fTitle = CreateFontW(-MulDiv(20, static_cast<int>(g_dpi), 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fBody = CreateFontW(-MulDiv(10, static_cast<int>(g_dpi), 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fSmall = CreateFontW(-MulDiv(9, static_cast<int>(g_dpi), 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    for (HWND c : {g_list, g_notes, g_btn[0], g_btn[1], g_btn[2]}) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_fBody), TRUE);
}
void applyState() {
    Ui ui; std::vector<fs::path> cands; std::wstring notes; bool launched;
    { std::lock_guard<std::mutex> l(g.m); ui = g.ui; cands = g.candidates; notes = g.notes; launched = g.launched; }
    auto show = [](HWND h, bool v, const wchar_t* text = nullptr) { ShowWindow(h, v ? SW_SHOW : SW_HIDE); if (text) SetWindowTextW(h, text); };
    switch (ui) {
    case Ui::Working: show(g_btn[0], false); show(g_btn[1], true, L"Cancel"); show(g_btn[2], false); show(g_list, false); show(g_notes, false); break;
    case Ui::NeedClose: show(g_btn[0], false); show(g_btn[1], true, L"Cancel"); show(g_btn[2], false); show(g_list, false); show(g_notes, false); break;
    case Ui::NeedFolder:
        SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
        for (auto& c : cands) SendMessageW(g_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(c.c_str()));
        if (!cands.empty()) SendMessageW(g_list, LB_SETCURSEL, 0, 0);
        show(g_btn[0], !cands.empty(), L"Use selected"); show(g_btn[1], true, L"Cancel"); show(g_btn[2], true, L"Browse...");
        show(g_list, !cands.empty()); show(g_notes, false); break;
    case Ui::Done:
        show(g_btn[0], true, L"Close"); show(g_btn[1], false); show(g_btn[2], !launched, L"Launch ReDot"); show(g_list, false);
        SetWindowTextW(g_notes, notes.c_str()); show(g_notes, !notes.empty()); break;
    case Ui::UpToDate:
        show(g_btn[0], true, L"Close"); show(g_btn[1], false); show(g_btn[2], true, L"Launch ReDot"); show(g_list, false); show(g_notes, false); break;
    case Ui::Error:
        show(g_btn[0], true, L"Try again"); show(g_btn[1], true, L"Close"); show(g_btn[2], false); show(g_list, false); show(g_notes, false); break;
    }
    layout();
    InvalidateRect(g_wnd, nullptr, FALSE);
}

fs::path browseFolder(HWND owner) {
    fs::path result; IFileOpenDialog* d = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) {
        DWORD opts = 0; d->GetOptions(&opts); d->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        d->SetTitle(L"Select the folder that contains ReDot.exe");
        if (SUCCEEDED(d->Show(owner))) { IShellItem* it = nullptr; if (SUCCEEDED(d->GetResult(&it))) { PWSTR p = nullptr; if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { result = p; CoTaskMemFree(p); } it->Release(); } }
        d->Release();
    }
    return result;
}

void paint(HDC dc, RECT rc) {
    HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ old = SelectObject(mem, bmp);
    FillRect(mem, &rc, g_bgBrush);
    Ui ui; std::wstring title, status, detail; double progress;
    { std::lock_guard<std::mutex> l(g.m); ui = g.ui; title = g.title; status = g.status; detail = g.detail; progress = g.progress; }
    int m = S(28);
    // brand dot: a red-ringed accent dot
    HBRUSH accent = CreateSolidBrush(kAccent); HPEN none = static_cast<HPEN>(GetStockObject(NULL_PEN));
    SelectObject(mem, accent); SelectObject(mem, none); Ellipse(mem, m, S(30), m + S(18), S(48)); DeleteObject(accent);
    SetBkMode(mem, TRANSPARENT);
    SelectObject(mem, g_fTitle); SetTextColor(mem, kText);
    RECT tr{m + S(28), S(24), rc.right - m, S(60)}; DrawTextW(mem, title.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(mem, g_fBody); SetTextColor(mem, ui == Ui::Error ? kError : kText);
    RECT sr{m, S(72), rc.right - m, S(120)}; DrawTextW(mem, status.c_str(), -1, &sr, DT_WORDBREAK | DT_END_ELLIPSIS);
    if (ui == Ui::Working || ui == Ui::NeedClose) {
        RECT bar{m, S(124), rc.right - m, S(124) + S(10)};
        HBRUSH track = CreateSolidBrush(kPanel); FillRect(mem, &bar, track); DeleteObject(track);
        HBRUSH fill = CreateSolidBrush(kAccent);
        if (ui == Ui::NeedClose || progress < 0) {
            int span = bar.right - bar.left, seg = span / 4, pos = (g_anim * S(6)) % (span + seg) - seg;
            RECT s{std::max<LONG>(bar.left, bar.left + pos), bar.top, std::min<LONG>(bar.right, bar.left + pos + seg), bar.bottom};
            if (s.right > s.left) FillRect(mem, &s, fill);
        } else { RECT f = bar; f.right = bar.left + static_cast<int>((bar.right - bar.left) * std::min(1.0, progress)); FillRect(mem, &f, fill); }
        DeleteObject(fill);
    } else if (!detail.empty()) {
        SelectObject(mem, g_fSmall); SetTextColor(mem, kMuted);
        RECT dr{m, S(122), rc.right - m, S(150)}; DrawTextW(mem, detail.c_str(), -1, &dr, DT_WORDBREAK | DT_END_ELLIPSIS);
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
}

void drawButton(const DRAWITEMSTRUCT* d) {
    bool primary = d->CtlID == IDA, pressed = (d->itemState & ODS_SELECTED) != 0, disabled = (d->itemState & ODS_DISABLED) != 0;
    HBRUSH fill = CreateSolidBrush(primary ? (pressed ? kAccent : kAccentHot) : (pressed ? kBorder : kPanel));
    HPEN pen = CreatePen(PS_SOLID, 1, primary ? kAccentHot : kBorder);
    FillRect(d->hDC, &d->rcItem, g_bgBrush);
    SelectObject(d->hDC, fill); SelectObject(d->hDC, pen);
    RoundRect(d->hDC, d->rcItem.left, d->rcItem.top, d->rcItem.right, d->rcItem.bottom, S(10), S(10));
    wchar_t text[64]; GetWindowTextW(d->hwndItem, text, 64);
    SetBkMode(d->hDC, TRANSPARENT); SetTextColor(d->hDC, disabled ? kMuted : primary ? RGB(255, 255, 255) : kText); SelectObject(d->hDC, g_fBody);
    RECT r = d->rcItem; DrawTextW(d->hDC, text, -1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    if (d->itemState & ODS_FOCUS) { RECT f = d->rcItem; InflateRect(&f, -S(4), -S(4)); DrawFocusRect(d->hDC, &f); }
    DeleteObject(fill); DeleteObject(pen);
}

void signalWorker(const std::function<void()>& f) { { std::lock_guard<std::mutex> l(g.m); f(); } g.cv.notify_all(); }

void onButton(int id) {
    logLine(L"button " + std::to_wstring(id) + L" clicked");
    Ui ui; { std::lock_guard<std::mutex> l(g.m); ui = g.ui; }
    if (id == IDA) {
        if (ui == Ui::NeedFolder) {
            LRESULT sel = SendMessageW(g_list, LB_GETCURSEL, 0, 0); fs::path p;
            { std::lock_guard<std::mutex> l(g.m); if (sel >= 0 && static_cast<size_t>(sel) < g.candidates.size()) p = g.candidates[static_cast<size_t>(sel)]; }
            if (!p.empty()) signalWorker([&] { g.promptPath = p; g.promptReady = true; });
        } else if (ui == Ui::Error) signalWorker([] { g.retry = true; });
        else DestroyWindow(g_wnd);
    } else if (id == IDB) {
        if (ui == Ui::Error) { g_cancel = true; g.cv.notify_all(); DestroyWindow(g_wnd); }
        else { g_cancel = true; g.cv.notify_all(); if (ui == Ui::Working) setState(Ui::Working, L"Cancelling", L"Cancelling..."); }
    } else if (id == IDC) {
        if (ui == Ui::NeedFolder) { fs::path p = browseFolder(g_wnd); if (!p.empty()) signalWorker([&] { g.promptPath = p; g.promptReady = true; }); }
        else if (ui == Ui::Done || ui == Ui::UpToDate) { if (launchReDot(g_installDir)) DestroyWindow(g_wnd); }
    }
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: SetTimer(h, TIMER_ANIM, 40, nullptr); return 0;
    case WM_TIMER: {
        Ui ui; double p; { std::lock_guard<std::mutex> l(g.m); ui = g.ui; p = g.progress; }
        if ((ui == Ui::Working && p < 0) || ui == Ui::NeedClose) { ++g_anim; RECT r{0, S(120), 4000, S(140)}; InvalidateRect(h, &r, FALSE); }
        return 0; }
    case WM_REFRESH: applyState(); return 0;
    case WM_APP + 2: {
        Ui ui; { std::lock_guard<std::mutex> l(g.m); ui = g.ui; }
        if (ui == Ui::Working || ui == Ui::NeedClose || ui == Ui::NeedFolder || cmd.checkOnly) DestroyWindow(h);   // cancelled, or a headless check finished
        return 0; }
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); paint(dc, rc); EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORLISTBOX: case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wp), kText); SetBkColor(reinterpret_cast<HDC>(wp), kPanel); return reinterpret_cast<LRESULT>(g_panelBrush);
    case WM_DRAWITEM: drawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
    case WM_COMMAND: if (HIWORD(wp) == BN_CLICKED) onButton(LOWORD(wp)); return 0;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp); makeFonts(); const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); layout(); return 0; }
    case WM_CLOSE: {
        bool can; { std::lock_guard<std::mutex> l(g.m); can = g.canClose; }
        if (!can) { MessageBeep(MB_ICONEXCLAMATION); return 0; }       // never interrupt the file swap
        g_cancel = true; g.cv.notify_all();
        if (g_worker.joinable()) g_worker.join();
        DestroyWindow(h); return 0; }
    case WM_DESTROY: KillTimer(h, TIMER_ANIM); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void parseCmd() {
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--install-dir" && i + 1 < argc) cmd.installDir = argv[++i];
        else if (a == L"--log" && i + 1 < argc) cmd.logFile = argv[++i];
        else if (a == L"--no-launch") cmd.noLaunch = true;
        else if (a == L"--check-only") cmd.checkOnly = true;
    }
    if (argv) LocalFree(argv);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&icc);
    parseCmd();
    g_bgBrush = CreateSolidBrush(kBg); g_panelBrush = CreateSolidBrush(kPanel);
    WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = wndProc; wc.hInstance = inst; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_bgBrush; wc.lpszClassName = L"ReDotUpdaterWindow"; wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    g_dpi = GetDpiForSystem();
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc{0, 0, S(560), S(380)}; AdjustWindowRectExForDpi(&rc, style, FALSE, 0, g_dpi);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN), w = rc.right - rc.left, h = rc.bottom - rc.top;
    g_wnd = CreateWindowExW(0, wc.lpszClassName, L"ReDot Updater", style, (sw - w) / 2, (sh - h) / 2, w, h, nullptr, nullptr, inst, nullptr);
    g_dpi = GetDpiForWindow(g_wnd);
    BOOL dark = TRUE; DwmSetWindowAttribute(g_wnd, 20, &dark, sizeof(dark));
    g_list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, g_wnd, reinterpret_cast<HMENU>(IDLIST), inst, nullptr);
    g_notes = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 10, 10, g_wnd, reinterpret_cast<HMENU>(IDNOTES), inst, nullptr);
    for (int i = 0; i < 3; ++i) g_btn[i] = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, g_wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDA + i)), inst, nullptr);
    makeFonts(); layout();
    setState(Ui::Working, L"Updating ReDot", L"Starting...");
    ShowWindow(g_wnd, SW_SHOW); UpdateWindow(g_wnd);
    g_worker = std::thread(workerMain);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { if (!IsDialogMessageW(g_wnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); } }
    if (g_worker.joinable()) { g_cancel = true; g.cv.notify_all(); g_worker.join(); }
    CoUninitialize();
    return 0;
}
