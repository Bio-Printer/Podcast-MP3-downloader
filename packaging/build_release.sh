#!/bin/bash
# Builds a complete Windows release on Linux, as ONE zip:
#   dist/PodcastMP3-v<version>-win64.zip
# Its files sit at the top level (no wrapper folder), so extracting it into the
# install folder (e.g. C:\Tmp\PodcastMP3) updates everything in place. The
# full source tree of the build is inside it in source\.
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
rm -rf "$stage" && mkdir -p "$stage/tools" "$stage/licenses" "$stage/source"
cp build/win/PodcastMP3.exe build/win/podcastmp3-host.exe "$stage/"
cp packaging/thirdparty/yt-dlp.exe packaging/thirdparty/ffmpeg.exe packaging/thirdparty/deno.exe "$stage/tools/"
cp -r extension "$stage/extension"
cp packaging/thirdparty/ffmpeg-LICENSE.txt "$stage/licenses/ffmpeg-LICENSE.txt"
cp packaging/thirdparty/yt-dlp-LICENSE.txt "$stage/licenses/yt-dlp-LICENSE.txt"
cp packaging/thirdparty/deno-LICENSE.txt "$stage/licenses/deno-LICENSE.txt"
cp third_party/nlohmann/LICENSE.MIT "$stage/licenses/nlohmann-json-LICENSE.txt"
cp LICENSE "$stage/licenses/PodcastMP3-LICENSE.txt"

ytv="$(LANG=C.UTF-8 WINEDEBUG=-all wine packaging/thirdparty/yt-dlp.exe --version 2>/dev/null | tr -d '\r' || echo unknown)"
ffv="$(LANG=C.UTF-8 WINEDEBUG=-all wine packaging/thirdparty/ffmpeg.exe -hide_banner -version 2>/dev/null | head -1 | tr -d '\r' || echo unknown)"
{
  echo "Podcast MP3 Downloader $full"
  echo "Source: https://github.com/Bio-Printer/Podcast-MP3-downloader (tag v$version)"
  echo "Bundled yt-dlp: $ytv"
  echo "Bundled ffmpeg: $ffv"
  echo "Bundled deno:   $(LANG=C.UTF-8 WINEDEBUG=-all wine packaging/thirdparty/deno.exe --version 2>/dev/null | head -1 | tr -d '\r' || echo unknown)"
} | sed 's/$/\r/' > "$stage/VERSION.txt"
sed 's/$/\r/' packaging/README-install.txt > "$stage/README.txt"
sed 's/$/\r/' CHANGELOG.md > "$stage/CHANGELOG.txt"

# The full source tree of this build goes inside the same ZIP, in "source\"
# (a fixed name, so unzipping a newer release over the old one replaces it).
git ls-files -z --cached --others --exclude-standard | xargs -0 -I{} cp --parents {} "$stage/source/"

mkdir -p dist
zipname="dist/PodcastMP3-v$version-win64.zip"
rm -f "$zipname"
(cd "$stage" && zip -q -r -9 "../../$zipname" .)
ls -la "$zipname"
cat "$stage/VERSION.txt"
