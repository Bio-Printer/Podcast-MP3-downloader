# Building with Visual Studio 2026

## What you need

* Visual Studio 2026 with the **Desktop development with C++** workload
  (MSVC v145 toolset and a Windows 10/11 SDK).
* Nothing else to compile. To make a complete runnable folder you also need the
  bundled tools (yt-dlp.exe, ffmpeg.exe), fetched by a script below.

## Build

1. Open `PodcastMP3.sln`.
2. Choose **Release | x64** (or Debug | x64) and **Build > Build Solution**.
3. Output goes to `build\vs\x64\<Configuration>\`:
   * `PodcastMP3.exe`, `podcastmp3-host.exe`, `pmp3tests.exe`
   * `extension\` (copied by the post-build step of PodcastMP3)
   * `tools\yt-dlp.exe`, `tools\ffmpeg.exe` (copied if you fetched them, see below)
4. Run the unit tests: `build\vs\x64\Release\pmp3tests.exe` (prints
   "N passed, 0 failed").

The programs use the static C++ runtime (`/MT`), so they run on any Windows
10/11 PC without installing the Visual C++ Redistributable.

If Visual Studio offers to "retarget" the projects to a newer toolset or SDK,
accept it.

## Get yt-dlp and ffmpeg

```
powershell -ExecutionPolicy Bypass -File packaging\fetch_tools.ps1
```

This puts them in `packaging\thirdparty\` (not in git). Build again and the
post-build step copies them to `build\vs\x64\<Configuration>\tools\`.

## Try your build

Run `build\vs\x64\Release\PodcastMP3.exe` once. It registers itself with Edge.
Then, in `edge://extensions`, **Load unpacked** either
`build\vs\x64\Release\extension` (to test this build) or keep using
`C:\PodcastMP3\extension` from a release.

Only one copy is connected to Edge at a time: the last `PodcastMP3.exe` you
started. Start `C:\PodcastMP3\PodcastMP3.exe` again to switch back.

## Make the release ZIP from a Visual Studio build

```
powershell -ExecutionPolicy Bypass -File packaging\make_zip.ps1
```

Creates `dist\PodcastMP3-v<version>-win64.zip`, the same layout as the
official release.

## Changing the version

Edit `VERSION`, then run `python scripts\set_version.py` (updates
`src\core\version.h` and `extension\manifest.json`). If you add or remove
source files, update the list in `scripts\make_vs_projects.py` and run it (and
`CMakeLists.txt`).

## Alternative: CMake

Visual Studio can also open the folder directly (**File > Open > Folder**) and
build from `CMakeLists.txt`.
