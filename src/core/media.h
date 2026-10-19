// Podcast MP3 Downloader - pure logic used by the download pipeline.
// Everything here is free of I/O on child processes so it can be unit tested.
#pragma once

#include "util.h"

#include <string>
#include <vector>

namespace pmp3 {

struct FormatInfo {
    std::string id;
    std::string ext;
    std::string protocol;
    std::string acodec;  // "" = unknown, "none" = no audio
    std::string vcodec;  // "" = unknown, "none" = no video
    double abr = 0;      // audio bitrate kbps, 0 = unknown
    double tbr = 0;
    int height = 0;      // 0 = unknown
    double filesize = 0; // bytes, 0 = unknown
    int languagePreference = -1;  // YouTube: 10 = original audio track, lower = dubbed
    std::string note;             // format_note, e.g. "original (default)", "DRC"

    bool hasAudio() const { return acodec != "none"; }
    bool audioOnly() const { return hasAudio() && vcodec == "none"; }
    // YouTube's "DRC" (dynamic range compressed) copies sound flatter.
    bool isDrc() const;
};

struct VideoInfo {
    std::string id;
    std::string title;
    std::string channel;
    std::string uploadDate;   // YYYYMMDD or ""
    std::string webpageUrl;
    std::string thumbnail;
    std::string description;
    std::string extractor;
    double duration = 0;      // seconds
    bool isLive = false;
    bool isPlaylist = false;
    std::vector<FormatInfo> formats;
};

// Parses `yt-dlp -J` output. Returns false and sets err on failure.
bool parseVideoInfo(const std::string& json, VideoInfo& info, std::string& err);

// Parses Rumble's own video-info answer (rumble.com/embedJS/u3/?request=video
// &ver=2&v=<id>) that the Edge extension fetched through Edge's connection.
// Fills title/channel/date/duration/thumbnail/page and picks the media URL:
// the audio-only file if Rumble offers one, else the smallest MP4, else HLS.
bool parseRumbleEmbedJson(const std::string& json, VideoInfo& info, std::string& mediaUrl, std::string& err);

// Picks the best audio with the least video weight:
//  1) an audio-only stream with the highest bitrate, otherwise
//  2) the muxed stream with the best audio, smallest resolution on ties.
// Returns the index into info.formats, or -1 if nothing has audio.
int chooseFormat(const VideoInfo& info);

// "2026-10-18" from "20261018".
std::string prettyDate(const std::string& yyyymmdd);

// Expands {title} {channel} {date} {speed} {speedtag} {id} and sanitizes.
std::string buildFileName(const std::string& tmpl, const VideoInfo& info, double speed);

// Escapes a value for an ffmpeg ";FFMETADATA1" file.
std::string ffmetaEscape(const std::string& value);

struct TagSet {
    std::string title, artist, album, date, year, genre, comment, sourceUrl;
};
TagSet buildTags(const VideoInfo& info, double speed, double volume = 1.0);
std::string buildFfmetadata(const TagSet& tags);

// Audio filter for the chosen speed ("" for 1.0x).
std::string tempoFilter(double speed, const std::string& engine);
// Full ffmpeg -af chain: speed change, then volume boost with a limiter so
// louder peaks are held just below full scale instead of distorting.
// "" when nothing needs changing.
std::string audioFilter(double speed, const std::string& engine, double volume);

// Adds (or replaces) a WOAS "official audio source" URL frame and a COMM
// comment frame in the ID3v2.3/2.4 tag at the start of the file. ffmpeg
// writes "comment" as a TXXX frame, which most phone players ignore.
bool addId3Extras(const fs::path& mp3, const std::string& url, const std::string& comment, std::string& err);

// UTF-8 -> UTF-16LE bytes (with BOM), as ID3 text encoding 1 expects.
std::string utf16WithBom(const std::string& utf8);

// Parses "Duration: 01:02:03.45" from ffmpeg's stderr; -1 if absent.
double parseFfmpegDuration(const std::string& text);

// Progress lines we ask yt-dlp to print with --progress-template.
struct DownloadProgress {
    double downloaded = 0, total = 0, speed = 0, eta = -1;
};
bool parseDownloadProgress(const std::string& line, DownloadProgress& p);

// Parses "out_time_us=123456" from ffmpeg -progress; returns seconds or -1.
double parseFfmpegOutTime(const std::string& line);

// True if the URL looks like something we can handle (http/https).
bool looksLikeUrl(const std::string& s);
// "rumble", "youtube", "other".
std::string siteOf(const std::string& url);

}  // namespace pmp3
