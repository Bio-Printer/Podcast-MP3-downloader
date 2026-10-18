# Downloads the Windows builds of yt-dlp and ffmpeg into packaging\thirdparty\
# (used by the Visual Studio post-build step and by make_zip.ps1).
#   powershell -ExecutionPolicy Bypass -File packaging\fetch_tools.ps1
$ErrorActionPreference = "Stop"
$out = Join-Path $PSScriptRoot "thirdparty"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$ProgressPreference = "SilentlyContinue"
Invoke-WebRequest "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe" -OutFile "$out\yt-dlp.exe"
Invoke-WebRequest "https://raw.githubusercontent.com/yt-dlp/yt-dlp/master/LICENSE" -OutFile "$out\yt-dlp-LICENSE.txt"
$tmp = Join-Path $env:TEMP ("pmp3-ffmpeg-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
Invoke-WebRequest "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip" -OutFile "$tmp\ff.zip"
Expand-Archive "$tmp\ff.zip" -DestinationPath $tmp
$dir = Get-ChildItem $tmp -Directory | Select-Object -First 1
Copy-Item "$($dir.FullName)\bin\ffmpeg.exe" "$out\ffmpeg.exe" -Force
Copy-Item "$($dir.FullName)\LICENSE.txt" "$out\ffmpeg-LICENSE.txt" -Force
Remove-Item $tmp -Recurse -Force
Get-ChildItem $out
