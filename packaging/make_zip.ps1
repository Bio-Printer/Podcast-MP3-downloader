# Packs a Visual Studio Release build into the same overlay ZIP that the
# Linux build script produces.
#   powershell -ExecutionPolicy Bypass -File packaging\make_zip.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$version = (Get-Content "$root\VERSION" -Raw).Trim()
$bin = "$root\build\vs\x64\Release"
$tp = "$root\packaging\thirdparty"
foreach ($f in "$bin\PodcastMP3.exe", "$bin\podcastmp3-host.exe", "$tp\yt-dlp\yt-dlp.exe", "$tp\ffmpeg.exe", "$tp\deno.exe") {
    if (-not (Test-Path $f)) { throw "Missing $f - build Release|x64 and run packaging\fetch_tools.ps1 first." }
}
$stage = "$root\dist\stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$stage\tools", "$stage\licenses" | Out-Null
Copy-Item "$bin\PodcastMP3.exe", "$bin\podcastmp3-host.exe" $stage
Copy-Item "$tp\ffmpeg.exe", "$tp\deno.exe" "$stage\tools"
Copy-Item "$tp\yt-dlp" "$stage\tools\yt-dlp" -Recurse
Copy-Item "$root\extension" "$stage\extension" -Recurse
Copy-Item "$tp\ffmpeg-LICENSE.txt", "$tp\yt-dlp-LICENSE.txt", "$tp\deno-LICENSE.txt" "$stage\licenses"
Copy-Item "$root\third_party\nlohmann\LICENSE.MIT" "$stage\licenses\nlohmann-json-LICENSE.txt"
Copy-Item "$root\LICENSE" "$stage\licenses\PodcastMP3-LICENSE.txt"
Copy-Item "$root\packaging\README-install.txt" "$stage\README.txt"
Copy-Item "$root\CHANGELOG.md" "$stage\CHANGELOG.txt"
# Full source tree in source\ (files git tracks, plus new files not ignored).
Push-Location $root
git ls-files --cached --others --exclude-standard | ForEach-Object {
    $dest = Join-Path "$stage\source" $_
    New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
    Copy-Item $_ $dest
}
Pop-Location
& "$bin\podcastmp3-host.exe" --version | Out-File "$stage\VERSION.txt" -Encoding utf8
$zip = "$root\dist\PodcastMP3-v$version-win64.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "Created $zip"
