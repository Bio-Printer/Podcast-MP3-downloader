// End-to-end test on Linux: Chromium + the extension + the Linux helper.
//
//   node tests/e2e_extension.mjs <build dir with podcastmp3-host> <site dir served on :8765>
//
// The site dir must contain episode.mp4 / thumb.jpg (see tests/make_test_site.sh).
// Rumble itself is replaced by a local page: the test copies the extension and
// adds 127.0.0.1 as an extra "site" so the content script runs there.
import { execFileSync } from "node:child_process";
import { createRequire } from "node:module";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

// Playwright may be installed globally (set PLAYWRIGHT_MODULE to its folder).
const { chromium } = createRequire(import.meta.url)(process.env.PLAYWRIGHT_MODULE || "playwright");

const buildDir = path.resolve(process.argv[2] || "build/linux");
const repo = path.resolve(path.dirname(new URL(import.meta.url).pathname), "..");
const base = "http://127.0.0.1:8765";
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "pmp3-e2e-"));
const home = path.join(tmp, "home");
const userData = path.join(tmp, "profile");
const ext = path.join(tmp, "extension");
const shots = process.env.PMP3_SHOTS || tmp;
fs.mkdirSync(home, { recursive: true });

function fail(msg) {
  console.error("E2E FAIL:", msg);
  process.exit(1);
}

// 1. Extension copy that also runs on the local test site.
fs.cpSync(path.join(repo, "extension"), ext, { recursive: true });
const manifest = JSON.parse(fs.readFileSync(path.join(ext, "manifest.json"), "utf8"));
manifest.host_permissions.push("http://127.0.0.1/*");
manifest.content_scripts[0].matches.push("http://127.0.0.1/*");
fs.writeFileSync(path.join(ext, "manifest.json"), JSON.stringify(manifest, null, 2));
const sites = fs
  .readFileSync(path.join(ext, "sites.js"), "utf8")
  .replace(
    "  ];",
    '    { name: "Test", host: /^127\\.0\\.0\\.1$/, cookieDomain: "127.0.0.1", videoUrl: /^http:\\/\\/127\\.0\\.0\\.1:\\d+\\/v[a-z0-9]+-[^\\/?#]*\\.html/i },\n  ];'
  );
fs.writeFileSync(path.join(ext, "sites.js"), sites);

// The helper reports the version of <its folder>/extension/manifest.json.
const link = path.join(buildDir, "extension");
try { fs.rmSync(link, { recursive: true, force: true }); } catch {}
fs.symlinkSync(ext, link);

// 2. Register the native messaging host inside the test profile.
const env = { ...process.env, HOME: home, PMP3_EXTRA_NMH_DIR: path.join(userData, "NativeMessagingHosts"),
              no_proxy: "127.0.0.1,localhost", NO_PROXY: "127.0.0.1,localhost" };
console.log(execFileSync(path.join(buildDir, "podcastmp3-host"), ["register"], { env }).toString().trim());

// 3. Browser with the extension.
// Load it as an *unpacked* extension, exactly like Edge's "Load unpacked"
// button (extensions loaded with --load-extension cannot reload themselves).
const context = await chromium.launchPersistentContext(userData, {
  channel: "chromium",
  headless: true,
  env,
  ignoreDefaultArgs: ["--disable-extensions"],
  args: ["--enable-unsafe-extension-debugging"],
  viewport: { width: 1100, height: 760 },
});
const cdp = await context.browser().newBrowserCDPSession();
const { id: extId } = await cdp.send("Extensions.loadUnpacked", { path: ext });
console.log("extension id", extId);
if (extId !== "dbnhcdhaapecgdcdniigdkgdbhacblhm") fail("unexpected extension id " + extId);

const outDir = path.join(home, "Tmp", "Audio");
async function waitForMp3(count, timeoutMs = 120000) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    const files = fs.existsSync(outDir) ? fs.readdirSync(outDir).filter((f) => f.endsWith(".mp3")) : [];
    if (files.length >= count) return files;
    await new Promise((r) => setTimeout(r, 500));
  }
  fail("timed out waiting for MP3 #" + count);
}

// A "Cloudflare" cookie the extension must hand over to yt-dlp.
await context.addCookies([{ name: "cf_clearance", value: "secret-test-value", domain: "127.0.0.1", path: "/" }]);

// 4. Middle-click the player on a video page.
const page = await context.newPage();
await page.goto(`${base}/v6test-episode-7.html`);
const box = await page.locator("video").boundingBox();
await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2, { button: "middle" });
await page.waitForTimeout(1500);
await page.screenshot({ path: path.join(shots, "1-middle-click-toast.png") });
let files = await waitForMp3(1);
console.log("MP3 #1:", files);
{
  const logs = path.join(home, ".local/share/podcastmp3/logs");
  const first = fs.readdirSync(logs).sort()[0];
  const text = fs.readFileSync(path.join(logs, first), "utf8");
  const used = /Using 1 browser cookie\(s\) handed over by Edge/.test(text) && /--cookies /.test(text) && /--user-agent /.test(text);
  const leaked = text.includes("secret-test-value");
  const leftover = fs.readdirSync(path.join(home, ".local/share/podcastmp3/jobs")).filter((f) => /\.(cookies\.txt|ua)$/.test(f));
  console.log("cookies handed over:", used, "| value in log:", leaked, "| leftover cookie files:", leftover.length);
  if (!used || leaked || leftover.length) fail("browser cookies not handed over safely");
}

// 5. Popup: versions and the finished job.
const popup = await context.newPage();
await popup.setViewportSize({ width: 380, height: 640 });
await popup.goto(`chrome-extension://${extId}/popup.html`);
await popup.waitForTimeout(2500);
const versions = await popup.locator("#versions").innerText();
console.log("popup versions:", versions.replace(/\n/g, " | "));
if (!/matched/.test(versions)) fail("popup does not show matched versions");
await popup.screenshot({ path: path.join(shots, "2-popup.png") });

// 6. Choose 2x in the popup, then middle-click a thumbnail link on a list page.
await popup.locator("#speeds button", { hasText: /^2x$/ }).click();
const vol = await popup.locator("#volumeValue").innerText();
if (vol !== "1.25x") fail("volume slider default is " + vol);
await popup.locator("#volume").fill("1.5");
await popup.locator("#volume").dispatchEvent("change");
await page.goto(`${base}/list.html`);
const pagesBefore = context.pages().length;
await page.locator("a.thumb").click({ button: "middle" });
await page.waitForTimeout(1000);
if (context.pages().length !== pagesBefore) fail("middle-click on the link also opened a new tab");
files = await waitForMp3(2);
console.log("MP3 #2:", files);
await popup.reload();
await popup.waitForTimeout(2500);
await popup.screenshot({ path: path.join(shots, "3-popup-two-jobs.png") });
const jobsText = await popup.locator("#jobs").innerText();
console.log("popup jobs:", jobsText.replace(/\n/g, " | "));
// The "MP3 ready" notification is shown and then removed after 30 s.
const notes = () => popup.evaluate(() => new Promise((r) => chrome.notifications.getAll((n) => r(Object.keys(n)))));
let shown = [];
for (let i = 0; i < 10 && !shown.length; i++) { shown = await notes(); if (!shown.length) await popup.waitForTimeout(500); }
console.log("notifications shown:", shown.length);
if (!shown.length) fail("no completion notification");
await popup.waitForTimeout(33000);
const left = await notes();
console.log("notifications after 33 s:", left.length);
if (left.length) fail("notification was not removed after 30 s");
const jobsDirPath = path.join(home, ".local", "share", "podcastmp3", "jobs");
const vols = fs.readdirSync(jobsDirPath).filter((f) => f.endsWith(".json"))
  .map((f) => JSON.parse(fs.readFileSync(path.join(jobsDirPath, f), "utf8"))).sort((a, b) => a.id.localeCompare(b.id)).map((j) => j.volume);
console.log("job volumes:", vols);
if (JSON.stringify(vols) !== "[1.25,1.5]") fail("unexpected job volumes " + JSON.stringify(vols));

// 6b. A news page with two embedded YouTube players: the toolbar button
// shows a chooser on the page (largest first, outlined); picking the second
// one downloads exactly that video.
await page.goto(`${base}/news.html`);
await page.bringToFront();
const sendReply = await popup.evaluate(() => new Promise((r) => chrome.runtime.sendMessage({ type: "sendTab" }, r)));
console.log("embedded page ->", JSON.stringify(sendReply));
if (!sendReply.ok || !sendReply.choosing || sendReply.count !== 2) fail("expected a chooser for 2 videos");
await page.waitForTimeout(800);
const chooser = await page.evaluate(() => {
  const h = document.getElementById("pmp3-chooser");
  if (!h) return null;
  const opts = [...h.shadowRoot.querySelectorAll("li")].map((o) => o.textContent);
  const outlined = [...document.querySelectorAll("iframe")].map((f) => f.style.outline);
  return { opts, outlined };
});
console.log("chooser:", JSON.stringify(chooser));
if (!chooser || !/^1(main video|Me at the zoo).*largest/.test(chooser.opts[0]) || !/^2small related video/.test(chooser.opts[1]))
  fail("chooser not shown or not sorted by size");
await page.screenshot({ path: path.join(shots, "5-chooser.png") });
const jobsBeforePick = fs.readdirSync(path.join(home, ".local/share/podcastmp3/jobs")).length;
await page.evaluate(() => {
  const r = document.getElementById("pmp3-chooser").shadowRoot;
  r.querySelectorAll("li")[1].click(); // select the second (smaller) video
  r.querySelector(".go").click();
});
await page.waitForTimeout(2500);
const picked = fs.readdirSync(path.join(home, ".local/share/podcastmp3/jobs")).filter((f) => f.endsWith(".json"))
  .map((f) => JSON.parse(fs.readFileSync(path.join(home, ".local/share/podcastmp3/jobs", f), "utf8")))
  .sort((a, b) => b.id.localeCompare(a.id))[0];
console.log("picked ->", picked.url, "outline on:", await page.evaluate(() => [...document.querySelectorAll("iframe")].map((f) => f.style.outline || "-")));
if (picked.url !== "https://www.youtube.com/watch?v=aaaaaaaaaaa") fail("chooser did not download the selected video");

// 6c. X and Telegram. Their pages are imitated (same structure as the real
// sites) and served under the real addresses, so the real manifest matches.
const fakeSites = {
  "https://x.com/home": `<!doctype html><html><head><title>Home / X</title></head><body>
    <article><div>First post <a class="plainpost" href="/someone/status/333">link to a text post</a></div>
      <a href="/teameffujoe/status/2116654931448922495?s=20"><time>Oct 30</time></a>
      <div data-testid="videoPlayer"><video id="v1" width="480" height="270" src="${base}/episode.mp4"></video></div></article>
    <article><a href="/other/status/444"><time>Oct 29</time></a>
      <div data-testid="videoPlayer"><video id="v2" width="480" height="270" src="${base}/episode.mp4"></video></div></article>
    </body></html>`,
  "https://t.me/s/professor_patriot_official": `<!doctype html><html><head><title>Professor Patriot – Telegram</title></head><body>
    <div class="tgme_widget_message" data-post="professor_patriot_official/54104">
      <a class="tgme_widget_message_video_player" href="https://t.me/professor_patriot_official/54104">
        <video width="480" height="270" src="${base}/episode.mp4"></video></a>
      <div class="tgme_widget_message_text"><a class="plainpost" href="https://t.me/professor_patriot_official/54100">earlier post</a></div>
    </div></body></html>`,
  "https://platform.twitter.com/embed/Tweet.html?id=555": `<!doctype html><title>X post</title><p>Only text in this post.</p>`,
  "https://t.me/someone_else/777?embed=1": `<!doctype html><title>Telegram post</title><div data-post="someone_else/777">
    <a class="tgme_widget_message_video_player" href="https://t.me/someone_else/777"><video width="300" height="170"></video></a></div>`,
  [`${base}/posts-news.html`]: `<!doctype html><html><head><title>News with posts</title></head><body>
    <iframe width="500" height="300" src="https://platform.twitter.com/embed/Tweet.html?id=555" title="X post"></iframe>
    <iframe width="400" height="400" src="https://t.me/someone_else/777?embed=1" title="Telegram post"></iframe>
    </body></html>`,
};
await context.route(/^https:\/\/(x\.com|t\.me|platform\.twitter\.com)\/|posts-news\.html/, (route) => {
  const body = fakeSites[route.request().url()];
  if (body) route.fulfill({ status: 200, contentType: "text/html; charset=utf-8", body });
  else route.fulfill({ status: 404, body: "" });
});
const allJobs = () => fs.readdirSync(jobsDirPath).filter((f) => f.endsWith(".json"))
  .map((f) => JSON.parse(fs.readFileSync(path.join(jobsDirPath, f), "utf8"))).sort((a, b) => a.id.localeCompare(b.id));
async function middleClick(sel, p = page) {
  const b = await p.locator(sel).first().boundingBox();
  await p.mouse.click(b.x + b.width / 2, b.y + b.height / 2, { button: "middle" });
  await p.waitForTimeout(2500);
}
const newJobUrls = async (fn) => {
  const before = allJobs().length;
  await fn();
  return allJobs().slice(before).map((j) => j.url);
};
await page.goto("https://x.com/home");
let urls = await newJobUrls(() => middleClick("#v2"));
console.log("X timeline, 2nd video ->", urls);
if (JSON.stringify(urls) !== '["https://x.com/other/status/444"]') fail("X: wrong post for the clicked video");
urls = await newJobUrls(() => middleClick("#v1"));
console.log("X timeline, 1st video ->", urls);
if (JSON.stringify(urls) !== '["https://x.com/teameffujoe/status/2116654931448922495"]') fail("X: wrong post for the clicked video");
const tabsBefore = context.pages().length;
urls = await newJobUrls(() => middleClick("a.plainpost"));
console.log("X: middle-click on a link to a post ->", urls, "new tabs:", context.pages().length - tabsBefore);
if (urls.length) fail("X: a plain link to a post must not start a download");
await page.goto("https://t.me/s/professor_patriot_official");
urls = await newJobUrls(() => middleClick("video"));
console.log("Telegram channel view ->", urls);
if (JSON.stringify(urls) !== '["https://t.me/professor_patriot_official/54104"]') fail("Telegram: wrong post");
urls = await newJobUrls(() => middleClick("a.plainpost"));
if (urls.length) fail("Telegram: a plain link to a post must not start a download");
// Toolbar button on a page with an embedded text-only X post and an embedded
// Telegram video: no chooser, the Telegram video is sent straight away.
await page.goto(`${base}/posts-news.html`);
await page.bringToFront();
await page.waitForTimeout(800);
let embedReply;
urls = await newJobUrls(async () => {
  embedReply = await popup.evaluate(() => new Promise((r) => chrome.runtime.sendMessage({ type: "sendTab" }, r)));
  await page.waitForTimeout(1500);
});
console.log("page with embedded posts ->", JSON.stringify(embedReply), urls);
if (embedReply.choosing || JSON.stringify(urls) !== '["https://t.me/someone_else/777"]') fail("embedded posts: text-only X post was not skipped");
// Middle-click inside the embedded Telegram post (a frame) sends that post.
urls = await newJobUrls(async () => {
  const fr = page.frameLocator('iframe[title="Telegram post"]');
  const b = await fr.locator("video").boundingBox();
  await page.mouse.click(b.x + b.width / 2, b.y + b.height / 2, { button: "middle" });
  await page.waitForTimeout(2500);
});
console.log("middle-click in embedded Telegram post ->", urls);
if (JSON.stringify(urls) !== '["https://t.me/someone_else/777"]') fail("embedded Telegram post: middle-click did not send it");
await context.unroute(/^https:\/\/(x\.com|t\.me|platform\.twitter\.com)\/|posts-news\.html/);

// 6d. "Download MP4 video": the video itself, sped up, saved as an MP4.
await page.goto(`${base}/v6test-episode-7.html`);
await page.bringToFront();
const mp4Reply = await popup.evaluate(() => new Promise((r) => chrome.runtime.sendMessage({ type: "sendTab", format: "mp4" }, r)));
if (!mp4Reply.ok || mp4Reply.format !== "mp4") fail("MP4 button: job not started " + JSON.stringify(mp4Reply));
let mp4 = null;
for (let i = 0; i < 240 && !mp4; i++) {
  const f = fs.existsSync(outDir) ? fs.readdirSync(outDir).filter((n) => n.endsWith(".mp4")) : [];
  if (f.length) mp4 = f[0];
  else await page.waitForTimeout(500);
}
if (!mp4) fail("no MP4 file");
const probe = execFileSync("ffprobe", ["-v", "error", "-show_entries", "stream=codec_type:format=duration", "-of", "compact", path.join(outDir, mp4)]).toString();
console.log("MP4:", mp4, "|", probe.replace(/\n/g, " "));
if (!/codec_type=video/.test(probe) || !/codec_type=audio/.test(probe)) fail("MP4 lacks picture or sound");
const dur = Number((/duration=([\d.]+)/.exec(probe) || [])[1]);
if (!(Math.abs(dur - 15) < 0.5)) fail("MP4 at 2x should last 15 s, not " + dur);
await popup.reload();
await popup.waitForTimeout(2500);
await popup.screenshot({ path: path.join(shots, "6-popup-mp4.png") });
const mp4Line = await popup.locator("#jobs li").first().innerText();
console.log("popup MP4 job:", mp4Line.replace(/\n/g, " | "));
if (!/🎬/.test(mp4Line)) fail("popup does not mark the MP4 job");

// 6e. Right-click menu "Download MP4 video of this video" on a link (the
// menu's click handler is called directly; Playwright cannot open Edge's menu).
{
  await page.goto(`${base}/list.html`);
  const tabId = await popup.evaluate(() => new Promise((r) => chrome.tabs.query({ url: "http://127.0.0.1/*" }, (t) => r(t.find((x) => /list\.html/.test(x.url)).id))));
  const before = fs.readdirSync(outDir).filter((n) => n.endsWith(".mp4")).length;
  const [sw] = context.serviceWorkers();
  await sw.evaluate(async (tabId) => {
    const tab = await chrome.tabs.get(tabId);
    onMenuClick({ menuItemId: "pmp3-link-mp4", linkUrl: "http://127.0.0.1:8765/v6test-episode-7.html" }, tab);
  }, tabId);
  let n = before;
  for (let i = 0; i < 240 && n === before; i++) {
    await page.waitForTimeout(500);
    n = fs.readdirSync(outDir).filter((f) => f.endsWith(".mp4")).length;
  }
  const last = allJobs().pop();
  console.log("right-click MP4 on a link -> job format:", last.format, "| MP4 files:", n);
  if (n !== before + 1 || last.format !== "mp4") fail("right-click MP4 menu did not make an MP4");
}

// A video page that stays open while the extension updates itself (step 8).
await page.goto(`${base}/v6test-episode-7.html`);

// 7. "Unzip a new version over the old one": change the extension files on
// disk and check that the extension notices and reloads itself.
manifest.version = "0.1.99";
manifest.version_name = "0.1.99 (build e2e)";
fs.writeFileSync(path.join(ext, "manifest.json"), JSON.stringify(manifest, null, 2));
await popup.reload().catch(() => {});
await popup.waitForTimeout(1500);
await popup.screenshot({ path: path.join(shots, "4-popup-reloading.png") }).catch(() => {});
const t0 = Date.now();
let reloaded = null;
while (Date.now() - t0 < 20000 && !reloaded) {
  await new Promise((r) => setTimeout(r, 1000));
  const check = await context.newPage();
  try {
    await check.goto(`chrome-extension://${extId}/popup.html`);
    const v = await check.evaluate(() => chrome.runtime.getManifest().version_name);
    if (v === "0.1.99 (build e2e)") reloaded = v;
  } catch {}
  await check.close();
}
if (!reloaded) fail("extension did not reload itself after the files on disk changed");
console.log("self-reload OK ->", reloaded);

// 8. Middle-click on a page that was opened BEFORE the update (no page reload).
// The old page script is cut off from the extension; the new one must take over.
const jobsDir2 = path.join(home, ".local", "share", "podcastmp3", "jobs");
const countJobs = () => fs.readdirSync(jobsDir2).filter((f) => f.endsWith(".json")).length;
const before = countJobs();
await page.waitForTimeout(2000);
const vb = await page.locator("video").boundingBox();
await page.mouse.click(vb.x + vb.width / 2, vb.y + vb.height / 2, { button: "middle" });
await page.waitForTimeout(4000);
const toastText = await page.evaluate(() => {
  for (const d of document.querySelectorAll("div")) if (d.shadowRoot && d.shadowRoot.querySelector(".m")) return d.shadowRoot.querySelector(".m").textContent;
  return "";
});
console.log("after update: toast =", JSON.stringify(toastText), "new jobs =", countJobs() - before);
if (countJobs() - before !== 1) fail("middle-click after an extension update did not start exactly one job");

await context.close();
fs.rmSync(link, { force: true });
console.log(JSON.stringify({ outDir, files }));
console.log("E2E OK");
