// PodcastMP3.exe - Windows window for Podcast MP3 Downloader.
//
//   PodcastMP3.exe                         main window: paste a URL, pick speed, download
//   PodcastMP3.exe --job ID --url URL ...  small progress window (started by the Edge extension)
//
// Plain Win32 + common controls, no MFC/ATL, so it builds with any Visual
// Studio C++ workload and with MinGW.

#include "../core/host.h"
#include "../core/job.h"
#include "../core/childproc.h"
#include "../core/settings.h"
#include "../core/builder.h"
#include "../core/version.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "resource.h"

using namespace pmp3;

namespace {

enum : UINT {
    WM_APP_STATUS = WM_APP + 1,  // lParam = JobStatus*
    WM_APP_LOG,                  // lParam = std::string*
    WM_APP_FINISHED,             // lParam = JobStatus*
    WM_APP_TEXT,                 // lParam = std::string* (yt-dlp update output)
};

enum CtlId {
    IDC_URL = 100,
    IDC_PASTE,
    IDC_SPEED,
    IDC_BITRATE,
    IDC_FOLDER,
    IDC_BROWSE,
    IDC_OPENFOLDER,
    IDC_DOWNLOAD,
    IDC_PROGRESS,
    IDC_STATUS,
    IDC_LOG,
    IDC_VERSIONLINE,
    IDC_UPDATE,
    IDC_ABOUT,
    IDC_TITLE,
    IDC_CANCEL,
    IDC_PLAY,
    IDC_CLOSE,
    IDC_SHOWLOG,
    IDC_LBL_URL,
    IDC_LBL_SPEED,
    IDC_LBL_BITRATE,
    IDC_VOLUME,
    IDC_LBL_VOLUME,
    IDC_LBL_FOLDER,
};

const UINT_PTR kAutoCloseTimer = 1;

struct App {
    bool jobMode = false;
    HWND hwnd = nullptr;
    HFONT font = nullptr, boldFont = nullptr;
    int dpi = 96;
    std::vector<std::pair<int, HWND>> controls;

    // job state
    JobRequest request;
    std::unique_ptr<JobRunner> runner;
    std::thread worker;
    std::atomic<bool> running{false};
    JobStatus last;
    int closeCountdown = 0;
    std::string ytdlpVersionCached;
};

App g;

int S(int v) { return MulDiv(v, g.dpi, 96); }

HWND ctl(int id) { return GetDlgItem(g.hwnd, id); }

std::wstring W(const std::string& s) { return widen(s); }

std::string getText(int id) {
    HWND h = ctl(id);
    int n = GetWindowTextLengthW(h);
    std::wstring w((size_t)n + 1, L'\0');
    GetWindowTextW(h, &w[0], n + 1);
    w.resize((size_t)n);
    return narrow(w);
}

void setText(int id, const std::string& s) { SetWindowTextW(ctl(id), W(s).c_str()); }

HWND add(const wchar_t* cls, const std::wstring& text, DWORD style, int id, DWORD exStyle = 0) {
    HWND h = CreateWindowExW(exStyle, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g.hwnd,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    g.controls.push_back({id, h});
    return h;
}

void move(int id, int x, int y, int w, int h) { MoveWindow(ctl(id), S(x), S(y), S(w), S(h), TRUE); }

void makeFonts() {
    if (g.font) DeleteObject(g.font);
    if (g.boldFont) DeleteObject(g.boldFont);
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    // lfMessageFont is for the system DPI; rescale for this window's monitor.
    HDC dc = GetDC(nullptr);
    int sysDpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    lf.lfHeight = MulDiv(lf.lfHeight, g.dpi, sysDpi);
    g.font = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_SEMIBOLD;
    g.boldFont = CreateFontIndirectW(&lf);
    for (auto& c : g.controls) SendMessageW(c.second, WM_SETFONT, (WPARAM)g.font, TRUE);
    if (g.jobMode && ctl(IDC_TITLE)) SendMessageW(ctl(IDC_TITLE), WM_SETFONT, (WPARAM)g.boldFont, TRUE);
}

int windowDpi(HWND h) {
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static auto fn = (GetDpiForWindowFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && h) return (int)fn(h);
    HDC dc = GetDC(nullptr);
    int d = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    return d;
}

void appendLog(const std::string& line) {
    HWND h = ctl(IDC_LOG);
    if (!h) return;
    std::wstring w = W(line) + L"\r\n";
    int len = GetWindowTextLengthW(h);
    if (len > 200000) {
        SendMessageW(h, EM_SETSEL, 0, 100000);
        SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)L"");
        len = GetWindowTextLengthW(h);
    }
    SendMessageW(h, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)w.c_str());
}

std::string versionLine() {
    std::string ext = extensionVersionOnDisk();
    std::string s = "App v" PMP3_VERSION_FULL " - " PMP3_BUILDER_NAME;
    s += ext.empty() ? std::string("  |  Extension files: not found") : "  |  Extension files v" + ext;
    if (!g.ytdlpVersionCached.empty()) s += "  |  yt-dlp " + g.ytdlpVersionCached;
    s += nativeHostRegistered() ? "  |  Edge link OK" : "  |  Edge link NOT registered";
    return s;
}

double comboSpeed() {
    int i = (int)SendMessageW(ctl(IDC_SPEED), CB_GETCURSEL, 0, 0);
    return (i >= 0 && i < kSpeedChoiceCount) ? kSpeedChoices[i] : 1.5;
}

double comboVolume() {
    int i = (int)SendMessageW(ctl(IDC_VOLUME), CB_GETCURSEL, 0, 0);
    return (i >= 0 && i < kVolumeChoiceCount) ? kVolumeChoices[i] : 1.25;
}

int comboBitrate() {
    int i = (int)SendMessageW(ctl(IDC_BITRATE), CB_GETCURSEL, 0, 0);
    return (i >= 0 && i < kBitrateChoiceCount) ? kBitrateChoices[i] : 192;
}

std::string clipboardUrl() {
    std::string result;
    if (!OpenClipboard(g.hwnd)) return result;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        auto p = (const wchar_t*)GlobalLock(h);
        if (p) {
            std::string s = trim(narrow(p));
            if (looksLikeUrl(s)) result = s;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return result;
}

// ---- layout -------------------------------------------------------------

void layoutMain() {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    int w = MulDiv(rc.right, 96, g.dpi), h = MulDiv(rc.bottom, 96, g.dpi);
    const int m = 12, lbl = 92, rowH = 24;
    int x2 = m + lbl;
    int y = m;
    move(IDC_LBL_URL, m, y + 4, lbl, 18);
    move(IDC_URL, x2, y, w - x2 - m - 80, rowH);
    move(IDC_PASTE, w - m - 74, y, 74, rowH);
    y += rowH + 10;
    move(IDC_LBL_SPEED, m, y + 4, lbl, 18);
    move(IDC_SPEED, x2, y, 90, 200);
    move(IDC_LBL_BITRATE, x2 + 110, y + 4, 60, 18);
    move(IDC_BITRATE, x2 + 170, y, 120, 200);
    move(IDC_LBL_VOLUME, x2 + 310, y + 4, 86, 18);
    move(IDC_VOLUME, x2 + 396, y, 80, 200);
    y += rowH + 10;
    move(IDC_LBL_FOLDER, m, y + 4, lbl, 18);
    move(IDC_FOLDER, x2, y, w - x2 - m - 160, rowH);
    move(IDC_BROWSE, w - m - 154, y, 74, rowH);
    move(IDC_OPENFOLDER, w - m - 74, y, 74, rowH);
    y += rowH + 14;
    move(IDC_DOWNLOAD, x2, y, 150, 30);
    move(IDC_CANCEL, x2 + 160, y, 90, 30);
    y += 30 + 12;
    move(IDC_PROGRESS, m, y, w - 2 * m, 18);
    y += 18 + 6;
    move(IDC_STATUS, m, y, w - 2 * m, 18);
    y += 18 + 6;
    int versionY = h - m - 18;
    int buttonsY = versionY - 6 - 26;
    move(IDC_LOG, m, y, w - 2 * m, std::max(40, buttonsY - y - 8));
    move(IDC_UPDATE, w - m - 194, buttonsY, 110, 26);
    move(IDC_ABOUT, w - m - 78, buttonsY, 78, 26);
    move(IDC_VERSIONLINE, m, versionY, w - 2 * m, 18);
}

void layoutJob() {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    int w = MulDiv(rc.right, 96, g.dpi);
    const int m = 12;
    move(IDC_TITLE, m, m, w - 2 * m, 20);
    move(IDC_PROGRESS, m, m + 28, w - 2 * m, 18);
    move(IDC_STATUS, m, m + 52, w - 2 * m, 36);
    int by = m + 94;
    move(IDC_SHOWLOG, m, by, 80, 28);
    move(IDC_PLAY, w - m - 3 * 92 + 2, by, 88, 28);
    move(IDC_OPENFOLDER, w - m - 2 * 92 + 2, by, 88, 28);
    move(IDC_CANCEL, w - m - 90, by, 90, 28);
    move(IDC_CLOSE, w - m - 90, by, 90, 28);
}

void layout() {
    if (g.jobMode)
        layoutJob();
    else
        layoutMain();
}

// ---- job control -----------------------------------------------------------

void showFinishedButtons(const JobStatus& s) {
    bool done = s.state == JobState::Done;
    ShowWindow(ctl(IDC_CANCEL), SW_HIDE);
    if (g.jobMode) {
        ShowWindow(ctl(IDC_CLOSE), SW_SHOW);
        ShowWindow(ctl(IDC_PLAY), done ? SW_SHOW : SW_HIDE);
        ShowWindow(ctl(IDC_OPENFOLDER), done ? SW_SHOW : SW_HIDE);
        ShowWindow(ctl(IDC_SHOWLOG), SW_SHOW);
    } else {
        EnableWindow(ctl(IDC_DOWNLOAD), TRUE);
    }
}

void startJob(const JobRequest& req) {
    if (g.running) return;
    g.request = req;
    g.running = true;
    g.last = JobStatus();
    SendMessageW(ctl(IDC_PROGRESS), PBM_SETPOS, 0, 0);
    if (!g.jobMode) {
        EnableWindow(ctl(IDC_DOWNLOAD), FALSE);
        ShowWindow(ctl(IDC_CANCEL), SW_SHOW);
        appendLog("---- " + nowLocalString() + "  " + req.url);
    }
    HWND hwnd = g.hwnd;
    g.runner.reset(new JobRunner(
        req,
        [hwnd](const JobStatus& s) { PostMessageW(hwnd, WM_APP_STATUS, 0, (LPARAM) new JobStatus(s)); },
        [hwnd](const std::string& line) { PostMessageW(hwnd, WM_APP_LOG, 0, (LPARAM) new std::string(line)); }));
    if (g.worker.joinable()) g.worker.join();
    JobRunner* r = g.runner.get();
    g.worker = std::thread([r, hwnd] {
        JobStatus s = r->run();
        PostMessageW(hwnd, WM_APP_FINISHED, 0, (LPARAM) new JobStatus(s));
    });
}

void onStatus(const JobStatus& s) {
    g.last = s;
    SendMessageW(ctl(IDC_PROGRESS), PBM_SETPOS, (WPARAM)(int)std::lround(s.percent * 10), 0);
    std::string text = s.state == JobState::Failed ? "Failed: " + s.error : s.message;
    setText(IDC_STATUS, text);
    if (g.jobMode) {
        std::string title = s.title.empty() ? s.url : s.title;
        if (!s.channel.empty()) title = s.channel + " - " + title;
        setText(IDC_TITLE, title);
        std::wstring caption = W((s.state == JobState::Done ? std::string("Done") : std::to_string((int)s.percent) + "%") +
                                 " - " PMP3_PRODUCT_NAME " v" PMP3_VERSION " - " PMP3_BUILDER_NAME);
        SetWindowTextW(g.hwnd, caption.c_str());
    }
}

void onFinished(const JobStatus& s) {
    g.running = false;
    if (g.worker.joinable()) g.worker.join();
    onStatus(s);
    showFinishedButtons(s);
    if (s.state == JobState::Done) {
        if (!g.jobMode) appendLog("Saved: " + s.output);
        Settings st = loadSettings();
        if (g.jobMode && st.autoCloseSeconds > 0) {
            g.closeCountdown = st.autoCloseSeconds;
            SetTimer(g.hwnd, kAutoCloseTimer, 1000, nullptr);
        }
    } else if (s.state == JobState::Failed) {
        if (!g.jobMode) appendLog("Failed: " + s.error);
        FlashWindow(g.hwnd, TRUE);
    }
}

void onDownloadClicked() {
    std::string url = trim(getText(IDC_URL));
    if (!looksLikeUrl(url)) {
        MessageBoxW(g.hwnd, L"Please paste the web address (https://...) of the page that shows the video.",
                    TEXT(PMP3_PRODUCT_NAME), MB_ICONINFORMATION);
        SetFocus(ctl(IDC_URL));
        return;
    }
    Settings s = loadSettings();
    s.speed = comboSpeed();
    s.bitrateKbps = comboBitrate();
    s.volume = comboVolume();
    std::string folder = trim(getText(IDC_FOLDER));
    if (!folder.empty()) s.outputDir = folder;
    saveSettings(s);
    JobRequest req = requestFromSettings(s, url);
    req.source = "app";
    startJob(req);
}

void browseFolder() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"Where should the MP3 files be saved?");
    IShellItem* start = nullptr;
    std::wstring cur = W(getText(IDC_FOLDER));
    if (!cur.empty() && SUCCEEDED(SHCreateItemFromParsingName(cur.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
        dlg->SetFolder(start);
        start->Release();
    }
    if (SUCCEEDED(dlg->Show(g.hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                SetWindowTextW(ctl(IDC_FOLDER), path);
                Settings s = loadSettings();
                s.outputDir = narrow(path);
                saveSettings(s);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
}

void showAbout() {
    std::string text = std::string(PMP3_PRODUCT_NAME "\n\nApp version:\t") + PMP3_VERSION_FULL +
                       "\nBuilt by:\t" PMP3_BUILDER_NAME +
                       "\nExtension files:\t" + extensionVersionOnDisk() + "\nyt-dlp:\t\t" + ytDlpVersion() +
                       "\nffmpeg:\t\t" + ffmpegVersion() + "\n\nInstalled in:\t" + pathToUtf8(executableDir()) +
                       "\nExtension folder:\t" + pathToUtf8(extensionDir()) + "\nSettings:\t" +
                       pathToUtf8(settingsDir()) + "\nJob logs:\t" + pathToUtf8(logsDir()) +
                       "\n\nEdge link registered:\t" + (nativeHostRegistered() ? "yes" : "no") +
                       "\n\nhttps://github.com/Bio-Printer/Podcast-MP3-downloader";
    MessageBoxW(g.hwnd, W(text).c_str(), L"About", MB_ICONINFORMATION);
}

void updateYtDlpAsync() {
    EnableWindow(ctl(IDC_UPDATE), FALSE);
    appendLog("Updating yt-dlp...");
    HWND hwnd = g.hwnd;
    std::thread([hwnd] {
        int code = 0;
        std::string outText = updateYtDlp(code);
        std::string v = ytDlpVersion();
        PostMessageW(hwnd, WM_APP_TEXT, (WPARAM)code,
                     (LPARAM) new std::string(outText + "\r\nyt-dlp version now: " + v + "\n" + v));
    }).detach();
}

// ---- window creation ------------------------------------------------------

void createMainControls() {
    Settings s = loadSettings();
    add(L"STATIC", L"Video page URL:", 0, IDC_LBL_URL);
    add(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_URL);
    SendMessageW(ctl(IDC_URL), EM_SETCUEBANNER, TRUE, (LPARAM)L"https://rumble.com/...");
    add(L"BUTTON", L"Paste", WS_TABSTOP | BS_PUSHBUTTON, IDC_PASTE);
    add(L"STATIC", L"Playback speed:", 0, IDC_LBL_SPEED);
    add(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_SPEED);
    for (int i = 0; i < kSpeedChoiceCount; ++i) {
        std::wstring t = W(formatSpeed(kSpeedChoices[i]) + "x");
        SendMessageW(ctl(IDC_SPEED), CB_ADDSTRING, 0, (LPARAM)t.c_str());
        if (std::fabs(kSpeedChoices[i] - s.speed) < 0.001) SendMessageW(ctl(IDC_SPEED), CB_SETCURSEL, (WPARAM)i, 0);
    }
    if (SendMessageW(ctl(IDC_SPEED), CB_GETCURSEL, 0, 0) < 0) SendMessageW(ctl(IDC_SPEED), CB_SETCURSEL, 2, 0);
    add(L"STATIC", L"Quality:", 0, IDC_LBL_BITRATE);
    add(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_BITRATE);
    for (int i = 0; i < kBitrateChoiceCount; ++i) {
        std::wstring t = W(std::to_string(kBitrateChoices[i]) + " kbps stereo");
        SendMessageW(ctl(IDC_BITRATE), CB_ADDSTRING, 0, (LPARAM)t.c_str());
        if (kBitrateChoices[i] == s.bitrateKbps) SendMessageW(ctl(IDC_BITRATE), CB_SETCURSEL, (WPARAM)i, 0);
    }
    if (SendMessageW(ctl(IDC_BITRATE), CB_GETCURSEL, 0, 0) < 0)
        SendMessageW(ctl(IDC_BITRATE), CB_SETCURSEL, (WPARAM)(kBitrateChoiceCount - 1), 0);
    add(L"STATIC", L"Volume boost:", 0, IDC_LBL_VOLUME);
    add(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_VOLUME);
    int volSel = 2;  // 1.25x
    for (int i = 0; i < kVolumeChoiceCount; ++i) {
        std::wstring t = W(formatSpeed(kVolumeChoices[i]) + "x");
        SendMessageW(ctl(IDC_VOLUME), CB_ADDSTRING, 0, (LPARAM)t.c_str());
        if (std::fabs(kVolumeChoices[i] - s.volume) < 0.001) volSel = i;
    }
    SendMessageW(ctl(IDC_VOLUME), CB_SETCURSEL, (WPARAM)volSel, 0);
    add(L"STATIC", L"Save MP3 in:", 0, IDC_LBL_FOLDER);
    add(L"EDIT", W(s.outputDir), WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_FOLDER);
    add(L"BUTTON", L"Browse...", WS_TABSTOP | BS_PUSHBUTTON, IDC_BROWSE);
    add(L"BUTTON", L"Open", WS_TABSTOP | BS_PUSHBUTTON, IDC_OPENFOLDER);
    add(L"BUTTON", L"Download MP3", WS_TABSTOP | BS_DEFPUSHBUTTON, IDC_DOWNLOAD);
    add(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, IDC_CANCEL);
    ShowWindow(ctl(IDC_CANCEL), SW_HIDE);
    add(PROGRESS_CLASSW, L"", 0, IDC_PROGRESS);
    SendMessageW(ctl(IDC_PROGRESS), PBM_SETRANGE32, 0, 1000);
    add(L"STATIC", L"Paste a Rumble video link and press Download MP3.", SS_ENDELLIPSIS, IDC_STATUS);
    add(L"EDIT", L"", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_BORDER, IDC_LOG);
    add(L"STATIC", W(versionLine()), SS_ENDELLIPSIS, IDC_VERSIONLINE);
    add(L"BUTTON", L"Update yt-dlp", WS_TABSTOP | BS_PUSHBUTTON, IDC_UPDATE);
    add(L"BUTTON", L"About", WS_TABSTOP | BS_PUSHBUTTON, IDC_ABOUT);

    std::string clip = clipboardUrl();
    if (!clip.empty()) setText(IDC_URL, clip);
    appendLog(std::string(PMP3_PRODUCT_NAME " v") + PMP3_VERSION_FULL + " - " PMP3_BUILDER_NAME);
    appendLog("Tip: in Edge, middle-click a Rumble video to send it here automatically.");
}

void createJobControls() {
    add(L"STATIC", W(g.request.url), SS_ENDELLIPSIS | SS_NOPREFIX, IDC_TITLE);
    add(PROGRESS_CLASSW, L"", 0, IDC_PROGRESS);
    SendMessageW(ctl(IDC_PROGRESS), PBM_SETRANGE32, 0, 1000);
    add(L"STATIC", L"Starting...", SS_NOPREFIX, IDC_STATUS);
    add(L"BUTTON", L"Show log", WS_TABSTOP | BS_PUSHBUTTON, IDC_SHOWLOG);
    add(L"BUTTON", L"Play", WS_TABSTOP | BS_PUSHBUTTON, IDC_PLAY);
    add(L"BUTTON", L"Open folder", WS_TABSTOP | BS_PUSHBUTTON, IDC_OPENFOLDER);
    add(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, IDC_CANCEL);
    add(L"BUTTON", L"Close", WS_TABSTOP | BS_DEFPUSHBUTTON, IDC_CLOSE);
    for (int id : {IDC_SHOWLOG, IDC_PLAY, IDC_OPENFOLDER, IDC_CLOSE}) ShowWindow(ctl(id), SW_HIDE);
}

void stopAutoClose() {
    if (g.closeCountdown > 0) {
        KillTimer(g.hwnd, kAutoCloseTimer);
        g.closeCountdown = 0;
        onStatus(g.last);
        setText(IDC_STATUS, g.last.message + "   (kept open - close it when you are done)");
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.hwnd = hwnd;
            g.dpi = windowDpi(hwnd);
            makeFonts();
            if (g.jobMode)
                createJobControls();
            else
                createMainControls();
            makeFonts();
            layout();
            if (g.jobMode) startJob(g.request);
            return 0;
        }
        case WM_SIZE:
            layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto mmi = (MINMAXINFO*)lp;
            mmi->ptMinTrackSize.x = S(g.jobMode ? 420 : 620);
            mmi->ptMinTrackSize.y = S(g.jobMode ? 190 : 380);
            return 0;
        }
        case 0x02E0: {  // WM_DPICHANGED
            g.dpi = HIWORD(wp);
            makeFonts();
            auto r = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            switch (id) {
                case IDC_DOWNLOAD: onDownloadClicked(); break;
                case IDC_PASTE: {
                    std::string u = clipboardUrl();
                    if (u.empty())
                        SendMessageW(ctl(IDC_URL), WM_PASTE, 0, 0);
                    else
                        setText(IDC_URL, u);
                    break;
                }
                case IDC_BROWSE: browseFolder(); break;
                case IDC_OPENFOLDER:
                    if (g.jobMode && !g.last.output.empty())
                        openInFileManager(g.last.output, true);
                    else {
                        std::string f = g.jobMode ? g.request.outputDir : trim(getText(IDC_FOLDER));
                        std::error_code ec;
                        fs::create_directories(toPath(f), ec);
                        openInFileManager(f, false);
                    }
                    stopAutoClose();
                    break;
                case IDC_PLAY:
                    if (!g.last.output.empty())
                        ShellExecuteW(hwnd, L"open", W(g.last.output).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    stopAutoClose();
                    break;
                case IDC_CANCEL:
                    if (g.runner) g.runner->cancel();
                    setText(IDC_STATUS, "Cancelling...");
                    break;
                case IDC_CLOSE: DestroyWindow(hwnd); break;
                case IDC_SHOWLOG: {
                    fs::path log = logsDir() / toPath(g.request.id + ".log");
                    ShellExecuteW(hwnd, L"open", L"notepad.exe", log.wstring().c_str(), nullptr, SW_SHOWNORMAL);
                    stopAutoClose();
                    break;
                }
                case IDC_UPDATE: updateYtDlpAsync(); break;
                case IDC_ABOUT: showAbout(); break;
            }
            return 0;
        }
        // A click anywhere in the window (also on the text or the progress
        // bar, or on the title bar) stops the countdown so it stays open.
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            if (g.jobMode) stopAutoClose();
            return 0;
        case WM_PARENTNOTIFY:
            if (g.jobMode && (LOWORD(wp) == WM_LBUTTONDOWN || LOWORD(wp) == WM_RBUTTONDOWN)) stopAutoClose();
            return 0;
        case WM_NCLBUTTONDOWN:
            if (g.jobMode) stopAutoClose();
            break;  // keep normal title-bar behaviour (moving the window)
        case WM_TIMER:
            if (wp == kAutoCloseTimer) {
                if (--g.closeCountdown <= 0) {
                    KillTimer(hwnd, kAutoCloseTimer);
                    DestroyWindow(hwnd);
                } else {
                    setText(IDC_STATUS, g.last.message + "   (closing in " + std::to_string(g.closeCountdown) +
                                            "s - click anywhere in this window to keep it open)");
                }
            }
            return 0;
        case WM_APP_STATUS: {
            std::unique_ptr<JobStatus> s((JobStatus*)lp);
            if (g.running) onStatus(*s);
            return 0;
        }
        case WM_APP_LOG: {
            std::unique_ptr<std::string> s((std::string*)lp);
            appendLog(*s);
            return 0;
        }
        case WM_APP_FINISHED: {
            std::unique_ptr<JobStatus> s((JobStatus*)lp);
            onFinished(*s);
            return 0;
        }
        case WM_APP_TEXT: {
            std::unique_ptr<std::string> s((std::string*)lp);
            size_t nl = s->rfind('\n');
            if (nl > 0) appendLog(s->substr(0, nl));
            g.ytdlpVersionCached = s->substr(nl + 1);
            setText(IDC_VERSIONLINE, versionLine());
            EnableWindow(ctl(IDC_UPDATE), TRUE);
            return 0;
        }
        case WM_CLOSE:
            if (g.running) {
                int r = MessageBoxW(hwnd, L"A download is still running. Stop it and close?",
                                    TEXT(PMP3_PRODUCT_NAME), MB_YESNO | MB_ICONQUESTION);
                if (r != IDYES) return 0;
                if (g.runner) g.runner->cancel();
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (g.runner) g.runner->cancel();
            if (g.worker.joinable()) g.worker.join();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::string argValue(const std::vector<std::string>& a, const char* name) {
    for (size_t i = 1; i + 1 < a.size(); ++i)
        if (a[i] == name) return a[i + 1];
    return std::string();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(narrow(wargv[i]));
    LocalFree(wargv);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    // Keep the Edge link pointing at this copy of the program. This is what
    // makes "unzip the new version over the old one" just work.
    std::string report;
    registerNativeHost(report);

    std::string jobId = argValue(args, "--job");
    if (!jobId.empty()) {
        g.jobMode = true;
        Settings s = loadSettings();
        std::string url = argValue(args, "--url");
        std::string speed = argValue(args, "--speed"), bitrate = argValue(args, "--bitrate");
        if (!speed.empty()) s.speed = std::atof(speed.c_str());
        if (!bitrate.empty()) s.bitrateKbps = std::atoi(bitrate.c_str());
        std::string volume = argValue(args, "--volume");
        if (!volume.empty()) s.volume = std::atof(volume.c_str());
        normalizeSettings(s);
        g.request = requestFromSettings(s, url);
        g.request.id = jobId;
        g.request.source = "extension";
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = L"PodcastMP3Window";
    RegisterClassExW(&wc);

    HDC dc = GetDC(nullptr);
    int sysDpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    int w = MulDiv(g.jobMode ? 520 : 720, sysDpi, 96), h = MulDiv(g.jobMode ? 200 : 520, sysDpi, 96);
    std::wstring title = g.jobMode ? L"Podcast MP3"
                                   : W(std::string(PMP3_PRODUCT_NAME " v") + PMP3_VERSION_FULL + " - " PMP3_BUILDER_NAME);
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (g.jobMode) {
        // Bottom-right corner of the work area, like a notification.
        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        x = wa.right - w - MulDiv(16, sysDpi, 96);
        y = wa.bottom - h - MulDiv(16, sysDpi, 96);
    }
    HWND hwnd = CreateWindowExW(g.jobMode ? WS_EX_TOPMOST : 0, wc.lpszClassName, title.c_str(),
                                WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, g.jobMode ? SW_SHOWNOACTIVATE : show);
    UpdateWindow(hwnd);

    if (!g.jobMode) {
        // yt-dlp --version is slow on first start; fetch it in the background.
        std::thread([hwnd] {
            std::string v = ytDlpVersion();
            PostMessageW(hwnd, WM_APP_TEXT, 0, (LPARAM) new std::string("\n" + v));
        }).detach();
    }

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    CoUninitialize();
    return (int)m.wParam;
}
