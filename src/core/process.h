// Podcast MP3 Downloader - child process control (yt-dlp, ffmpeg).
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pmp3 {

// Runs a child program with stdin connected to the null device, stdout
// readable line by line and stderr collected in the background. The whole
// process tree (yt-dlp.exe spawns a second process) can be killed.
class Process {
public:
    Process();
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // args[0] is the program path. Returns false and sets err on failure.
    bool start(const std::vector<std::string>& args, std::string& err);

    // Blocks until a complete line (without the line ending) is available.
    // Carriage returns also end a line. Returns false at end of output.
    bool readLine(std::string& line);

    // Waits for the process to exit and returns its exit code.
    int wait();

    // Kills the process and all its children. Safe to call from any thread.
    void kill();

    // The last ~64 KB written to stderr.
    std::string stderrText();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Convenience: run to completion, capture all stdout.
int runCapture(const std::vector<std::string>& args, std::string& out, std::string& err);

// Launch a program that keeps running after we exit (no handles shared).
bool spawnDetached(const std::vector<std::string>& args, std::string& err, bool hideWindow = false);

#ifdef _WIN32
// Quote one argument the way CommandLineToArgvW / the MSVC runtime expect.
std::string quoteWindowsArg(const std::string& arg);
#endif

}  // namespace pmp3
