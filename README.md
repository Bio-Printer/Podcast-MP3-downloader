# Podcast MP3 Downloader

Middle-click a Rumble or YouTube video (also YouTube players embedded on other sites) in Microsoft Edge and get its audio as an MP3 you can
play on your phone, already sped up to your preferred playback speed (default
1.5x, pitch kept natural).

* **Edge extension**: middle-click a video or a video thumbnail; pick speed
  (1x–2.5x) and quality (128/160/192 kbps stereo) in the toolbar popup.
* **PodcastMP3.exe**: paste a link and download without Edge; shows progress
  for downloads started from Edge.
* **podcastmp3-host.exe**: the helper Edge talks to (native messaging), also a
  command line tool.
* Uses [yt-dlp](https://github.com/yt-dlp/yt-dlp) to fetch the media and
  [FFmpeg](https://ffmpeg.org) to convert, speed up and tag it. Both are bundled
  in the release ZIP.

MP3 tags: title, channel (artist + album), year and full date, genre
"Podcast", cover picture (video thumbnail), a comment with the source address,
speed and download date, and the source URL (`WOAS` and `TXXX:SOURCE_URL`).

## Install / update

Download `PodcastMP3-vX.Y.Z-win64.zip` from
[Releases](https://github.com/Bio-Printer/Podcast-MP3-downloader/releases) and
follow `README.txt` inside it ([packaging/README-install.txt](packaging/README-install.txt)).
In short: unzip to `C:\Tmp\PodcastMP3`, run `PodcastMP3.exe` once, load
`C:\Tmp\PodcastMP3\extension` with **Load unpacked** in `edge://extensions`.
To update, unzip the new ZIP over the same folder; the extension reloads itself.

## Switching between the official build and your own build

Two copies of the programs can live side by side:

| Build | Folder | Shows |
|---|---|---|
| Official release (from the ZIP) | `C:\Tmp\PodcastMP3\` | **Claude build** |
| Your Visual Studio build | `C:\Tmp\PodcastMP3\source\build\vs\x64\Release\` (or `...\Debug\`) | **EDN - Built** |

**Edge talks to only one of them at a time: the copy whose `PodcastMP3.exe`
was started most recently.** Starting `PodcastMP3.exe` registers its own
folder with Edge (this is the "Edge link OK" on the bottom line), so
switching is just a matter of starting the other one once.

### Use the official build (normal use)

1. Close all Podcast MP3 windows.
2. Start `C:\Tmp\PodcastMP3\PodcastMP3.exe` (the one in the root of the
   install folder, where the ZIP was unpacked) once. You can close it again.
3. Check: the title bar ends in **"- Claude build"**, and the Edge toolbar
   popup shows `Helper v... · Claude build`.

Unpacking a new release ZIP does **not** switch builds by itself; if your
own build was the last one started, start the root `PodcastMP3.exe` once
after updating.

### Use your own build

1. Build the solution (`PodcastMP3.sln`, Release | x64 recommended).
2. Your build folder needs the download tools next to the programs, in a
   `tools` folder. Either copy `C:\Tmp\PodcastMP3\tools` into the build
   folder (giving `...\Release\tools\yt-dlp\yt-dlp.exe`,
   `...\Release\tools\ffmpeg.exe`, `...\Release\tools\deno.exe`), or run
   `powershell -ExecutionPolicy Bypass -File packaging\fetch_tools.ps1` once in
   the source folder so every build copies them automatically.
3. Close all Podcast MP3 windows.
4. Start `PodcastMP3.exe` from your build folder once.
5. Check: the title bar ends in **"- EDN - Built"**, and the Edge popup shows
   `Helper v... · EDN - Built`.

To run it from Visual Studio with F5, right-click the **PodcastMP3** project
and choose **Set as Startup Project** (otherwise Visual Studio tries to start
the `pmp3core` library).

### Things that do not change when you switch

- The Edge extension: keep it loaded from `C:\Tmp\PodcastMP3\extension`.
  Only the helper programs differ between the builds.
- Your settings (speed, volume, quality, MP3 folder) and the list of recent
  downloads: both builds share them (stored in `%APPDATA%\PodcastMP3` and
  `%LOCALAPPDATA%\PodcastMP3`).

### Which one is running right now?

- Edge toolbar popup: `Helper v<version> (build <id>) · Claude build` or `· EDN - Built`.
- `PodcastMP3.exe`: the title bar, the bottom line, and **About** ("Built by"
  and "Installed in" show the builder and the folder).
- Progress windows: the builder is in the window title.
- Command line: `podcastmp3-host.exe --version`.

## How it fits together

```
Edge page (content.js)          middle-click -> page URL
   │ chrome.runtime.sendMessage
Extension service worker (background.js)
   │ native messaging (JSON over stdin/stdout)
podcastmp3-host.exe             registered under HKCU\Software\Microsoft\Edge\NativeMessagingHosts
   │ starts, then exits
PodcastMP3.exe --job ...        small progress window, runs the job:
   ├─ yt-dlp -J <url>            read title/channel/date/formats
   ├─ yt-dlp -f <best audio, smallest video> --write-thumbnail
   ├─ ffmpeg rubberband=tempo=1.5 → libmp3lame 192k stereo + ID3 + cover
   └─ add WOAS/COMM frames, move to C:\Tmp\Audio as "<date> <channel> - <title> (1.5x).mp3"
Job status: %LOCALAPPDATA%\PodcastMP3\jobs\<id>.json (the popup reads these)
```

The extension has a fixed ID (`dbnhcdhaapecgdcdniigdkgdbhacblhm`, from the
`key` in `manifest.json`), so the helper registration keeps working across
updates. The version is stamped from `VERSION` into `src/core/version.h` and
`extension/manifest.json` (`version_name` shows the build id in
`edge://extensions`); the popup compares the extension and helper versions.

## Source layout

| Path | What |
|---|---|
| `src/core/` | portable C++17 engine (Windows + Linux): job pipeline, format choice, ID3, native messaging, settings |
| `src/host/` | `podcastmp3-host` command line / native messaging host |
| `src/gui/` | `PodcastMP3.exe` Win32 window (Windows only) |
| `extension/` | Edge (Chromium MV3) extension, plain JavaScript |
| `tests/` | unit tests (`pmp3tests`) and the browser end-to-end test |
| `PodcastMP3.sln` + `*.vcxproj` | Visual Studio 2026 solution (generated by `scripts/make_vs_projects.py`) |
| `CMakeLists.txt` | CMake build (Linux, MinGW cross-build, or Visual Studio "Open Folder") |
| `packaging/` | release scripts, install README, MinGW toolchain |

## Building

* **Visual Studio 2026**: see [docs/BUILD-VS2026.md](docs/BUILD-VS2026.md).
* **Linux (development and the official release build)**: see
  [docs/DEVELOPING.md](docs/DEVELOPING.md).
