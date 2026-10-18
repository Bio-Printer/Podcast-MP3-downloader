#include "job.h"

#include "nlohmann/json.hpp"
#include "childproc.h"
#include "version.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

namespace pmp3 {

using json = nlohmann::json;

const char* jobStateName(JobState s) {
    switch (s) {
        case JobState::Queued: return "queued";
        case JobState::Probing: return "probing";
        case JobState::Downloading: return "downloading";
        case JobState::Converting: return "converting";
        case JobState::Done: return "done";
        case JobState::Failed: return "failed";
        case JobState::Cancelled: return "cancelled";
    }
    return "unknown";
}

bool jobStateFinal(JobState s) {
    return s == JobState::Done || s == JobState::Failed || s == JobState::Cancelled;
}

static JobState stateFromName(const std::string& n) {
    for (int i = 0; i <= (int)JobState::Cancelled; ++i)
        if (n == jobStateName((JobState)i)) return (JobState)i;
    return JobState::Failed;
}

std::string jobStatusToJson(const JobStatus& s) {
    json j = {{"id", s.id},
              {"url", s.url},
              {"state", jobStateName(s.state)},
              {"percent", std::round(s.percent * 10) / 10},
              {"message", s.message},
              {"title", s.title},
              {"channel", s.channel},
              {"output", s.output},
              {"error", s.error},
              {"speed", s.speed},
              {"bitrateKbps", s.bitrateKbps},
              {"duration", s.duration},
              {"started", s.started},
              {"finished", s.finished},
              {"updated", s.updatedEpoch},
              {"version", s.version}};
    return j.dump(2);
}

bool jobStatusFromJson(const std::string& text, JobStatus& s) {
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return false;
    s.id = j.value("id", "");
    s.url = j.value("url", "");
    s.state = stateFromName(j.value("state", "failed"));
    s.percent = j.value("percent", 0.0);
    s.message = j.value("message", "");
    s.title = j.value("title", "");
    s.channel = j.value("channel", "");
    s.output = j.value("output", "");
    s.error = j.value("error", "");
    s.speed = j.value("speed", 1.5);
    s.bitrateKbps = j.value("bitrateKbps", 192);
    s.duration = j.value("duration", 0.0);
    s.started = j.value("started", "");
    s.finished = j.value("finished", "");
    s.updatedEpoch = j.value("updated", (int64_t)0);
    s.version = j.value("version", "");
    return !s.id.empty();
}

std::string newJobId() { return nowCompactString() + "-" + randomHex(6); }

JobRequest requestFromSettings(const Settings& s, const std::string& url) {
    JobRequest r;
    r.id = newJobId();
    r.url = trim(url);
    r.speed = s.speed;
    r.bitrateKbps = s.bitrateKbps;
    r.outputDir = s.outputDir;
    r.tempoEngine = s.tempoEngine;
    r.fileNameTemplate = s.fileNameTemplate;
    return r;
}

fs::path jobStatusFile(const std::string& id) { return jobsDir() / toPath(id + ".json"); }
fs::path jobCancelFile(const std::string& id) { return jobsDir() / toPath(id + ".cancel"); }

std::vector<JobStatus> listRecentJobs(size_t max) {
    std::vector<JobStatus> jobs;
    std::error_code ec;
    for (auto it = fs::directory_iterator(jobsDir(), ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".json") continue;
        std::string text;
        JobStatus s;
        if (readTextFile(it->path(), text) && jobStatusFromJson(text, s)) jobs.push_back(s);
    }
    // Job ids start with a timestamp, so sorting by id is sorting by age.
    std::sort(jobs.begin(), jobs.end(), [](const JobStatus& a, const JobStatus& b) { return a.id > b.id; });
    if (jobs.size() > max) jobs.resize(max);
    return jobs;
}

void pruneOldJobs(size_t keep) {
    std::error_code ec;
    for (const fs::path& dir : {jobsDir(), logsDir()}) {
        std::vector<fs::path> files;
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec)) files.push_back(it->path());
        std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { return a > b; });
        for (size_t i = keep; i < files.size(); ++i) fs::remove(files[i], ec);
    }
}

// ---------------------------------------------------------------------

JobRunner::JobRunner(JobRequest req, UpdateFn onUpdate, LogFn onLog)
    : req_(std::move(req)), onUpdate_(std::move(onUpdate)), onLog_(std::move(onLog)) {
    status_.id = req_.id;
    status_.url = req_.url;
    status_.speed = req_.speed;
    status_.bitrateKbps = req_.bitrateKbps;
    status_.version = PMP3_VERSION_FULL;
}

JobRunner::~JobRunner() = default;

void JobRunner::log(const std::string& line) {
    {
        std::error_code ec;
        fs::create_directories(logsDir(), ec);
        std::ofstream f(logsDir() / toPath(req_.id + ".log"), std::ios::app | std::ios::binary);
        f << line << "\n";
    }
    if (onLog_) onLog_(line);
}

void JobRunner::update() {
    status_.updatedEpoch = nowEpochSeconds();
    writeTextFileAtomic(jobStatusFile(req_.id), jobStatusToJson(status_));
    if (onUpdate_) onUpdate_(status_);
}

void JobRunner::fail(const std::string& error) {
    if (cancelled_) {
        status_.state = JobState::Cancelled;
        status_.message = "Cancelled";
        status_.error.clear();
    } else {
        status_.state = JobState::Failed;
        status_.error = error;
        status_.message = "Failed";
        log("ERROR: " + error);
    }
    status_.finished = nowLocalString();
    update();
}

void JobRunner::cancel() {
    cancelled_ = true;
    std::lock_guard<std::mutex> lock(procMutex_);
    if (current_) current_->kill();
}

static std::string describeArgs(const std::vector<std::string>& args) {
    std::string s;
    for (auto& a : args) {
        if (!s.empty()) s += ' ';
        s += a.find(' ') == std::string::npos ? a : "\"" + a + "\"";
    }
    return s;
}

// Last meaningful line of a tool's error output, for the user.
static std::string lastErrorLine(const std::string& text) {
    std::string best;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = trim(text.substr(start, end - start));
        if (!line.empty()) {
            if (line.find("ERROR") != std::string::npos || best.empty() ||
                best.find("ERROR") == std::string::npos)
                best = line;
        }
        start = end + 1;
    }
    if (best.size() > 400) best = best.substr(0, 400) + "...";
    return best;
}

bool JobRunner::runStep(const std::vector<std::string>& args,
                        const std::function<void(const std::string&)>& onLine, std::string& stderrOut,
                        int& exitCode) {
    log("> " + describeArgs(args));
    Process p;
    std::string err;
    {
        std::lock_guard<std::mutex> lock(procMutex_);
        if (cancelled_) return false;
        if (!p.start(args, err)) {
            stderrOut = err;
            exitCode = -1;
            return false;
        }
        current_ = &p;
    }
    std::string line;
    while (p.readLine(line))
        if (onLine) onLine(line);
    exitCode = p.wait();
    {
        std::lock_guard<std::mutex> lock(procMutex_);
        current_ = nullptr;
    }
    stderrOut = p.stderrText();
    if (!stderrOut.empty()) log(trim(stderrOut));
    log("< exit code " + std::to_string(exitCode));
    return exitCode == 0 && !cancelled_;
}

JobStatus JobRunner::run() {
    status_.started = nowLocalString();
    log(std::string(PMP3_PRODUCT_NAME " v") + PMP3_VERSION_FULL + " job " + req_.id);
    log("URL: " + req_.url + "  speed " + formatSpeed(req_.speed) + "x  " + std::to_string(req_.bitrateKbps) +
        " kbps  output " + req_.outputDir);

    // Lets the browser extension cancel us by creating <id>.cancel.
    std::atomic<bool> watching{true};
    std::thread watcher([this, &watching] {
        while (watching) {
            std::error_code ec;
            if (fs::exists(jobCancelFile(req_.id), ec)) {
                cancel();
                fs::remove(jobCancelFile(req_.id), ec);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
    });
    struct StopWatcher {
        std::atomic<bool>& w;
        std::thread& t;
        ~StopWatcher() {
            w = false;
            if (t.joinable()) t.join();
        }
    } stopWatcher{watching, watcher};

    if (!looksLikeUrl(req_.url)) {
        fail("That does not look like a web address: " + req_.url);
        return status_;
    }

    const std::string ytdlp = ytDlpPath();
    const std::string ffmpeg = ffmpegPath();
    std::error_code ec;
    fs::path tmp = tempRootDir() / toPath(req_.id);
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(dir, e);
        }
    } cleanup{tmp};

    // ---- 1. Ask yt-dlp what is on the page ---------------------------
    status_.state = JobState::Probing;
    status_.message = "Reading video page...";
    status_.percent = 1;
    update();

    std::string probeJson, errText;
    int code = 0;
    std::vector<std::string> probeArgs = {ytdlp, "--ignore-config", "--no-playlist", "--playlist-items", "1",
                                          "-J", "--no-progress", req_.url};
    bool ok = runStep(probeArgs, [&probeJson](const std::string& l) { probeJson += l + "\n"; }, errText, code);
    // Sites behind Cloudflare sometimes refuse plain HTTP clients; yt-dlp can
    // present itself like a regular Chrome browser instead.
    bool impersonate = false;
    if (!ok && !cancelled_ && code != -1 &&
        (errText.find("Cloudflare") != std::string::npos || errText.find("403") != std::string::npos)) {
        log("Site refused the request; retrying as a regular browser (--impersonate chrome)");
        impersonate = true;
        probeJson.clear();
        probeArgs.insert(probeArgs.begin() + 1, {"--impersonate", "chrome"});
        ok = runStep(probeArgs, [&probeJson](const std::string& l) { probeJson += l + "\n"; }, errText, code);
    }
    if (!ok) {
        if (code == -1)
            fail("Could not run yt-dlp (" + ytdlp + "): " + errText);
        else if (errText.find("403") != std::string::npos)
            fail("The site refused the request (HTTP 403, usually its bot protection). Open the video in Edge, "
                 "wait for it to play, then try again; if it keeps failing, use \"Update yt-dlp\". Details: " +
                 lastErrorLine(errText));
        else
            fail("Could not read the video page. " + lastErrorLine(errText));
        return status_;
    }
    VideoInfo info;
    std::string perr;
    if (!parseVideoInfo(probeJson, info, perr)) {
        fail(perr);
        return status_;
    }
    if (info.isLive) {
        fail("This is a live stream. It can be downloaded after the broadcast has ended.");
        return status_;
    }
    if (info.webpageUrl.empty()) info.webpageUrl = req_.url;
    status_.title = info.title;
    status_.channel = info.channel;
    status_.duration = info.duration;
    int fi = chooseFormat(info);
    if (fi < 0) {
        fail("No audio was found for this video.");
        return status_;
    }
    const FormatInfo& fmt = info.formats[(size_t)fi];
    log("Title: " + info.title + " | Channel: " + info.channel + " | Duration: " + formatDuration(info.duration));
    log("Chosen format " + fmt.id + " (" + fmt.ext + ", " +
        (fmt.audioOnly() ? std::string("audio only") : std::to_string(fmt.height) + "p video") +
        (fmt.abr > 0 ? ", audio " + std::to_string((int)fmt.abr) + " kbps" : std::string()) + ", " +
        fmt.protocol + ")");

    // ---- 2. Download -------------------------------------------------
    status_.state = JobState::Downloading;
    status_.message = "Starting download...";
    status_.percent = 3;
    update();

    auto lastWrite = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    std::vector<std::string> dlArgs = {
        ytdlp, "--ignore-config", "--no-playlist", "--playlist-items", "1", "--newline", "--color", "never",
        "-f", fmt.id, "--downloader", "m3u8:native", "--retries", "10",
        "--fragment-retries", "10", "--no-mtime", "--write-thumbnail", "--progress-template",
        "download:[PMP3] %(progress.downloaded_bytes)s %(progress.total_bytes)s "
        "%(progress.total_bytes_estimate)s %(progress.speed)s %(progress.eta)s",
        "-o", pathToUtf8(tmp / "media.%(ext)s"), "-o", "thumbnail:" + pathToUtf8(tmp / "cover.%(ext)s"),
        req_.url};
    if (toPath(ffmpeg).is_absolute()) dlArgs.insert(dlArgs.begin() + 1, {"--ffmpeg-location", ffmpeg});
    if (impersonate) dlArgs.insert(dlArgs.begin() + 1, {"--impersonate", "chrome"});
    ok = runStep(
        dlArgs,
        [&](const std::string& l) {
            DownloadProgress p;
            if (!parseDownloadProgress(l, p)) return;
            double frac = p.total > 0 ? std::min(1.0, p.downloaded / p.total) : 0;
            status_.percent = 3 + frac * 67;
            std::string m = "Downloading " + formatBytes(p.downloaded);
            if (p.total > 0) m += " of " + formatBytes(p.total);
            if (p.speed > 0) m += " at " + formatBytes(p.speed) + "/s";
            if (p.eta >= 0) m += ", " + formatDuration(p.eta) + " left";
            status_.message = m;
            auto now = std::chrono::steady_clock::now();
            if (now - lastWrite > std::chrono::milliseconds(300)) {
                lastWrite = now;
                update();
            }
        },
        errText, code);
    if (!ok) {
        fail("Download failed. " + lastErrorLine(errText));
        return status_;
    }

    fs::path media, cover;
    for (auto it = fs::directory_iterator(tmp, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::string name = pathToUtf8(it->path().filename());
        if (endsWith(name, ".part") || endsWith(name, ".ytdl") || name.find(".temp") != std::string::npos) continue;
        if (startsWith(name, "media.")) media = it->path();
        if (startsWith(name, "cover.")) cover = it->path();
    }
    if (media.empty()) {
        fail("yt-dlp finished but no media file was found.");
        return status_;
    }

    // ---- 3. Convert to MP3 ------------------------------------------
    status_.state = JobState::Converting;
    status_.percent = 70;
    status_.message = "Converting to MP3 at " + formatSpeed(req_.speed) + "x...";
    update();

    fs::path metaFile = tmp / "tags.txt";
    {
        std::ofstream mf(metaFile, std::ios::binary | std::ios::trunc);
        mf << buildFfmetadata(buildTags(info, req_.speed));
    }
    fs::path outTmp = tmp / "out.mp3";
    if (info.duration <= 0) {
        // Some pages do not report a duration; ask ffmpeg so progress works.
        std::string o, e;
        runCapture({ffmpeg, "-hide_banner", "-nostdin", "-i", pathToUtf8(media)}, o, e);
        double d = parseFfmpegDuration(e);
        if (d > 0) info.duration = d;
        status_.duration = info.duration;
    }
    const double outDuration = info.duration > 0 ? info.duration / req_.speed : 0;

    auto convert = [&](const std::string& engine, bool withCover) {
        std::vector<std::string> a = {ffmpeg, "-hide_banner", "-nostdin", "-y", "-loglevel", "error",
                                      "-i", pathToUtf8(media), "-f", "ffmetadata", "-i", pathToUtf8(metaFile)};
        if (withCover) {
            a.push_back("-i");
            a.push_back(pathToUtf8(cover));
        }
        a.insert(a.end(), {"-map", "0:a:0"});
        if (withCover) a.insert(a.end(), {"-map", "2:v:0"});
        a.insert(a.end(), {"-map_metadata", "1", "-map_chapters", "-1"});
        std::string filter = tempoFilter(req_.speed, engine);
        if (!filter.empty()) a.insert(a.end(), {"-af", filter});
        a.insert(a.end(), {"-c:a", "libmp3lame", "-b:a", std::to_string(req_.bitrateKbps) + "k", "-ar", "44100",
                           "-ac", "2"});
        if (withCover)
            a.insert(a.end(), {"-c:v", "mjpeg", "-q:v", "3", "-vf",
                               "scale='min(800,iw)':-2:flags=lanczos,format=yuvj420p", "-disposition:v:0",
                               "attached_pic", "-metadata:s:v:0", "title=Album cover", "-metadata:s:v:0",
                               "comment=Cover (front)"});
        a.insert(a.end(), {"-id3v2_version", "3", "-write_id3v1", "1", "-progress", "pipe:1", "-nostats",
                           pathToUtf8(outTmp)});
        return runStep(
            a,
            [&](const std::string& l) {
                double t = parseFfmpegOutTime(l);
                if (t < 0) return;
                if (outDuration > 0) status_.percent = 70 + std::min(1.0, t / outDuration) * 29;
                status_.message = "Converting to MP3 at " + formatSpeed(req_.speed) + "x: " + formatDuration(t) +
                                  (outDuration > 0 ? " of " + formatDuration(outDuration) : std::string());
                auto now = std::chrono::steady_clock::now();
                if (now - lastWrite > std::chrono::milliseconds(300)) {
                    lastWrite = now;
                    update();
                }
            },
            errText, code);
    };

    std::string engine = req_.tempoEngine;
    bool withCover = !cover.empty();
    ok = convert(engine, withCover);
    if (!ok && !cancelled_ && engine == "rubberband" && errText.find("ubberband") != std::string::npos) {
        log("rubberband filter not available, retrying with atempo");
        engine = "atempo";
        ok = convert(engine, withCover);
    }
    if (!ok && !cancelled_ && withCover) {
        log("Conversion with cover art failed, retrying without cover art");
        withCover = false;
        ok = convert(engine, withCover);
    }
    if (!ok) {
        fail("MP3 conversion failed. " + lastErrorLine(errText));
        return status_;
    }

    std::string id3err;
    if (!addId3Extras(outTmp, info.webpageUrl, buildTags(info, req_.speed).comment, id3err))
        log("Note: could not add source URL / comment frames: " + id3err);

    // ---- 4. Move into place -----------------------------------------
    fs::path outDir = toPath(req_.outputDir.empty() ? pathToUtf8(defaultOutputDir()) : req_.outputDir);
    fs::create_directories(outDir, ec);
    if (ec) {
        fail("Cannot create the output folder " + pathToUtf8(outDir) + ": " + ec.message());
        return status_;
    }
    fs::path finalPath = uniquePath(outDir / toPath(buildFileName(req_.fileNameTemplate, info, req_.speed) + ".mp3"));
    std::string mverr;
    if (!moveFile(outTmp, finalPath, mverr)) {
        fail("Cannot save " + pathToUtf8(finalPath) + ": " + mverr);
        return status_;
    }

    status_.state = JobState::Done;
    status_.percent = 100;
    status_.output = pathToUtf8(finalPath);
    auto size = fs::file_size(finalPath, ec);
    status_.message = "Saved " + formatBytes((double)size) +
                      (outDuration > 0 ? ", " + formatDuration(outDuration) + " at " + formatSpeed(req_.speed) + "x"
                                       : std::string());
    status_.finished = nowLocalString();
    log("Saved " + status_.output);
    update();
    return status_;
}

}  // namespace pmp3
