#!/bin/bash
# Downloads the Windows builds of yt-dlp and ffmpeg that are bundled in the
# release ZIP into packaging/thirdparty/ (not committed to git).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/thirdparty"
mkdir -p "$out"
if [ ! -f "$out/yt-dlp/yt-dlp.exe" ] || [ "${REFRESH:-0}" = 1 ]; then
  # The unpacked "folder" build: starts much faster than the single-file exe.
  rm -rf "$out/yt-dlp" && mkdir -p "$out/yt-dlp"
  curl -fsSL -o "$out/yt-dlp_win.zip" https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_win.zip
  unzip -q -o "$out/yt-dlp_win.zip" -d "$out/yt-dlp" && rm "$out/yt-dlp_win.zip"
  curl -fsSL -o "$out/yt-dlp-LICENSE.txt" https://raw.githubusercontent.com/yt-dlp/yt-dlp/master/LICENSE
fi
if [ ! -f "$out/ffmpeg.exe" ] || [ "${REFRESH:-0}" = 1 ]; then
  tmp="$(mktemp -d)"
  curl -fsSL -o "$tmp/ff.zip" https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip
  unzip -q -o -j "$tmp/ff.zip" '*/bin/ffmpeg.exe' '*/LICENSE.txt' -d "$tmp"
  mv "$tmp/ffmpeg.exe" "$out/ffmpeg.exe"
  mv "$tmp/LICENSE.txt" "$out/ffmpeg-LICENSE.txt"
  rm -rf "$tmp"
fi
if [ ! -f "$out/deno.exe" ] || [ "${REFRESH:-0}" = 1 ]; then
  # JavaScript runtime yt-dlp needs for YouTube.
  tmp="$(mktemp -d)"
  curl -fsSL -o "$tmp/deno.zip" https://github.com/denoland/deno/releases/latest/download/deno-x86_64-pc-windows-msvc.zip
  unzip -q -o "$tmp/deno.zip" deno.exe -d "$tmp"
  mv "$tmp/deno.exe" "$out/deno.exe"
  curl -fsSL -o "$out/deno-LICENSE.txt" https://raw.githubusercontent.com/denoland/deno/main/LICENSE.md
  rm -rf "$tmp"
fi
ls -la "$out"
