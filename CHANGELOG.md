# Changelog

## 0.1.8

- Fix: after the extension updated itself, middle-click on a page that was
  already open showed "Sending to Podcast MP3..." forever and nothing
  happened. The outdated page script now steps aside for the new one.
- If the extension ever doesn't answer within 20 seconds, the message says
  so and suggests reloading the page, instead of "Sending..." forever.

## 0.1.7

- Middle-click now works on YouTube players embedded in other sites before
  they start playing (YouTube shows only a picture there until you press
  play), on YouTube search/home thumbnails that play a hover preview, and
  after moving between YouTube videos without reloading the page.
- Tabs that were already open when the extension updated get the
  middle-click handler right away (no page reload needed).
- Faster start: the video page is read once instead of twice (yt-dlp reuses
  what it found for the download), and the bundled yt-dlp is now the
  unpacked folder build (tools\yt-dlp\), which starts much faster than the
  single-file exe that unpacks itself every time.
- "Reading video page" shows what yt-dlp is doing and a seconds counter.
- The browser-impersonation retry is no longer tried for YouTube bot checks
  (it cannot help there and only made the failure take longer).

## 0.1.6

- YouTube: watch pages, Shorts, youtu.be links, thumbnails, and YouTube
  players embedded on other web sites (middle-click inside the player).
- "Download MP3 of this tab" (toolbar button, Alt+Shift+M, right-click) finds
  a video player embedded in any page; if there is none it hands the page
  address to yt-dlp, which can find many other embedded players.
- Picks YouTube's original-language audio track and avoids the "DRC"
  (dynamic range compressed) copies.
- Bundles Deno (tools\deno.exe), the JavaScript runtime yt-dlp needs to read
  YouTube.

## 0.1.5

- The "MP3 ready" notification stays on screen for 30 seconds (Windows used
  to hide it after about 5), and the progress window waits 30 seconds before
  closing itself (was 8).
- README: close all Podcast MP3 windows before unzipping an update; Edge can
  stay open.

## 0.1.4

- Volume boost: slider in the Edge popup (1x-2x, default 1.25x) and a
  "Volume boost" choice in PodcastMP3.exe. A limiter keeps boosted peaks
  just below full scale so loud parts don't distort. The boost is noted in
  the MP3's comment tag. Command line: `--volume 1.25`.

## 0.1.3

- One ZIP per release. Files are at the top level of the ZIP, so extracting
  it into the install folder (e.g. `C:\Tmp\PodcastMP3`) updates everything in
  place. The full source tree of the build is included in `source\`.

## 0.1.2

- If Rumble (Cloudflare) refuses the first request, the download is retried
  automatically with yt-dlp presenting itself as a regular Chrome browser.

## 0.1.1

- MP3 files are saved to `C:\Tmp\Audio` by default (created if missing).
- File names start with the video's publish date, e.g.
  `2026-10-15 Channel - Title (1.5x).mp3` (today's date if the page has none).
- Settings saved by 0.1.0 with the old defaults move to the new ones
  automatically; a folder you chose yourself is kept.

## 0.1.0 - first release

- Edge extension: middle-click a Rumble video or video thumbnail to save its
  audio as MP3; right-click menu, toolbar button and Alt+Shift+M also work.
- Speed picker (1x-2.5x, default 1.5x) applied with pitch-preserving time
  stretching (ffmpeg rubberband, atempo as fallback).
- MP3: 128/160/192 kbps stereo, 44.1 kHz, ID3v2.3 tags with title, channel
  (artist/album), year and full date, genre Podcast, cover picture, comment,
  and the source web address (WOAS + TXXX:SOURCE_URL).
- Downloads the smallest video stream that carries the best audio (or an
  audio-only stream when the site offers one).
- PodcastMP3.exe: paste-a-link window, progress window for Edge jobs, folder
  and quality settings, "Update yt-dlp", About box with all versions.
- podcastmp3-host.exe: native messaging helper plus command line
  (`download`, `info`, `register`, `--version`).
- Version shown everywhere (popup, edge://extensions, window title, About,
  command line); the extension reloads itself when newer files are unzipped
  over the install folder.
