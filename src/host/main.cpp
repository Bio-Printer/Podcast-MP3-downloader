// podcastmp3-host: command line tool and Edge native-messaging helper.
//
//   podcastmp3-host download <url> [--speed 1.5] [--bitrate 192] [--out DIR] [--engine rubberband|atempo]
//   podcastmp3-host info <url>
//   podcastmp3-host register | unregister
//   podcastmp3-host --version
//
// When Edge starts it, the first argument is "chrome-extension://<id>/" and
// it speaks the native messaging protocol on stdin/stdout.

#include "../core/host.h"
#include "../core/job.h"
#include "../core/media.h"
#include "../core/childproc.h"
#include "../core/settings.h"
#include "../core/version.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace pmp3;

static void out(const std::string& s) {
#ifdef _WIN32
    // Print UTF-8 correctly in a Windows console.
    std::wstring w = widen(s);
    DWORD n;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleMode(h, &n)) {
        WriteConsoleW(h, w.c_str(), (DWORD)w.size(), &n, nullptr);
        return;
    }
#endif
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

static void usage() {
    out(std::string(PMP3_PRODUCT_NAME " helper v") + PMP3_VERSION_FULL +
        "\n\n"
        "Usage:\n"
        "  podcastmp3-host download <url> [--speed 1.5] [--bitrate 192] [--out DIR] [--engine rubberband|atempo]\n"
        "  podcastmp3-host info <url>          show what would be downloaded\n"
        "  podcastmp3-host register            connect the helper to Edge/Chrome\n"
        "  podcastmp3-host unregister\n"
        "  podcastmp3-host update-ytdlp        update the bundled yt-dlp\n"
        "  podcastmp3-host --version\n");
}

struct Options {
    std::string url, out, engine, jobId;
    double speed = 0;
    int bitrate = 0;
};

static bool parseOptions(const std::vector<std::string>& a, size_t start, Options& o) {
    for (size_t i = start; i < a.size(); ++i) {
        auto next = [&](std::string& v) {
            if (i + 1 >= a.size()) return false;
            v = a[++i];
            return true;
        };
        std::string v;
        if (a[i] == "--speed" && next(v))
            o.speed = std::atof(v.c_str());
        else if (a[i] == "--bitrate" && next(v))
            o.bitrate = std::atoi(v.c_str());
        else if (a[i] == "--out" && next(v))
            o.out = v;
        else if (a[i] == "--engine" && next(v))
            o.engine = v;
        else if (a[i] == "--job" && next(v))
            o.jobId = v;
        else if (a[i] == "--url" && next(v))
            o.url = v;
        else if (!a[i].empty() && a[i][0] != '-' && o.url.empty())
            o.url = a[i];
        else {
            out("Unknown option: " + a[i] + "\n");
            return false;
        }
    }
    return true;
}

static JobRequest makeRequest(const Options& o) {
    Settings s = loadSettings();
    if (o.speed > 0) s.speed = o.speed;
    if (o.bitrate > 0) s.bitrateKbps = o.bitrate;
    if (!o.out.empty()) s.outputDir = o.out;
    if (!o.engine.empty()) s.tempoEngine = o.engine;
    normalizeSettings(s);
    JobRequest r = requestFromSettings(s, o.url);
    if (!o.jobId.empty()) r.id = o.jobId;
    return r;
}

static int cmdDownload(const Options& o, bool quiet) {
    JobRequest r = makeRequest(o);
    r.source = quiet ? "extension" : "cli";
    std::string lastMsg;
    JobRunner runner(
        r,
        [&](const JobStatus& s) {
            if (quiet) return;
            std::string m = "[" + std::to_string((int)s.percent) + "%] " + s.message;
            if (m != lastMsg) out("\r" + m + std::string(m.size() < lastMsg.size() ? lastMsg.size() - m.size() : 0, ' '));
            lastMsg = m;
        },
        nullptr);
    JobStatus st = runner.run();
    if (!quiet) {
        out("\n");
        if (st.state == JobState::Done)
            out("Saved: " + st.output + "\n");
        else
            out("Failed: " + st.error + "\n");
    }
    return st.state == JobState::Done ? 0 : 1;
}

static int cmdInfo(const Options& o) {
    std::string json, err;
    int code = runCapture({ytDlpPath(), "--ignore-config", "--no-playlist", "--playlist-items", "1", "-J", o.url},
                          json, err);
    if (code != 0) {
        out("yt-dlp failed:\n" + err + "\n");
        return 1;
    }
    VideoInfo info;
    std::string perr;
    if (!parseVideoInfo(json, info, perr)) {
        out(perr + "\n");
        return 1;
    }
    out("Title:     " + info.title + "\nChannel:   " + info.channel + "\nDate:      " + prettyDate(info.uploadDate) +
        "\nDuration:  " + formatDuration(info.duration) + "\nPage:      " + info.webpageUrl +
        "\nThumbnail: " + info.thumbnail + "\nFormats:\n");
    int chosen = chooseFormat(info);
    for (int i = 0; i < (int)info.formats.size(); ++i) {
        const auto& f = info.formats[(size_t)i];
        char line[512];
        std::snprintf(line, sizeof(line), "  %s %-22s %-5s %-10s a=%-10s v=%-14s abr=%-6.0f h=%-5d\n",
                      i == chosen ? "=>" : "  ", f.id.c_str(), f.ext.c_str(), f.protocol.c_str(),
                      f.acodec.c_str(), f.vcodec.c_str(), f.abr, f.height);
        out(line);
    }
    Settings s = loadSettings();
    out("File name: " + buildFileName(s.fileNameTemplate, info, s.speed) + ".mp3\n");
    return 0;
}

static int realMain(const std::vector<std::string>& a) {
    if (a.size() >= 2 && startsWith(a[1], "chrome-extension://")) return runNativeHost();
    if (a.size() < 2 || a[1] == "--help" || a[1] == "-h" || a[1] == "help") {
        usage();
        return a.size() < 2 ? 1 : 0;
    }
    const std::string& cmd = a[1];
    if (cmd == "--version" || cmd == "version") {
        out(std::string(PMP3_PRODUCT_NAME " v") + PMP3_VERSION_FULL + "\n");
        out("yt-dlp: " + ytDlpPath() + " " + ytDlpVersion() + "\n");
        out("ffmpeg: " + ffmpegPath() + " " + ffmpegVersion() + "\n");
        out("extension on disk: " + extensionVersionOnDisk() + "\n");
        return 0;
    }
    if (cmd == "register" || cmd == "unregister") {
        std::string report;
        bool ok = cmd == "register" ? registerNativeHost(report) : unregisterNativeHost(report);
        out(report + "\n");
        return ok ? 0 : 1;
    }
    if (cmd == "update-ytdlp") {
        int code = 0;
        out(updateYtDlp(code) + "\n");
        return code;
    }
    if (cmd == "native-host") return runNativeHost();
    Options o;
    if (!parseOptions(a, 2, o)) return 2;
    if (cmd == "download" || cmd == "run-job") {
        if (o.url.empty()) {
            usage();
            return 2;
        }
        return cmdDownload(o, cmd == "run-job");
    }
    if (cmd == "info" && !o.url.empty()) return cmdInfo(o);
    usage();
    return 2;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> a;
    for (int i = 0; i < argc; ++i) a.push_back(narrow(argv[i]));
    return realMain(a);
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> a(argv, argv + argc);
    return realMain(a);
}
#endif
