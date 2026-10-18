# Downloads the Windows builds of yt-dlp and ffmpeg into packaging\thirdparty\
# (used by the Visual Studio post-build step and by make_zip.ps1).
#   powershell -ExecutionPolicy Bypass -File packaging\fetch_tools.ps1
$ErrorActionPreference = "Stop"
$out = Join-Path $PSScriptRoot "thirdparty"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$ProgressPreference = "SilentlyContinue"
# The unpacked "folder" build of yt-dlp: starts much faster than the single-file exe.
Invoke-WebRequest "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_win.zip" -OutFile "$out\yt-dlp_win.zip"
if (Test-Path "$out\yt-dlp") { Remove-Item "$out\yt-dlp" -Recurse -Force }
Expand-Archive "$out\yt-dlp_win.zip" -DestinationPath "$out\yt-dlp"
Remove-Item "$out\yt-dlp_win.zip"
Invoke-WebRequest "https://raw.githubusercontent.com/yt-dlp/yt-dlp/master/LICENSE" -OutFile "$out\yt-dlp-LICENSE.txt"
$tmp = Join-Path $env:TEMP ("pmp3-ffmpeg-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
Invoke-WebRequest "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip" -OutFile "$tmp\ff.zip"
Expand-Archive "$tmp\ff.zip" -DestinationPath $tmp
$dir = Get-ChildItem $tmp -Directory | Select-Object -First 1
Copy-Item "$($dir.FullName)\bin\ffmpeg.exe" "$out\ffmpeg.exe" -Force
Copy-Item "$($dir.FullName)\LICENSE.txt" "$out\ffmpeg-LICENSE.txt" -Force
Remove-Item $tmp -Recurse -Force
# JavaScript runtime yt-dlp needs for YouTube.
$tmp = Join-Path $env:TEMP ("pmp3-deno-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
Invoke-WebRequest "https://github.com/denoland/deno/releases/latest/download/deno-x86_64-pc-windows-msvc.zip" -OutFile "$tmp\deno.zip"
Expand-Archive "$tmp\deno.zip" -DestinationPath $tmp
Copy-Item "$tmp\deno.exe" "$out\deno.exe" -Force
Invoke-WebRequest "https://raw.githubusercontent.com/denoland/deno/main/LICENSE.md" -OutFile "$out\deno-LICENSE.txt"
Remove-Item $tmp -Recurse -Force
Get-ChildItem $out
