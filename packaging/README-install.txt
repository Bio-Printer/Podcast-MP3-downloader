PODCAST MP3 DOWNLOADER - INSTALL AND UPDATE
===========================================

Saves the audio of a Rumble video as an MP3 file (stereo, 128-192 kbps) at the
playback speed you choose (default 1.5x, pitch kept natural), with title,
channel, date, cover picture and source web address stored in the MP3.

The exact version of this package is in VERSION.txt.


FIRST INSTALL (once)
--------------------
1. Right-click the downloaded ZIP > Properties > tick "Unblock" > OK.
   (Otherwise Windows may warn about every program in it.)
2. Extract the ZIP into the folder you want to keep the program in, e.g.
       C:\Tmp\PodcastMP3
   The ZIP has no folder of its own inside, so the files land directly
   there. Use the same folder for every future version.
3. Open that folder and double-click PodcastMP3.exe once.
   This connects the helper to Edge. The window's bottom line should say
   "Edge link OK". If Windows SmartScreen appears, click
   "More info" > "Run anyway" (the program is not code-signed).
4. In Edge, go to   edge://extensions
   - Turn on "Developer mode" (left side or bottom).
   - Click "Load unpacked" and choose the "extension" folder inside it,
     e.g.   C:\Tmp\PodcastMP3\extension
   - Optional: click the puzzle-piece icon on the toolbar and pin
     "Podcast MP3 Downloader" so its button is always visible.


USING IT
--------
- Click the toolbar button to choose the speed (1x ... 2.5x, default 1.5x),
  the volume boost (slider, 1x ... 2x, default 1.25x) and quality.
  Your choices are remembered.
- On rumble.com or youtube.com, MIDDLE-CLICK a playing video, or the
  thumbnail of any video, to save its MP3. This also works on a YouTube
  player embedded in another web site (a news article, a blog). A small window shows progress and closes itself
  when done.
- Other ways: right-click > "Download MP3 of this video", the toolbar
  button's "Download MP3 of this tab", or Alt+Shift+M. On a page that only
  has an embedded player, "Download MP3 of this tab" finds the player; if
  the page has several, a box lists them by size so you can pick one (the
  selected one is outlined in green).
- The progress window closes itself 30 seconds after finishing. Click
  anywhere in it to keep it open.
- Or paste a link into PodcastMP3.exe directly (no Edge needed).
- MP3 files go to   C:\Tmp\Audio   (change it in PodcastMP3.exe), named
  like   2026-10-15 Channel - Title (1.5x).mp3   (the video's publish date;
  today's date if the page does not give one).
- Copy the MP3 files to your phone and play them with any media player.


UPDATING TO A NEW VERSION
-------------------------
1. Close every Podcast MP3 window: the main PodcastMP3.exe window and any
   progress window (bottom-right of the screen). A running PodcastMP3.exe
   can't be replaced, and the unzip program will say "being used by another
   process". Edge can stay open; it does not need to be restarted.
2. Unblock the new ZIP (step 1 above) and extract it into the same folder
   again. Choose "Replace the files in the destination" when Windows asks.
3. That's it. The Edge extension notices the new files and reloads itself
   within a few minutes (or right away when you click its toolbar button).
   No uninstall, no new "Load unpacked".


CHECKING WHICH VERSION IS RUNNING
---------------------------------
- Toolbar button popup: shows "Extension v..." and "Helper v..." with the
  build number, and "matched" when both are from the same release.
- edge://extensions shows the same version text under the extension name.
- PodcastMP3.exe: version in the title bar, the bottom line and "About".
- VERSION.txt in the program folder.
- Command line: podcastmp3-host.exe --version  (in the program folder)


SWITCHING BETWEEN THE OFFICIAL BUILD AND YOUR OWN BUILD
------------------------------------------------------
Edge uses the copy of Podcast MP3 whose PodcastMP3.exe was started last.
- Official release ("Claude build"): close all Podcast MP3 windows, then
  start PodcastMP3.exe in this folder (the root of the unpacked ZIP) once.
- Your Visual Studio build ("EDN - Built"): close all Podcast MP3 windows,
  then start PodcastMP3.exe from your build folder once, for example
  source\build\vs\x64\Release\. That folder needs a copy of this
  folder's "tools" folder next to the program.
Unpacking a new ZIP does not switch builds by itself. Check which one is
active in the Edge toolbar popup ("Helper v... · Claude build" or
"· EDN - Built") or in PodcastMP3.exe's title bar.
Details: source\README.md, section "Switching between the official build
and your own build".


IF SOMETHING GOES WRONG
-----------------------
- "Helper not connected" in the popup: run PodcastMP3.exe once.
- A download fails: click "Show log" in the progress window (logs are in
  %LOCALAPPDATA%\PodcastMP3\logs). Sites change; "Update yt-dlp" in the
  popup or in PodcastMP3.exe usually fixes it.
- Uninstall: in edge://extensions click Remove, run
  "podcastmp3-host.exe unregister" from the program folder, delete the folder.

SOURCE CODE
-----------
The complete source of this exact build is in the "source" folder
(open source\PodcastMP3.sln in Visual Studio 2026; see
source\docs\BUILD-VS2026.md). Also at
https://github.com/Bio-Printer/Podcast-MP3-downloader
