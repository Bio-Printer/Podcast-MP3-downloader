// Podcast MP3 Downloader - small portable helpers (strings, paths, time).
// All strings inside the program are UTF-8. Conversion to UTF-16 happens
// only at the Windows API boundary.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pmp3 {

namespace fs = std::filesystem;

#ifdef _WIN32
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);
#endif

// UTF-8 string <-> filesystem path, correct on both Windows and Linux.
fs::path toPath(const std::string& utf8);
std::string pathToUtf8(const fs::path& p);

std::string trim(const std::string& s);
std::string toLower(std::string s);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);
std::string replaceAll(std::string s, const std::string& from, const std::string& to);

// Turn an arbitrary title into a safe Windows/Android file name (no
// extension). Removes characters that are illegal on Windows, control
// characters, trailing dots/spaces, reserved device names and limits the
// length (in bytes, without cutting a UTF-8 sequence in half).
std::string sanitizeFileName(const std::string& name, size_t maxBytes = 150);

// "1.5" for 1.5, "1.25" for 1.25, "2" for 2.0.
std::string formatSpeed(double speed);

// Human friendly "12.3 MB".
std::string formatBytes(double bytes);

// Seconds -> "1:02:03" / "4:05".
std::string formatDuration(double seconds);

// Current local time "2026-10-18 13:45:00" and a compact id-friendly form.
std::string nowLocalString();
std::string nowCompactString();  // 20261018-134500
int64_t nowEpochSeconds();

// Random hex string (for job ids).
std::string randomHex(size_t chars);

bool readTextFile(const fs::path& p, std::string& out);
// Writes via a temporary file + rename so readers never see a half file.
bool writeTextFileAtomic(const fs::path& p, const std::string& content);

// Move a file, falling back to copy+delete across volumes.
bool moveFile(const fs::path& from, const fs::path& to, std::string& err);

// If `p` exists, returns "name (2).ext", "name (3).ext", ...
fs::path uniquePath(const fs::path& p);

// Directory containing the running executable.
fs::path executableDir();

}  // namespace pmp3
