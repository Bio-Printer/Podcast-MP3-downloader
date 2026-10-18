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
- On rumble.com, MIDDLE-CLICK a playing video, or the thumbnail of any
  video, to save its MP3. A small window shows progress and closes itself
  when done.
- Other ways: right-click > "Download MP3 of this video", the toolbar
  button's "Download MP3 of this tab", or Alt+Shift+M.
- Or paste a link into PodcastMP3.exe directly (no Edge needed).
- MP3 files go to   C:\Tmp\Audio   (change it in PodcastMP3.exe), named
  like   2026-10-15 Channel - Title (1.5x).mp3   (the video's publish date;
  today's date if the page does not give one).
- Copy the MP3 files to your phone and play them with any media player.


UPDATING TO A NEW VERSION
-------------------------
1. Close any open Podcast MP3 windows.
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
