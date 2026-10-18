#include "util.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace pmp3 {

#ifdef _WIN32
std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), &s[0], n, nullptr, nullptr);
    return s;
}
#endif

fs::path toPath(const std::string& utf8) {
#ifdef _WIN32
    return fs::path(widen(utf8));
#else
    return fs::path(utf8);
#endif
}

std::string pathToUtf8(const fs::path& p) {
#ifdef _WIN32
    return narrow(p.wstring());
#else
    return p.string();
#endif
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (unsigned char)s[b] <= ' ') ++b;
    while (e > b && (unsigned char)s[e - 1] <= ' ') --e;
    return s.substr(b, e - b);
}

std::string toLower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string sanitizeFileName(const std::string& name, size_t maxBytes) {
    std::string out;
    out.reserve(name.size());
    bool lastSpace = false;
    for (unsigned char c : name) {
        char r = (char)c;
        if (c < 0x20 || c == 0x7f) r = ' ';
        switch (c) {
            case '<': case '>': case '"': case '|': case '?': case '*':
                r = ' ';
                break;
            case ':': r = '-'; break;
            case '/': case '\\': r = '-'; break;
            default: break;
        }
        if (r == ' ') {
            if (lastSpace) continue;
            lastSpace = true;
        } else {
            lastSpace = false;
        }
        out.push_back(r);
    }
    out = trim(out);

    // Limit the length without splitting a UTF-8 multi-byte sequence.
    if (out.size() > maxBytes) {
        size_t cut = maxBytes;
        while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80) --cut;
        out.resize(cut);
        out = trim(out);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();

    static const char* reserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4",
                                     "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2",
                                     "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    std::string lower = toLower(out);
    for (auto* r : reserved)
        if (lower == r) out = "_" + out;
    if (out.empty()) out = "podcast";
    return out;
}

std::string formatSpeed(double speed) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", speed);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string formatBytes(double bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    while (bytes >= 1024.0 && u < 4) {
        bytes /= 1024.0;
        ++u;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), u == 0 ? "%.0f %s" : "%.1f %s", bytes, units[u]);
    return buf;
}

std::string formatDuration(double seconds) {
    if (!(seconds >= 0)) seconds = 0;
    long long t = (long long)std::llround(seconds);
    long long h = t / 3600, m = (t / 60) % 60, s = t % 60;
    char buf[32];
    if (h > 0)
        std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", h, m, s);
    else
        std::snprintf(buf, sizeof(buf), "%lld:%02lld", m, s);
    return buf;
}

static std::tm localTm() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

std::string nowLocalString() {
    std::tm tm = localTm();
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::string nowCompactString() {
    std::tm tm = localTm();
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return buf;
}

int64_t nowEpochSeconds() {
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string randomHex(size_t chars) {
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen(((uint64_t)rd() << 32) ^ rd() ^
                        (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
    std::string s;
    for (size_t i = 0; i < chars; ++i) s.push_back(hex[gen() & 15]);
    return s;
}

bool readTextFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeTextFileAtomic(const fs::path& p, const std::string& content) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp" + randomHex(6);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(content.data(), (std::streamsize)content.size());
        if (!f) return false;
    }
    // A reader may briefly hold the file open on Windows; retry a little.
    for (int i = 0; i < 20; ++i) {
        fs::rename(tmp, p, ec);
        if (!ec) return true;
#ifdef _WIN32
        Sleep(25);
#else
        usleep(25000);
#endif
    }
    fs::remove(tmp, ec);
    return false;
}

bool moveFile(const fs::path& from, const fs::path& to, std::string& err) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) return true;
    // Different drive: copy then delete.
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    fs::remove(from, ec);
    return true;
}

fs::path uniquePath(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return p;
    fs::path dir = p.parent_path();
    fs::path stem = p.stem();
    fs::path ext = p.extension();
    for (int i = 2; i < 10000; ++i) {
        fs::path candidate = dir / stem;
        candidate += toPath(" (" + std::to_string(i) + ")");
        candidate += ext;
        if (!fs::exists(candidate, ec)) return candidate;
    }
    return p;
}

fs::path executableDir() {
#ifdef _WIN32
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, &buf[0], (DWORD)buf.size());
    buf.resize(n);
    return fs::path(buf).parent_path();
#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return fs::current_path();
    return p.parent_path();
#endif
}

}  // namespace pmp3
