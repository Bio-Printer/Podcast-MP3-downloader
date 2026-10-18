# Developing on Linux

The engine (`src/core`) and the command line helper build and run natively on
Linux; that is where most development and testing happens. The Windows
programs are cross-compiled with MinGW-w64 and tested under Wine.

## Packages (Ubuntu 24.04)

```
sudo apt install build-essential cmake ffmpeg python3-pil zip \
     g++-mingw-w64-x86-64-posix mingw-w64-tools wine64 xvfb
pip install yt-dlp mutagen
```

## Native build and tests

```
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux -j
build/linux/pmp3tests
build/linux/podcastmp3-host info https://rumble.com/v....html
build/linux/podcastmp3-host download https://rumble.com/v....html --speed 1.5 --out ~/tmp
```

## End-to-end test (Chromium + extension + helper)

`tests/make_test_site.sh` creates a local test page with a video and serves it
on port 8765. `tests/e2e_extension.mjs` then loads the extension unpacked in
Playwright's Chromium, middle-clicks the video and a thumbnail link, checks
the MP3 files, the popup's version display and the self-reload after the
extension files on disk change.

```
tests/make_test_site.sh /tmp/pmp3-site &
PLAYWRIGHT_MODULE=/path/to/node_modules/playwright node tests/e2e_extension.mjs build/linux
```

## Windows release build

```
BUILD_ID=$(date +%Y%m%d-%H%M) packaging/build_release.sh
```

Produces `dist/PodcastMP3-v<version>-win64.zip` (unzip over `C:\PodcastMP3`)
and `dist/PodcastMP3-v<version>-source.zip`.

## Releasing

1. Bump `VERSION`, add a `CHANGELOG.md` entry.
2. `packaging/build_release.sh` (stamps the version, builds, tests, zips).
3. Commit, tag `vX.Y.Z`, push, attach both ZIPs to the GitHub release.
