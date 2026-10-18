// Podcast MP3 Downloader - one "page URL -> MP3 file" job.
#pragma once

#include "media.h"
#include "settings.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace pmp3 {

class Process;

enum class JobState { Queued, Probing, Downloading, Converting, Done, Failed, Cancelled };
const char* jobStateName(JobState s);
bool jobStateFinal(JobState s);

struct JobRequest {
    std::string id;       // unique job id (also the status file name)
    std::string url;      // page URL as given by the user
    double speed = 1.5;
    int bitrateKbps = 192;
    double volume = 1.25;
    std::string outputDir;
    std::string tempoEngine = "rubberband";
    std::string fileNameTemplate = "{date} {channel} - {title}{speedtag}";
    std::string source = "app";  // "app", "extension", "cli"
};

struct JobStatus {
    std::string id;
    std::string url;
    JobState state = JobState::Queued;
    double percent = 0;    // overall 0..100
    std::string message;   // short human text: "Downloading 12.3 MB of 40 MB"
    std::string title;
    std::string channel;
    std::string output;    // final MP3 path when done
    std::string error;
    double speed = 1.5;
    int bitrateKbps = 192;
    double volume = 1.25;
    double duration = 0;   // source duration (seconds)
    std::string started;
    std::string finished;
    int64_t updatedEpoch = 0;
    std::string version;   // program version that ran the job
};

std::string jobStatusToJson(const JobStatus& s);
bool jobStatusFromJson(const std::string& text, JobStatus& s);

std::string newJobId();
JobRequest requestFromSettings(const Settings& s, const std::string& url);

// Status files live in jobsDir()/<id>.json; a <id>.cancel file asks the
// running job to stop (written by the native host for the extension).
fs::path jobStatusFile(const std::string& id);
fs::path jobCancelFile(const std::string& id);
std::vector<JobStatus> listRecentJobs(size_t max);
void pruneOldJobs(size_t keep);

class JobRunner {
public:
    using UpdateFn = std::function<void(const JobStatus&)>;
    using LogFn = std::function<void(const std::string&)>;

    JobRunner(JobRequest req, UpdateFn onUpdate, LogFn onLog);
    ~JobRunner();

    // Runs the whole pipeline on the calling thread. Returns final status.
    JobStatus run();
    // Thread safe; stops the current step as soon as possible.
    void cancel();

private:
    bool runStep(const std::vector<std::string>& args, const std::function<void(const std::string&)>& onLine,
                 std::string& stderrOut, int& exitCode);
    void update();
    void fail(const std::string& error);
    void log(const std::string& line);

    JobRequest req_;
    UpdateFn onUpdate_;
    LogFn onLog_;
    JobStatus status_;
    std::atomic<bool> cancelled_{false};
    std::mutex procMutex_;
    Process* current_ = nullptr;
};

}  // namespace pmp3
