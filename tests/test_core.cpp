// Unit tests for the portable core. Run: pmp3tests  (exit code 0 = all passed)

#include "host.h"
#include "job.h"
#include "media.h"
#include "settings.h"
#include "util.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace pmp3;

static int g_failed = 0, g_passed = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (cond) {                                                          \
            ++g_passed;                                                      \
        } else {                                                             \
            ++g_failed;                                                      \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        auto va = (a);                                                                     \
        auto vb = (b);                                                                     \
        if (va == vb) {                                                                    \
            ++g_passed;                                                                    \
        } else {                                                                           \
            ++g_failed;                                                                    \
            std::ostringstream ss;                                                         \
            ss << va << " != " << vb;                                                      \
            std::printf("FAIL %s:%d  %s == %s  (%s)\n", __FILE__, __LINE__, #a, #b,        \
                        ss.str().c_str());                                                 \
        }                                                                                  \
    } while (0)

static void testSanitize() {
    CHECK_EQ(sanitizeFileName("Hello: World?"), std::string("Hello- World"));
    CHECK_EQ(sanitizeFileName("a/b\\c"), std::string("a-b-c"));
    CHECK_EQ(sanitizeFileName("  trailing dots... "), std::string("trailing dots"));
    CHECK_EQ(sanitizeFileName("CON"), std::string("_CON"));
    CHECK_EQ(sanitizeFileName(""), std::string("podcast"));
    CHECK_EQ(sanitizeFileName("tab\there\nnewline"), std::string("tab here newline"));
    // Multi-byte characters are never cut in half.
    std::string s;
    for (int i = 0; i < 100; ++i) s += "\xC3\xA9";  // é
    std::string r = sanitizeFileName(s, 151);
    CHECK_EQ(r.size(), (size_t)150);
    CHECK_EQ(sanitizeFileName("Emoji \xF0\x9F\x8E\xA7 ok"), std::string("Emoji \xF0\x9F\x8E\xA7 ok"));
}

static void testFormatting() {
    CHECK_EQ(formatSpeed(1.5), std::string("1.5"));
    CHECK_EQ(formatSpeed(1.25), std::string("1.25"));
    CHECK_EQ(formatSpeed(2.0), std::string("2"));
    CHECK_EQ(formatDuration(3723), std::string("1:02:03"));
    CHECK_EQ(formatDuration(65), std::string("1:05"));
    CHECK_EQ(formatBytes(1536), std::string("1.5 KB"));
    CHECK_EQ(prettyDate("20261018"), std::string("2026-10-18"));
    CHECK_EQ(prettyDate(""), std::string(""));
}

static const char* kRumbleLikeJson = R"({
  "id": "v6abc12", "title": "Episode 42: Big News", "channel": "Some Show",
  "upload_date": "20261015", "webpage_url": "https://rumble.com/v6abc12-episode-42.html",
  "thumbnail": "https://example/thumb.jpg", "duration": 3600, "extractor_key": "Rumble",
  "formats": [
    {"format_id": "mp4-1080p", "ext": "mp4", "protocol": "https", "height": 1080, "tbr": 4000},
    {"format_id": "mp4-360p", "ext": "mp4", "protocol": "https", "height": 360, "tbr": 800},
    {"format_id": "hls-240", "ext": "mp4", "protocol": "m3u8_native", "height": 240, "tbr": 500},
    {"format_id": "mp4-240p", "ext": "mp4", "protocol": "https", "height": 240, "tbr": 450},
    {"format_id": "sb0", "ext": "mhtml", "protocol": "mhtml", "acodec": "none", "vcodec": "none"}
  ]})";

static void testParseAndChoose() {
    VideoInfo info;
    std::string err;
    CHECK(parseVideoInfo(std::string("WARNING: something\n") + kRumbleLikeJson, info, err));
    CHECK_EQ(info.title, std::string("Episode 42: Big News"));
    CHECK_EQ(info.channel, std::string("Some Show"));
    CHECK_EQ(info.formats.size(), (size_t)4);  // storyboard dropped
    int i = chooseFormat(info);
    CHECK(i >= 0);
    // Smallest picture, plain https preferred over HLS at the same height.
    if (i >= 0) CHECK_EQ(info.formats[(size_t)i].id, std::string("mp4-240p"));

    // An audio-only stream always wins.
    VideoInfo a;
    CHECK(parseVideoInfo(R"({"id":"x","title":"t","formats":[
        {"format_id":"v","vcodec":"avc1","acodec":"mp4a","height":144},
        {"format_id":"a1","vcodec":"none","acodec":"opus","abr":64},
        {"format_id":"a2","vcodec":"none","acodec":"mp4a","abr":128}]})",
                         a, err));
    CHECK_EQ(a.formats[(size_t)chooseFormat(a)].id, std::string("a2"));

    // Better audio beats smaller picture when bitrates are known.
    VideoInfo b;
    CHECK(parseVideoInfo(R"({"id":"x","title":"t","formats":[
        {"format_id":"small","acodec":"mp4a","abr":48,"height":144},
        {"format_id":"big","acodec":"mp4a","abr":128,"height":720},
        {"format_id":"mute","acodec":"none","vcodec":"avc1","height":100}]})",
                         b, err));
    CHECK_EQ(b.formats[(size_t)chooseFormat(b)].id, std::string("big"));

    VideoInfo none;
    CHECK(parseVideoInfo(R"({"id":"x","title":"t","formats":[{"format_id":"m","acodec":"none","vcodec":"avc1"}]})",
                         none, err));
    CHECK_EQ(chooseFormat(none), -1);

    VideoInfo list;
    CHECK(!parseVideoInfo(R"({"_type":"playlist","entries":[{"_type":"url","url":"x"}]})", list, err));
    CHECK(!parseVideoInfo("not json", list, err));
}

static void testFileName() {
    VideoInfo info;
    info.title = "Episode 42: Big News?";
    info.channel = "Some Show";
    info.uploadDate = "20261015";
    CHECK_EQ(buildFileName("{channel} - {title}{speedtag}", info, 1.5),
             std::string("Some Show - Episode 42- Big News (1.5x)"));
    CHECK_EQ(buildFileName("{channel} - {title}{speedtag}", info, 1.0), std::string("Some Show - Episode 42- Big News"));
    CHECK_EQ(buildFileName("{date} {title} [{speed}x]", info, 2.0), std::string("2026-10-15 Episode 42- Big News [2x]"));
}

static void testFfmeta() {
    CHECK_EQ(ffmetaEscape("a=b;c#d\\e"), std::string("a\\=b\\;c\\#d\\\\e"));
    CHECK_EQ(ffmetaEscape("line1\nline2"), std::string("line1\\\nline2"));
    VideoInfo info;
    info.title = "T=1";
    info.channel = "C";
    info.uploadDate = "20261015";
    info.webpageUrl = "https://rumble.com/v1.html";
    std::string m = buildFfmetadata(buildTags(info, 1.5));
    CHECK(startsWith(m, ";FFMETADATA1\n"));
    CHECK(m.find("title=T\\=1\n") != std::string::npos);
    CHECK(m.find("date=2026\n") != std::string::npos);
    CHECK(m.find("SOURCE_URL=https://rumble.com/v1.html\n") != std::string::npos);
    CHECK_EQ(tempoFilter(1.0, "rubberband"), std::string(""));
    CHECK_EQ(tempoFilter(1.5, "rubberband"), std::string("rubberband=tempo=1.5"));
    CHECK_EQ(tempoFilter(1.25, "atempo"), std::string("atempo=1.25"));
}

static void testProgress() {
    DownloadProgress p;
    CHECK(parseDownloadProgress("[PMP3] 1048576 4194304 NA 524288.5 6", p));
    CHECK_EQ(p.downloaded, 1048576.0);
    CHECK_EQ(p.total, 4194304.0);
    CHECK_EQ(p.eta, 6.0);
    CHECK(parseDownloadProgress("[PMP3] 100 NA 1000 NA NA", p));
    CHECK_EQ(p.total, 1000.0);
    CHECK_EQ(p.eta, -1.0);
    CHECK(!parseDownloadProgress("[download] 10%", p));
    CHECK_EQ(parseFfmpegOutTime("out_time_us=2500000"), 2.5);
    CHECK_EQ(parseFfmpegOutTime("bitrate=1k"), -1.0);
}

static void testUrls() {
    CHECK(looksLikeUrl("https://rumble.com/v6abc-x.html"));
    CHECK(!looksLikeUrl("rumble.com/v6abc"));
    CHECK(!looksLikeUrl("https://a b"));
    CHECK_EQ(siteOf("https://rumble.com/v1.html"), std::string("rumble"));
    CHECK_EQ(siteOf("https://www.youtube.com/watch?v=1"), std::string("youtube"));
    CHECK_EQ(siteOf("https://notrumble.com/x"), std::string("other"));
}

static void testNativeFraming() {
    std::string framed = encodeNativeMessage("{\"cmd\":\"ping\"}");
    CHECK_EQ(framed.size(), (size_t)18);
    CHECK_EQ((int)(unsigned char)framed[0], 14);
    std::istringstream in(framed + framed);
    std::string msg;
    CHECK(readNativeMessage(in, msg));
    CHECK_EQ(msg, std::string("{\"cmd\":\"ping\"}"));
    CHECK(readNativeMessage(in, msg));
    CHECK(!readNativeMessage(in, msg));
    std::string reply = handleHostRequest("{\"cmd\":\"nope\",\"requestId\":7}");
    CHECK(reply.find("\"requestId\":7") != std::string::npos);
    CHECK(reply.find("\"ok\":false") != std::string::npos);
}

static void testJobStatusJson() {
    JobStatus s;
    s.id = "20261018-120000-abcdef";
    s.state = JobState::Converting;
    s.title = "Ünïcode title";
    s.percent = 42.5;
    JobStatus r;
    CHECK(jobStatusFromJson(jobStatusToJson(s), r));
    CHECK_EQ(r.title, s.title);
    CHECK(r.state == JobState::Converting);
    CHECK_EQ(r.percent, 42.5);
}

static void testSettings() {
    Settings s;
    applySettingsJson(s, R"({"speed": 9, "bitrateKbps": 160, "tempoEngine": "bogus"})");
    normalizeSettings(s);
    CHECK_EQ(s.speed, 1.5);
    CHECK_EQ(s.bitrateKbps, 160);
    CHECK_EQ(s.tempoEngine, std::string("rubberband"));
    CHECK(!s.outputDir.empty());
}

// Builds a tiny file with an ID3v2.3 tag and checks the WOAS frame.
static void testId3() {
    fs::path p = fs::temp_directory_path() / ("pmp3-id3-test-" + randomHex(6) + ".mp3");
    {
        std::ofstream f(p, std::ios::binary);
        const char tit2[] = {'T', 'I', 'T', '2', 0, 0, 0, 3, 0, 0, 0, 'H', 'i'};
        unsigned char hdr[10] = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, (unsigned char)(sizeof(tit2) + 20)};
        f.write((const char*)hdr, 10);
        f.write(tit2, sizeof(tit2));
        std::string pad(20, '\0');
        f.write(pad.data(), (std::streamsize)pad.size());
        f << "AUDIODATA";
    }
    std::string err;
    CHECK(addId3Extras(p, "https://rumble.com/v1-é.html", "first", err));
    CHECK(addId3Extras(p, "https://rumble.com/v2.html", "Söurce", err));  // replaces, not duplicates
    std::string data;
    CHECK(readTextFile(p, data));
    CHECK(data.find("WOAS") != std::string::npos);
    CHECK(data.find("https://rumble.com/v2.html") != std::string::npos);
    CHECK(data.find("v1-") == std::string::npos);
    CHECK(data.find("TIT2") != std::string::npos);
    CHECK(endsWith(data, "AUDIODATA"));
    CHECK(data.find("COMM") != std::string::npos);
    CHECK(data.find("COMM") == data.rfind("COMM"));
    CHECK(data.find(utf16WithBom("S\xC3\xB6urce")) != std::string::npos);
    CHECK_EQ(utf16WithBom("A\xF0\x9F\x8E\xA7"), std::string("\xFF\xFE" "A\0" "\x3C\xD8\xA7\xDF", 8));
    CHECK_EQ(parseFfmpegDuration("  Duration: 01:02:03.50, start"), 3723.5);
    std::error_code ec;
    fs::remove(p, ec);
}

int main() {
    testSanitize();
    testFormatting();
    testParseAndChoose();
    testFileName();
    testFfmeta();
    testProgress();
    testUrls();
    testNativeFraming();
    testJobStatusJson();
    testSettings();
    testId3();
    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
