#include "settings.h"

#include "nlohmann/json.hpp"

#include <cmath>
#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#endif

namespace pmp3 {

using json = nlohmann::json;

const double kSpeedChoices[] = {1.0, 1.25, 1.5, 1.75, 2.0, 2.5};
const int kSpeedChoiceCount = (int)(sizeof(kSpeedChoices) / sizeof(kSpeedChoices[0]));
const int kBitrateChoices[] = {128, 160, 192};
const int kBitrateChoiceCount = (int)(sizeof(kBitrateChoices) / sizeof(kBitrateChoices[0]));
const double kVolumeChoices[] = {1.0, 1.1, 1.25, 1.5, 1.75, 2.0};
const int kVolumeChoiceCount = (int)(sizeof(kVolumeChoices) / sizeof(kVolumeChoices[0]));

#ifdef _WIN32
static fs::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) result = fs::path(p);
    if (p) CoTaskMemFree(p);
    return result;
}
#else
static fs::path homeDir() {
    const char* h = std::getenv("HOME");
    return fs::path(h && *h ? h : "/tmp");
}
static fs::path xdgDir(const char* var, const char* fallback) {
    const char* v = std::getenv(var);
    if (v && *v) return fs::path(v);
    return homeDir() / fallback;
}
#endif

fs::path settingsDir() {
#ifdef _WIN32
    return knownFolder(FOLDERID_RoamingAppData) / L"PodcastMP3";
#else
    return xdgDir("XDG_CONFIG_HOME", ".config") / "podcastmp3";
#endif
}

fs::path dataDir() {
#ifdef _WIN32
    return knownFolder(FOLDERID_LocalAppData) / L"PodcastMP3";
#else
    return xdgDir("XDG_DATA_HOME", ".local/share") / "podcastmp3";
#endif
}

fs::path jobsDir() { return dataDir() / "jobs"; }
fs::path tempRootDir() { return dataDir() / "temp"; }
fs::path logsDir() { return dataDir() / "logs"; }

fs::path defaultOutputDir() {
#ifdef _WIN32
    return fs::path(L"C:\\Tmp\\Audio");
#else
    return homeDir() / "Tmp" / "Audio";
#endif
}

// Defaults used by 0.1.0; settings still holding them move to the new ones.
static fs::path oldDefaultOutputDir() {
#ifdef _WIN32
    fs::path music = knownFolder(FOLDERID_Music);
    if (music.empty()) music = knownFolder(FOLDERID_Profile) / L"Music";
    return music / L"Podcasts";
#else
    return homeDir() / "Music" / "Podcasts";
#endif
}

void normalizeSettings(Settings& s) {
    if (!(s.speed >= 0.5 && s.speed <= 4.0)) s.speed = 1.5;
    s.speed = std::round(s.speed * 100.0) / 100.0;
    if (s.bitrateKbps < 64 || s.bitrateKbps > 320) s.bitrateKbps = 192;
    if (!(s.volume >= kVolumeMin && s.volume <= kVolumeMax)) s.volume = 1.25;
    s.volume = std::round(s.volume * 100.0) / 100.0;
    if (s.tempoEngine != "rubberband" && s.tempoEngine != "atempo") s.tempoEngine = "rubberband";
    if (trim(s.fileNameTemplate).empty() || s.fileNameTemplate == "{channel} - {title}{speedtag}")
        s.fileNameTemplate = Settings().fileNameTemplate;
    // 8 was the default before 0.1.5; treat it as "use the current default".
    if (s.autoCloseSeconds < 0 || s.autoCloseSeconds > 3600 || s.autoCloseSeconds == 8)
        s.autoCloseSeconds = Settings().autoCloseSeconds;
    if (trim(s.outputDir).empty() || toPath(s.outputDir) == oldDefaultOutputDir())
        s.outputDir = pathToUtf8(defaultOutputDir());
}

std::string settingsToJson(const Settings& s) {
    json j = {{"outputDir", s.outputDir},           {"speed", s.speed},
              {"bitrateKbps", s.bitrateKbps},       {"volume", s.volume},
              {"tempoEngine", s.tempoEngine},
              {"fileNameTemplate", s.fileNameTemplate}, {"autoCloseSeconds", s.autoCloseSeconds}};
    return j.dump(2);
}

void applySettingsJson(Settings& s, const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return;
    if (j.contains("outputDir") && j["outputDir"].is_string()) s.outputDir = j["outputDir"].get<std::string>();
    if (j.contains("speed") && j["speed"].is_number()) s.speed = j["speed"].get<double>();
    if (j.contains("bitrateKbps") && j["bitrateKbps"].is_number()) s.bitrateKbps = j["bitrateKbps"].get<int>();
    if (j.contains("volume") && j["volume"].is_number()) s.volume = j["volume"].get<double>();
    if (j.contains("tempoEngine") && j["tempoEngine"].is_string())
        s.tempoEngine = j["tempoEngine"].get<std::string>();
    if (j.contains("fileNameTemplate") && j["fileNameTemplate"].is_string())
        s.fileNameTemplate = j["fileNameTemplate"].get<std::string>();
    if (j.contains("autoCloseSeconds") && j["autoCloseSeconds"].is_number())
        s.autoCloseSeconds = j["autoCloseSeconds"].get<int>();
}

Settings loadSettings() {
    Settings s;
    std::string text;
    if (readTextFile(settingsDir() / "settings.json", text)) applySettingsJson(s, text);
    normalizeSettings(s);
    return s;
}

bool saveSettings(const Settings& in) {
    Settings s = in;
    normalizeSettings(s);
    return writeTextFileAtomic(settingsDir() / "settings.json", settingsToJson(s));
}

static std::string findTool(const char* name) {
#ifdef _WIN32
    std::string exe = std::string(name) + ".exe";
#else
    std::string exe = name;
#endif
    std::error_code ec;
    fs::path bundled = executableDir() / "tools" / exe;
    if (fs::exists(bundled, ec)) return pathToUtf8(bundled);
    fs::path sibling = executableDir() / exe;
    if (fs::exists(sibling, ec)) return pathToUtf8(sibling);
    return exe;  // rely on PATH
}

std::string ytDlpPath() { return findTool("yt-dlp"); }
std::string ffmpegPath() { return findTool("ffmpeg"); }

fs::path extensionDir() { return executableDir() / "extension"; }

}  // namespace pmp3
