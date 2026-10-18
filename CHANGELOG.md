# Changelog

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
