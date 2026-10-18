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
    '    { name: "Test", host: /^127\\.0\\.0\\.1$/, videoUrl: /^http:\\/\\/127\\.0\\.0\\.1:\\d+\\/v[a-z0-9]+-[^\\/?#]*\\.html/i },\n  ];'
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

// 4. Middle-click the player on a video page.
const page = await context.newPage();
await page.goto(`${base}/v6test-episode-7.html`);
const box = await page.locator("video").boundingBox();
await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2, { button: "middle" });
await page.waitForTimeout(1500);
await page.screenshot({ path: path.join(shots, "1-middle-click-toast.png") });
let files = await waitForMp3(1);
console.log("MP3 #1:", files);

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
const jobsDirPath = path.join(home, ".local", "share", "podcastmp3", "jobs");
const vols = fs.readdirSync(jobsDirPath).filter((f) => f.endsWith(".json"))
  .map((f) => JSON.parse(fs.readFileSync(path.join(jobsDirPath, f), "utf8"))).sort((a, b) => a.id.localeCompare(b.id)).map((j) => j.volume);
console.log("job volumes:", vols);
if (JSON.stringify(vols) !== "[1.25,1.5]") fail("unexpected job volumes " + JSON.stringify(vols));

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

await context.close();
fs.rmSync(link, { force: true });
console.log(JSON.stringify({ outDir, files }));
console.log("E2E OK");
