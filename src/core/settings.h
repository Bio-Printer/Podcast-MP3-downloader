// Podcast MP3 Downloader - user settings and well-known locations.
#pragma once

#include "util.h"

#include <string>

namespace pmp3 {

struct Settings {
    std::string outputDir;             // where finished MP3 files go
    double speed = 1.5;                // playback speed baked into the MP3
    int bitrateKbps = 192;             // MP3 bitrate (stereo, 44.1 kHz)
    double volume = 1.25;              // loudness multiplier (1.0 = unchanged)
    std::string tempoEngine = "rubberband";  // "rubberband" or "atempo"
    // Placeholders: {title} {channel} {date} {speed} {speedtag} {id}
    std::string fileNameTemplate = "{date} {channel} - {title}{speedtag}";
    int autoCloseSeconds = 30;         // job window closes this long after success (0 = stay open)
};

// %APPDATA%\PodcastMP3 on Windows, ~/.config/podcastmp3 on Linux.
fs::path settingsDir();
// %LOCALAPPDATA%\PodcastMP3 on Windows, ~/.local/share/podcastmp3 on Linux.
fs::path dataDir();
fs::path jobsDir();
fs::path tempRootDir();
fs::path logsDir();

// C:\Tmp\Audio on Windows, ~/Tmp/Audio elsewhere.
fs::path defaultOutputDir();

Settings loadSettings();
bool saveSettings(const Settings& s);
// Clamp values into supported ranges.
void normalizeSettings(Settings& s);

std::string settingsToJson(const Settings& s);
// Applies only the fields present in the JSON text.
void applySettingsJson(Settings& s, const std::string& json);

// Supported choices (also used to fill the UI lists).
extern const double kSpeedChoices[];
extern const int kSpeedChoiceCount;
extern const int kBitrateChoices[];
extern const int kBitrateChoiceCount;
extern const double kVolumeChoices[];
extern const int kVolumeChoiceCount;
const double kVolumeMin = 0.5, kVolumeMax = 3.0;

// Locations of the bundled tools: <exe dir>\tools\yt-dlp.exe etc.,
// falling back to the plain program name (searched on PATH).
std::string ytDlpPath();
std::string ffmpegPath();
// Deno: the JavaScript runtime yt-dlp needs for YouTube. "" if not bundled/found.
std::string denoPath();

// Folder holding the unpacked Edge extension next to the program.
fs::path extensionDir();

}  // namespace pmp3
