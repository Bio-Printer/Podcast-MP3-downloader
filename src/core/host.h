// Podcast MP3 Downloader - native messaging host (talks to the Edge extension)
// and registration of that host with the browser.
#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace pmp3 {

// Chrome/Edge native messaging framing: 4-byte little-endian length + UTF-8 JSON.
bool readNativeMessage(std::istream& in, std::string& msg);
void writeNativeMessage(std::ostream& out, const std::string& msg);
std::string encodeNativeMessage(const std::string& msg);

// Converts the extension's cookie list (JSON array of chrome.cookies objects)
// to a Netscape cookies.txt file as yt-dlp reads it. Sets count.
std::string netscapeCookiesFromJson(const std::string& jsonArray, int& count);

// Handles one request from the extension and returns the JSON reply.
std::string handleHostRequest(const std::string& requestJson);

// Reads requests from stdin until it closes (the browser end of the pipe).
int runNativeHost();

// Writes <exe dir>/com.bioprinter.podcastmp3.json and registers it for
// Edge and Chrome for the current user. Safe to call repeatedly.
bool registerNativeHost(std::string& report);
bool unregisterNativeHost(std::string& report);
// True if the registration points to this program.
bool nativeHostRegistered();

// Version info shown in the extension popup / about box.
std::string ytDlpVersion();      // cached by file time; "" if not found
std::string ffmpegVersion();     // first line of "ffmpeg -version"
std::string extensionVersionOnDisk();  // "version" + "version_name" of extension/manifest.json

// Starts a job in its own process (GUI window on Windows).
bool launchJobProcess(const std::string& jobId, const std::string& url, double speed, int bitrate, double volume,
                      std::string& err);

// Runs "yt-dlp -U" and returns its output.
std::string updateYtDlp(int& exitCode);

// Opens a folder in the file manager, optionally selecting a file.
bool openInFileManager(const std::string& path, bool selectFile);

}  // namespace pmp3
