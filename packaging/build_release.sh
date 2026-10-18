#!/bin/bash
# Builds a complete Windows release on Linux:
#   dist/PodcastMP3-v<version>-win64.zip   unzip over C:\PodcastMP3 to install/update
#   dist/PodcastMP3-v<version>-source.zip  the full source tree of this release
#
# Needs: cmake, x86_64-w64-mingw32-g++-posix (apt: g++-mingw-w64-x86-64-posix),
#        python3, zip, git; wine is optional (runs the unit tests on Windows code).
#   BUILD_ID=20261018-1500 packaging/build_release.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

full="$(python3 scripts/set_version.py ${BUILD_ID:+--build "$BUILD_ID"})"
version="$(cat VERSION)"
echo "== Building Podcast MP3 Downloader $full"

packaging/fetch_tools.sh >/dev/null

cmake -S . -B build/win -DCMAKE_TOOLCHAIN_FILE=packaging/mingw-toolchain.cmake -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/win -j"$(nproc)"
x86_64-w64-mingw32-strip build/win/PodcastMP3.exe build/win/podcastmp3-host.exe

if command -v wine >/dev/null; then
  echo "== Unit tests (Windows build, under wine)"
  LANG=C.UTF-8 WINEDEBUG=-all wine build/win/pmp3tests.exe
fi

stage="dist/stage"
rm -rf "$stage" && mkdir -p "$stage/tools" "$stage/licenses"
cp build/win/PodcastMP3.exe build/win/podcastmp3-host.exe "$stage/"
cp packaging/thirdparty/yt-dlp.exe packaging/thirdparty/ffmpeg.exe "$stage/tools/"
cp -r extension "$stage/extension"
cp packaging/thirdparty/ffmpeg-LICENSE.txt "$stage/licenses/ffmpeg-LICENSE.txt"
cp packaging/thirdparty/yt-dlp-LICENSE.txt "$stage/licenses/yt-dlp-LICENSE.txt"
cp third_party/nlohmann/LICENSE.MIT "$stage/licenses/nlohmann-json-LICENSE.txt"
cp LICENSE "$stage/licenses/PodcastMP3-LICENSE.txt"

ytv="$(LANG=C.UTF-8 WINEDEBUG=-all wine packaging/thirdparty/yt-dlp.exe --version 2>/dev/null | tr -d '\r' || echo unknown)"
ffv="$(LANG=C.UTF-8 WINEDEBUG=-all wine packaging/thirdparty/ffmpeg.exe -hide_banner -version 2>/dev/null | head -1 | tr -d '\r' || echo unknown)"
{
  echo "Podcast MP3 Downloader $full"
  echo "Source: https://github.com/Bio-Printer/Podcast-MP3-downloader (tag v$version)"
  echo "Bundled yt-dlp: $ytv"
  echo "Bundled ffmpeg: $ffv"
} | sed 's/$/\r/' > "$stage/VERSION.txt"
sed 's/$/\r/' packaging/README-install.txt > "$stage/README.txt"
sed 's/$/\r/' CHANGELOG.md > "$stage/CHANGELOG.txt"

mkdir -p dist
zipname="dist/PodcastMP3-v$version-win64.zip"
rm -f "$zipname"
(cd "$stage" && zip -q -r -9 "../../$zipname" .)
# Source ZIP = every file git tracks or would track (respects .gitignore),
# taken from the working tree so it matches exactly what was just built.
srcname="dist/PodcastMP3-v$version-source.zip"
rm -f "$srcname"
srcstage="dist/src-stage/PodcastMP3-v$version-source"
rm -rf dist/src-stage && mkdir -p "$srcstage"
git ls-files -z --cached --others --exclude-standard | xargs -0 -I{} cp --parents {} "$srcstage/"
(cd dist/src-stage && zip -q -r -9 "../../$srcname" .)
rm -rf dist/src-stage
ls -la dist/*.zip
cat "$stage/VERSION.txt"
