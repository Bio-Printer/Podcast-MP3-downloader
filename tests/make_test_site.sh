#!/bin/bash
# Creates a tiny local "video site" for the end-to-end test and serves it on
# http://127.0.0.1:8765 (Ctrl+C to stop).
set -euo pipefail
dir="${1:-/tmp/pmp3-site}"
mkdir -p "$dir" && cd "$dir"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc=size=640x360:rate=25" \
  -f lavfi -i "sine=frequency=440:sample_rate=44100,volume=0.5" -t 30 \
  -c:v libx264 -preset ultrafast -c:a aac -b:a 128k -shortest episode.mp4
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc=size=1280x720" -frames:v 1 thumb.jpg
cat > v6test-episode-7.html <<'HTML'
<!doctype html><html><head><meta charset="utf-8">
<title>Test Episode 7: Ünïcode &amp; Friends</title>
<meta property="og:title" content="Test Episode 7: Ünïcode &amp; Friends">
<meta property="og:image" content="http://127.0.0.1:8765/thumb.jpg">
</head><body><h1>Test Episode</h1>
<video controls width="640" src="http://127.0.0.1:8765/episode.mp4"></video>
</body></html>
HTML
cp v6test-episode-7.html episode.html
cat > list.html <<'HTML'
<!doctype html><html><head><meta charset="utf-8"><title>Channel videos</title></head><body>
<h1>Latest</h1>
<a class="thumb" href="/v6test-episode-7.html" title="Test Episode 7"><img src="/thumb.jpg" width="320" height="180"></a>
</body></html>
HTML
cat > news.html <<'HTML'
<!doctype html><html><head><meta charset="utf-8"><title>News article with a YouTube video</title></head><body>
<h1>Some news article</h1><p>Text...</p>
<iframe width="200" height="113" src="https://www.youtube.com/embed/aaaaaaaaaaa?rel=0" title="small related video"></iframe>
<iframe width="640" height="360" src="https://www.youtube.com/embed/jNQXAC9IVRw?feature=oembed" title="main video" allowfullscreen></iframe>
</body></html>
HTML
exec python3 -m http.server 8765 --bind 127.0.0.1
