#include "media.h"

#include "nlohmann/json.hpp"
#include "version.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace pmp3 {

using json = nlohmann::json;

static std::string str(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return std::string();
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<long long>());
    if (it->is_number()) return std::to_string(it->get<double>());
    return std::string();
}

static double num(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return 0;
    return it->get<double>();
}

bool parseVideoInfo(const std::string& text, VideoInfo& info, std::string& err) {
    // yt-dlp may print warnings before the JSON; start at the first '{'.
    size_t start = text.find('{');
    if (start == std::string::npos) {
        err = "yt-dlp returned no video information.";
        return false;
    }
    json j = json::parse(text.begin() + (std::ptrdiff_t)start, text.end(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        err = "Could not read the video information returned by yt-dlp.";
        return false;
    }
    std::string type = str(j, "_type");
    if (type == "playlist" || type == "multi_video") {
        // A page with several videos: use the first real entry.
        auto it = j.find("entries");
        if (it != j.end() && it->is_array() && !it->empty() && (*it)[0].is_object() &&
            (*it)[0].contains("formats")) {
            info.isPlaylist = true;
            json first = (*it)[0];
            j = first;
        } else {
            err = "This page is a list of videos, not a single video. Open the video itself and try again.";
            return false;
        }
    }
    info.id = str(j, "id");
    info.title = str(j, "title");
    if (info.title.empty()) info.title = str(j, "fulltitle");
    info.channel = str(j, "channel");
    if (info.channel.empty()) info.channel = str(j, "uploader");
    if (info.channel.empty()) info.channel = str(j, "uploader_id");
    info.uploadDate = str(j, "upload_date");
    if (info.uploadDate.empty()) info.uploadDate = str(j, "release_date");
    info.webpageUrl = str(j, "webpage_url");
    info.thumbnail = str(j, "thumbnail");
    info.description = str(j, "description");
    info.extractor = str(j, "extractor_key");
    info.duration = num(j, "duration");
    info.isLive = j.value("is_live", false) == true;

    auto fit = j.find("formats");
    if (fit != j.end() && fit->is_array()) {
        for (const auto& f : *fit) {
            if (!f.is_object()) continue;
            FormatInfo fi;
            fi.id = str(f, "format_id");
            fi.ext = str(f, "ext");
            fi.protocol = str(f, "protocol");
            fi.acodec = str(f, "acodec");
            fi.vcodec = str(f, "vcodec");
            fi.abr = num(f, "abr");
            fi.tbr = num(f, "tbr");
            fi.height = (int)num(f, "height");
            fi.filesize = num(f, "filesize");
            if (f.contains("language_preference") && f["language_preference"].is_number())
                fi.languagePreference = f["language_preference"].get<int>();
            fi.note = str(f, "format_note");
            if (fi.filesize <= 0) fi.filesize = num(f, "filesize_approx");
            // Storyboards / image tracks are not media we can use.
            if (fi.ext == "mhtml" || startsWith(fi.protocol, "mhtml")) continue;
            if (!fi.id.empty()) info.formats.push_back(fi);
        }
    }
    if (info.formats.empty()) {
        // Single-format result: yt-dlp puts the format fields on the root.
        FormatInfo fi;
        fi.id = str(j, "format_id");
        fi.ext = str(j, "ext");
        fi.protocol = str(j, "protocol");
        fi.acodec = str(j, "acodec");
        fi.vcodec = str(j, "vcodec");
        fi.height = (int)num(j, "height");
        if (!fi.id.empty()) info.formats.push_back(fi);
    }
    if (info.title.empty()) info.title = info.id.empty() ? "Podcast" : info.id;
    return true;
}

bool FormatInfo::isDrc() const {
    return id.find("-drc") != std::string::npos || toLower(note).find("drc") != std::string::npos;
}

static int protocolPenalty(const FormatInfo& f) {
    // Plain HTTP downloads are fastest and most reliable; HLS/DASH next.
    if (f.protocol == "https" || f.protocol == "http" || f.protocol.empty()) return 0;
    if (startsWith(f.protocol, "m3u8")) return 1;
    return 2;
}

int chooseFormat(const VideoInfo& info) {
    int best = -1;
    // 1) audio-only streams
    for (int i = 0; i < (int)info.formats.size(); ++i) {
        const auto& f = info.formats[i];
        if (!f.audioOnly()) continue;
        if (best < 0) {
            best = i;
            continue;
        }
        const auto& b = info.formats[best];
        // Original-language track first, then non-DRC, then bitrate.
        if (f.languagePreference != b.languagePreference) {
            if (f.languagePreference > b.languagePreference) best = i;
            continue;
        }
        if (f.isDrc() != b.isDrc()) {
            if (!f.isDrc()) best = i;
            continue;
        }
        double fa = f.abr > 0 ? f.abr : f.tbr, ba = b.abr > 0 ? b.abr : b.tbr;
        if (fa > ba || (fa == ba && protocolPenalty(f) < protocolPenalty(b))) best = i;
    }
    if (best >= 0) return best;

    // 2) muxed streams: best audio bitrate, then smallest picture.
    for (int i = 0; i < (int)info.formats.size(); ++i) {
        const auto& f = info.formats[i];
        if (!f.hasAudio()) continue;
        if (best < 0) {
            best = i;
            continue;
        }
        const auto& b = info.formats[best];
        if (f.abr != b.abr) {
            if (f.abr > b.abr) best = i;
            continue;
        }
        int fh = f.height > 0 ? f.height : 100000, bh = b.height > 0 ? b.height : 100000;
        if (fh != bh) {
            if (fh < bh) best = i;
            continue;
        }
        if (protocolPenalty(f) != protocolPenalty(b)) {
            if (protocolPenalty(f) < protocolPenalty(b)) best = i;
            continue;
        }
        double fs = f.filesize > 0 ? f.filesize : 1e18, bs = b.filesize > 0 ? b.filesize : 1e18;
        if (fs < bs) best = i;
    }
    return best;
}

std::string prettyDate(const std::string& d) {
    if (d.size() == 8 && std::all_of(d.begin(), d.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return d.substr(0, 4) + "-" + d.substr(4, 2) + "-" + d.substr(6, 2);
    return d;
}

std::string buildFileName(const std::string& tmpl, const VideoInfo& info, double speed) {
    std::string channel = info.channel.empty() ? "Unknown channel" : info.channel;
    std::string name = tmpl;
    name = replaceAll(name, "{title}", info.title);
    name = replaceAll(name, "{channel}", channel);
    // Pages without a publish date get today's date so names still sort.
    std::string date = info.uploadDate.empty() ? nowLocalString().substr(0, 10) : prettyDate(info.uploadDate);
    name = replaceAll(name, "{date}", date);
    name = replaceAll(name, "{id}", info.id);
    name = replaceAll(name, "{speed}", formatSpeed(speed));
    name = replaceAll(name, "{speedtag}", std::fabs(speed - 1.0) < 0.001 ? "" : " (" + formatSpeed(speed) + "x)");
    // Template pieces whose values were empty leave "- -" or "[]" behind.
    name = replaceAll(name, "[]", "");
    name = replaceAll(name, "()", "");
    name = replaceAll(name, " -  - ", " - ");
    return sanitizeFileName(name);
}

std::string ffmetaEscape(const std::string& v) {
    std::string out;
    for (char c : v) {
        if (c == '=' || c == ';' || c == '#' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\\n";
        } else if (c == '\r') {
            continue;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

TagSet buildTags(const VideoInfo& info, double speed, double volume) {
    TagSet t;
    t.title = info.title;
    t.artist = info.channel;
    t.album = info.channel.empty() ? std::string("Podcasts") : info.channel;
    t.date = prettyDate(info.uploadDate);
    t.year = info.uploadDate.size() >= 4 ? info.uploadDate.substr(0, 4) : std::string();
    t.genre = "Podcast";
    t.sourceUrl = info.webpageUrl;
    std::string c;
    if (!info.webpageUrl.empty()) c += "Source: " + info.webpageUrl + "\n";
    if (!t.date.empty()) c += "Published: " + t.date + "\n";
    c += "Speed: " + formatSpeed(speed) + "x\n";
    if (std::fabs(volume - 1.0) >= 0.001) c += "Volume: " + formatSpeed(volume) + "x\n";
    c += "Downloaded " + nowLocalString() + " with " PMP3_PRODUCT_NAME " v" PMP3_VERSION;
    t.comment = c;
    return t;
}

std::string buildFfmetadata(const TagSet& t) {
    std::string s = ";FFMETADATA1\n";
    auto add = [&s](const char* key, const std::string& value) {
        if (value.empty()) return;
        s += key;
        s += '=';
        s += ffmetaEscape(value);
        s += '\n';
    };
    add("title", t.title);
    add("artist", t.artist);
    add("album_artist", t.artist);
    add("album", t.album);
    add("date", t.year);           // ID3v2.3 TYER (4 digits)
    add("RELEASEDATE", t.date);    // TXXX with the full date
    add("genre", t.genre);
    add("SOURCE_URL", t.sourceUrl);  // TXXX:SOURCE_URL
    return s;
}

std::string tempoFilter(double speed, const std::string& engine) {
    if (std::fabs(speed - 1.0) < 0.001) return std::string();
    char buf[64];
    if (engine == "atempo")
        std::snprintf(buf, sizeof(buf), "atempo=%.4g", speed);
    else
        std::snprintf(buf, sizeof(buf), "rubberband=tempo=%.4g", speed);
    return buf;
}

std::string audioFilter(double speed, const std::string& engine, double volume) {
    std::string chain = tempoFilter(speed, engine);
    if (std::fabs(volume - 1.0) >= 0.001) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "volume=%.4g", volume);
        if (!chain.empty()) chain += ",";
        chain += buf;
        if (volume > 1.0) chain += ",alimiter=limit=0.97:level=0";
    }
    return chain;
}

// ---- ID3 -------------------------------------------------------------

static uint32_t readSyncsafe(const unsigned char* p) {
    return ((uint32_t)(p[0] & 0x7f) << 21) | ((uint32_t)(p[1] & 0x7f) << 14) | ((uint32_t)(p[2] & 0x7f) << 7) |
           (uint32_t)(p[3] & 0x7f);
}
static void writeSyncsafe(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)((v >> 21) & 0x7f);
    p[1] = (unsigned char)((v >> 14) & 0x7f);
    p[2] = (unsigned char)((v >> 7) & 0x7f);
    p[3] = (unsigned char)(v & 0x7f);
}
static uint32_t readBE(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static void writeBE(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

// URL frames are ISO-8859-1; percent-encode anything outside printable ASCII.
static std::string asciiUrl(const std::string& url) {
    std::string out;
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : url) {
        if (c > 0x20 && c < 0x7f) {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::string utf16WithBom(const std::string& utf8) {
    std::string out = "\xFF\xFE";
    auto put = [&out](uint32_t u) {
        out.push_back((char)(u & 0xff));
        out.push_back((char)(u >> 8));
    };
    for (size_t i = 0; i < utf8.size();) {
        unsigned char c = (unsigned char)utf8[i];
        uint32_t cp;
        int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { cp = 0xFFFD; len = 1; }
        if (i + (size_t)len > utf8.size()) { cp = 0xFFFD; len = 1; }
        for (int k = 1; k < len; ++k) cp = (cp << 6) | ((unsigned char)utf8[i + (size_t)k] & 0x3F);
        i += (size_t)len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            put(0xD800 + (cp >> 10));
            put(0xDC00 + (cp & 0x3FF));
        } else {
            put(cp);
        }
    }
    return out;
}

bool addId3Extras(const fs::path& mp3, const std::string& url, const std::string& comment, std::string& err) {
    std::ifstream in(mp3, std::ios::binary);
    if (!in) {
        err = "cannot open MP3";
        return false;
    }
    unsigned char hdr[10];
    if (!in.read((char*)hdr, 10) || hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') {
        err = "no ID3v2 tag";
        return false;
    }
    int major = hdr[3];
    unsigned char flags = hdr[5];
    if ((major != 3 && major != 4) || (flags & 0x80) || (flags & 0x40)) {
        err = "unsupported ID3 layout";
        return false;
    }
    uint32_t tagSize = readSyncsafe(hdr + 6);
    std::vector<unsigned char> body(tagSize);
    if (!in.read((char*)body.data(), tagSize)) {
        err = "truncated ID3 tag";
        return false;
    }
    size_t footer = (major == 4 && (flags & 0x10)) ? 10 : 0;
    in.seekg((std::streamoff)(10 + tagSize + footer));

    // Copy all frames except an existing WOAS; measure the padding.
    std::vector<unsigned char> frames;
    size_t pos = 0;
    while (pos + 10 <= body.size()) {
        const unsigned char* f = body.data() + pos;
        if (f[0] == 0) break;  // padding
        uint32_t fsize = major == 4 ? readSyncsafe(f + 4) : readBE(f + 4);
        if (pos + 10 + fsize > body.size()) break;
        std::string id((const char*)f, 4);
        bool replaced = (id == "WOAS" && !url.empty()) || (id == "COMM" && !comment.empty());
        if (!replaced) frames.insert(frames.end(), f, f + 10 + fsize);
        pos += 10 + fsize;
    }

    auto addFrame = [&](const char* id, const std::string& data) {
        unsigned char fh[10] = {(unsigned char)id[0], (unsigned char)id[1], (unsigned char)id[2],
                                (unsigned char)id[3], 0, 0, 0, 0, 0, 0};
        if (major == 4)
            writeSyncsafe(fh + 4, (uint32_t)data.size());
        else
            writeBE(fh + 4, (uint32_t)data.size());
        frames.insert(frames.end(), fh, fh + 10);
        frames.insert(frames.end(), data.begin(), data.end());
    };
    if (!url.empty()) addFrame("WOAS", asciiUrl(url));
    if (!comment.empty()) {
        // encoding 1 (UTF-16 with BOM), language, empty description, text
        std::string data = std::string("\x01") + "eng" + utf16WithBom("") + std::string(2, '\0') +
                           utf16WithBom(comment);
        addFrame("COMM", data);
    }
    const size_t padding = 1024;
    frames.resize(frames.size() + padding, 0);

    fs::path tmp = mp3;
    tmp += ".id3tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            err = "cannot write temporary MP3";
            return false;
        }
        unsigned char nh[10];
        std::copy(hdr, hdr + 10, nh);
        nh[5] = (unsigned char)(flags & ~0x10);  // we drop any footer
        writeSyncsafe(nh + 6, (uint32_t)frames.size());
        out.write((const char*)nh, 10);
        out.write((const char*)frames.data(), (std::streamsize)frames.size());
        std::vector<char> buf(1 << 20);
        while (in) {
            in.read(buf.data(), (std::streamsize)buf.size());
            std::streamsize n = in.gcount();
            if (n > 0) out.write(buf.data(), n);
        }
        if (!out) {
            err = "write failed";
            return false;
        }
    }
    in.close();
    std::error_code ec;
    fs::rename(tmp, mp3, ec);
    if (ec) {
        fs::remove(tmp, ec);
        err = "rename failed";
        return false;
    }
    return true;
}

// ---- progress ---------------------------------------------------------

static double parseNumberOrNA(const std::string& s) {
    if (s.empty() || s == "NA" || s == "None") return -1;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return -1;
    return v;
}

bool parseDownloadProgress(const std::string& line, DownloadProgress& p) {
    // Template: "[PMP3] <downloaded> <total> <total_estimate> <speed> <eta>"
    const std::string tag = "[PMP3] ";
    size_t at = line.find(tag);
    if (at == std::string::npos) return false;
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = at + tag.size(); i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ' ') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(line[i]);
        }
    }
    if (parts.size() < 5) return false;
    p.downloaded = std::max(0.0, parseNumberOrNA(parts[0]));
    double total = parseNumberOrNA(parts[1]);
    if (total <= 0) total = parseNumberOrNA(parts[2]);
    p.total = std::max(0.0, total);
    p.speed = std::max(0.0, parseNumberOrNA(parts[3]));
    p.eta = parseNumberOrNA(parts[4]);
    return true;
}

double parseFfmpegDuration(const std::string& text) {
    size_t p = text.find("Duration: ");
    if (p == std::string::npos) return -1;
    int h = 0, m = 0;
    double sec = 0;
    if (std::sscanf(text.c_str() + p + 10, "%d:%d:%lf", &h, &m, &sec) != 3) return -1;
    return h * 3600.0 + m * 60.0 + sec;
}

double parseFfmpegOutTime(const std::string& line) {
    const char* keys[] = {"out_time_us=", "out_time_ms="};  // both are microseconds
    for (auto k : keys) {
        if (startsWith(line, k)) {
            double v = parseNumberOrNA(line.substr(std::string(k).size()));
            return v < 0 ? -1 : v / 1e6;
        }
    }
    return -1;
}

bool looksLikeUrl(const std::string& s) {
    std::string t = toLower(trim(s));
    return (startsWith(t, "http://") || startsWith(t, "https://")) && t.size() > 10 &&
           t.find(' ') == std::string::npos;
}

std::string siteOf(const std::string& url) {
    std::string t = toLower(url);
    size_t p = t.find("://");
    if (p == std::string::npos) return "other";
    std::string host = t.substr(p + 3);
    host = host.substr(0, host.find_first_of("/?#"));
    auto is = [&host](const std::string& domain) { return host == domain || endsWith(host, "." + domain); };
    if (is("rumble.com")) return "rumble";
    if (is("youtube.com") || is("youtu.be")) return "youtube";
    return "other";
}

}  // namespace pmp3
