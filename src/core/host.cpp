#include "host.h"

#include "job.h"
#include "nlohmann/json.hpp"
#include "childproc.h"
#include "settings.h"
#include "version.h"

#include <cstdio>
#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <cstdlib>
#endif

namespace pmp3 {

using json = nlohmann::json;

// ---- framing ----------------------------------------------------------

bool readNativeMessage(std::istream& in, std::string& msg) {
    unsigned char len[4];
    if (!in.read((char*)len, 4)) return false;
    uint32_t n = (uint32_t)len[0] | ((uint32_t)len[1] << 8) | ((uint32_t)len[2] << 16) | ((uint32_t)len[3] << 24);
    if (n > 64u * 1024u * 1024u) return false;
    msg.resize(n);
    if (n && !in.read(&msg[0], n)) return false;
    return true;
}

std::string encodeNativeMessage(const std::string& msg) {
    uint32_t n = (uint32_t)msg.size();
    std::string out;
    out.push_back((char)(n & 0xff));
    out.push_back((char)((n >> 8) & 0xff));
    out.push_back((char)((n >> 16) & 0xff));
    out.push_back((char)((n >> 24) & 0xff));
    out += msg;
    return out;
}

void writeNativeMessage(std::ostream& out, const std::string& msg) {
    std::string framed = encodeNativeMessage(msg);
    out.write(framed.data(), (std::streamsize)framed.size());
    out.flush();
}

// ---- version info -------------------------------------------------------

std::string ytDlpVersion() {
    std::string exe = ytDlpPath();
    std::error_code ec;
    fs::path p = toPath(exe);
    std::string stamp;
    if (fs::exists(p, ec)) {
        auto t = fs::last_write_time(p, ec);
        stamp = std::to_string((long long)t.time_since_epoch().count()) + ":" +
                std::to_string((unsigned long long)fs::file_size(p, ec));
    }
    // yt-dlp.exe takes a second or two to start, so cache the answer.
    fs::path cacheFile = dataDir() / "yt-dlp-version.json";
    std::string text;
    if (!stamp.empty() && readTextFile(cacheFile, text)) {
        json c = json::parse(text, nullptr, false);
        if (c.is_object() && c.value("stamp", "") == stamp && c.value("path", "") == exe)
            return c.value("version", "");
    }
    std::string out, err;
    if (runCapture({exe, "--version"}, out, err) != 0) return std::string();
    std::string v = trim(out);
    if (!stamp.empty()) writeTextFileAtomic(cacheFile, json{{"stamp", stamp}, {"path", exe}, {"version", v}}.dump());
    return v;
}

std::string ffmpegVersion() {
    std::string out, err;
    if (runCapture({ffmpegPath(), "-hide_banner", "-version"}, out, err) != 0) return std::string();
    std::string first = out.substr(0, out.find('\n'));
    // "ffmpeg version N-12345-gabc Copyright ..." -> "N-12345-gabc"
    const std::string key = "ffmpeg version ";
    if (startsWith(first, key)) first = first.substr(key.size());
    return trim(first.substr(0, first.find(' ')));
}

std::string extensionVersionOnDisk() {
    std::string text;
    if (!readTextFile(extensionDir() / "manifest.json", text)) return std::string();
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return std::string();
    return j.value("version_name", j.value("version", ""));
}

// ---- launching ---------------------------------------------------------

bool launchJobProcess(const std::string& jobId, const std::string& url, double speed, int bitrate,
                      std::string& err) {
    fs::path dir = executableDir();
    std::error_code ec;
    std::vector<std::string> args;
#ifdef _WIN32
    fs::path gui = dir / L"PodcastMP3.exe";
    if (fs::exists(gui, ec)) {
        args = {pathToUtf8(gui), "--job", jobId, "--url", url, "--speed", formatSpeed(speed), "--bitrate",
                std::to_string(bitrate)};
        return spawnDetached(args, err, false);
    }
    fs::path host = dir / L"podcastmp3-host.exe";
#else
    fs::path host = dir / "podcastmp3-host";
#endif
    args = {pathToUtf8(host), "run-job", "--job", jobId, "--url", url, "--speed", formatSpeed(speed), "--bitrate",
            std::to_string(bitrate)};
    return spawnDetached(args, err, true);
}

std::string updateYtDlp(int& exitCode) {
    std::string out, err;
    exitCode = runCapture({ytDlpPath(), "-U"}, out, err);
    std::error_code ec;
    fs::remove(dataDir() / "yt-dlp-version.json", ec);
    return trim(out + "\n" + err);
}

bool openInFileManager(const std::string& path, bool selectFile) {
#ifdef _WIN32
    std::wstring w = toPath(path).make_preferred().wstring();
    if (selectFile) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(w.c_str());
        if (pidl) {
            HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            ILFree(pidl);
            if (SUCCEEDED(hr)) return true;
        }
        w = toPath(path).parent_path().wstring();
    }
    return (INT_PTR)ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
#else
    std::string target = selectFile ? pathToUtf8(toPath(path).parent_path()) : path;
    std::string err;
    return spawnDetached({"xdg-open", target}, err, true);
#endif
}

// ---- registration ------------------------------------------------------

static fs::path hostExePath() {
#ifdef _WIN32
    return executableDir() / L"podcastmp3-host.exe";
#else
    return executableDir() / "podcastmp3-host";
#endif
}

static std::string hostManifestJson() {
    json j = {{"name", PMP3_HOST_NAME},
              {"description", PMP3_PRODUCT_NAME " helper"},
              {"path", pathToUtf8(hostExePath())},
              {"type", "stdio"},
              {"allowed_origins", {std::string("chrome-extension://") + PMP3_EXTENSION_ID + "/"}}};
    return j.dump(2);
}

#ifdef _WIN32
static fs::path hostManifestPath() { return executableDir() / (std::string(PMP3_HOST_NAME) + ".json"); }

static const wchar_t* kRegKeys[] = {
    L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\" TEXT(PMP3_HOST_NAME),
    L"Software\\Google\\Chrome\\NativeMessagingHosts\\" TEXT(PMP3_HOST_NAME),
};

static std::wstring readRegDefault(const wchar_t* key) {
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &h) != ERROR_SUCCESS) return std::wstring();
    wchar_t buf[4096];
    DWORD size = sizeof(buf), type = 0;
    std::wstring v;
    if (RegQueryValueExW(h, nullptr, nullptr, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS && type == REG_SZ)
        v.assign(buf, wcsnlen(buf, size / sizeof(wchar_t)));
    RegCloseKey(h);
    return v;
}
#endif

bool registerNativeHost(std::string& report) {
    std::string manifest = hostManifestJson();
#ifdef _WIN32
    fs::path mpath = hostManifestPath();
    std::string existing;
    bool ok = true;
    if (!readTextFile(mpath, existing) || existing != manifest) {
        if (!writeTextFileAtomic(mpath, manifest)) {
            // Install folder not writable (e.g. Program Files): use AppData.
            mpath = dataDir() / (std::string(PMP3_HOST_NAME) + ".json");
            if (!writeTextFileAtomic(mpath, manifest)) {
                report = "Could not write " + pathToUtf8(mpath);
                return false;
            }
        }
    }
    std::wstring value = mpath.wstring();
    for (auto key : kRegKeys) {
        if (readRegDefault(key) == value) continue;
        HKEY h;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) !=
            ERROR_SUCCESS) {
            ok = false;
            continue;
        }
        if (RegSetValueExW(h, nullptr, 0, REG_SZ, (const BYTE*)value.c_str(),
                           (DWORD)((value.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS)
            ok = false;
        RegCloseKey(h);
    }
    report = ok ? "Registered helper for Edge and Chrome: " + pathToUtf8(mpath)
                : "Registration in the Windows registry failed.";
    return ok;
#else
    std::vector<fs::path> dirs;
    const char* home = std::getenv("HOME");
    fs::path h = home ? fs::path(home) : fs::path("/tmp");
    for (const char* b : {"microsoft-edge", "google-chrome", "chromium"})
        dirs.push_back(h / ".config" / b / "NativeMessagingHosts");
    if (const char* extra = std::getenv("PMP3_EXTRA_NMH_DIR")) dirs.push_back(fs::path(extra));
    bool ok = true;
    report.clear();
    for (auto& d : dirs) {
        fs::path p = d / (std::string(PMP3_HOST_NAME) + ".json");
        if (writeTextFileAtomic(p, manifest))
            report += "Wrote " + p.string() + "\n";
        else
            ok = false;
    }
    return ok;
#endif
}

bool unregisterNativeHost(std::string& report) {
#ifdef _WIN32
    for (auto key : kRegKeys) RegDeleteKeyW(HKEY_CURRENT_USER, key);
    report = "Removed the helper registration for Edge and Chrome.";
    return true;
#else
    const char* home = std::getenv("HOME");
    fs::path h = home ? fs::path(home) : fs::path("/tmp");
    std::error_code ec;
    for (const char* b : {"microsoft-edge", "google-chrome", "chromium"})
        fs::remove(h / ".config" / b / "NativeMessagingHosts" / (std::string(PMP3_HOST_NAME) + ".json"), ec);
    report = "Removed native messaging manifests.";
    return true;
#endif
}

bool nativeHostRegistered() {
#ifdef _WIN32
    std::wstring v = readRegDefault(kRegKeys[0]);
    if (v.empty()) return false;
    std::string text;
    if (!readTextFile(fs::path(v), text)) return false;
    json j = json::parse(text, nullptr, false);
    return j.is_object() && toPath(j.value("path", "")) == hostExePath();
#else
    const char* home = std::getenv("HOME");
    std::string text;
    return home && readTextFile(fs::path(home) / ".config/microsoft-edge/NativeMessagingHosts" /
                                    (std::string(PMP3_HOST_NAME) + ".json"),
                                text);
#endif
}

// ---- request handling --------------------------------------------------

static json jobToJson(const JobStatus& s) { return json::parse(jobStatusToJson(s)); }

std::string handleHostRequest(const std::string& requestJson) {
    json req = json::parse(requestJson, nullptr, false);
    json reply = {{"ok", false}};
    if (!req.is_object()) {
        reply["error"] = "bad request";
        return reply.dump();
    }
    std::string cmd = req.value("cmd", "");
    reply["replyTo"] = cmd;
    if (req.contains("requestId")) reply["requestId"] = req["requestId"];
    try {
        if (cmd == "ping") {
            Settings s = loadSettings();
            reply["ok"] = true;
            reply["hostVersion"] = PMP3_VERSION;
            reply["hostBuild"] = PMP3_BUILD;
            reply["hostVersionFull"] = PMP3_VERSION_FULL;
            reply["extensionOnDisk"] = extensionVersionOnDisk();
            reply["extensionDir"] = pathToUtf8(extensionDir());
            reply["installDir"] = pathToUtf8(executableDir());
            reply["settings"] = json::parse(settingsToJson(s));
            if (req.value("full", false)) {
                reply["ytdlp"] = ytDlpVersion();
                reply["ffmpeg"] = ffmpegVersion();
            }
        } else if (cmd == "start") {
            std::string url = trim(req.value("url", ""));
            if (!looksLikeUrl(url)) {
                reply["error"] = "Not a web address: " + url;
            } else {
                Settings s = loadSettings();
                double speed = req.contains("speed") && req["speed"].is_number() ? req["speed"].get<double>() : s.speed;
                int bitrate = req.contains("bitrateKbps") && req["bitrateKbps"].is_number()
                                  ? req["bitrateKbps"].get<int>()
                                  : s.bitrateKbps;
                Settings check = s;
                check.speed = speed;
                check.bitrateKbps = bitrate;
                normalizeSettings(check);
                JobStatus st;
                st.id = newJobId();
                st.url = url;
                st.speed = check.speed;
                st.bitrateKbps = check.bitrateKbps;
                st.state = JobState::Queued;
                st.message = "Starting...";
                st.title = req.value("title", "");
                st.started = nowLocalString();
                st.updatedEpoch = nowEpochSeconds();
                st.version = PMP3_VERSION_FULL;
                writeTextFileAtomic(jobStatusFile(st.id), jobStatusToJson(st));
                pruneOldJobs(60);
                std::string err;
                if (launchJobProcess(st.id, url, check.speed, check.bitrateKbps, err)) {
                    reply["ok"] = true;
                    reply["jobId"] = st.id;
                    reply["job"] = jobToJson(st);
                } else {
                    st.state = JobState::Failed;
                    st.error = err;
                    writeTextFileAtomic(jobStatusFile(st.id), jobStatusToJson(st));
                    reply["error"] = err;
                }
            }
        } else if (cmd == "jobs") {
            json arr = json::array();
            for (auto& j : listRecentJobs((size_t)req.value("max", 10))) arr.push_back(jobToJson(j));
            reply["ok"] = true;
            reply["jobs"] = arr;
        } else if (cmd == "cancel") {
            std::string id = req.value("jobId", "");
            if (id.empty() || id.find_first_of("/\\.") != std::string::npos) {
                reply["error"] = "bad job id";
            } else {
                writeTextFileAtomic(jobCancelFile(id), "cancel");
                reply["ok"] = true;
            }
        } else if (cmd == "open") {
            // Open the folder of a finished job (selecting its file), or the output folder.
            std::string id = req.value("jobId", "");
            std::string target;
            bool select = false;
            if (!id.empty()) {
                std::string text;
                JobStatus st;
                if (readTextFile(jobStatusFile(id), text) && jobStatusFromJson(text, st) && !st.output.empty()) {
                    target = st.output;
                    select = true;
                }
            }
            if (target.empty()) {
                Settings s = loadSettings();
                std::error_code ec;
                fs::create_directories(toPath(s.outputDir), ec);
                target = s.outputDir;
            }
            reply["ok"] = openInFileManager(target, select);
        } else if (cmd == "getSettings") {
            reply["ok"] = true;
            reply["settings"] = json::parse(settingsToJson(loadSettings()));
        } else if (cmd == "setSettings") {
            Settings s = loadSettings();
            if (req.contains("settings")) applySettingsJson(s, req["settings"].dump());
            normalizeSettings(s);
            reply["ok"] = saveSettings(s);
            reply["settings"] = json::parse(settingsToJson(s));
        } else if (cmd == "openApp") {
            std::string err;
#ifdef _WIN32
            reply["ok"] = spawnDetached({pathToUtf8(executableDir() / L"PodcastMP3.exe")}, err, false);
#else
            reply["ok"] = false;
            err = "The desktop window is only available on Windows.";
#endif
            if (!err.empty()) reply["error"] = err;
        } else if (cmd == "updateYtdlp") {
            int code = 0;
            reply["output"] = updateYtDlp(code);
            reply["ok"] = code == 0;
            reply["ytdlp"] = ytDlpVersion();
        } else {
            reply["error"] = "unknown command: " + cmd;
        }
    } catch (const std::exception& e) {
        reply["ok"] = false;
        reply["error"] = std::string("internal error: ") + e.what();
    }
    return reply.dump();
}

int runNativeHost() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::ios::sync_with_stdio(false);
    std::string msg;
    while (readNativeMessage(std::cin, msg)) writeNativeMessage(std::cout, handleHostRequest(msg));
    return 0;
}

}  // namespace pmp3
