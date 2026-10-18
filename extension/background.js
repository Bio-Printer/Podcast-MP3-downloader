// Podcast MP3 Downloader - background service worker.
// Talks to the helper program (podcastmp3-host.exe) through native messaging.
/* global chrome, importScripts, PMP3Sites */
importScripts("sites.js");

const HOST = "com.bioprinter.podcastmp3";
const NOTIFY_SECONDS = 30; // how long the "MP3 ready" notification stays on screen
const DEFAULTS = { speed: 1.5, bitrateKbps: 192, volume: 1.25 };
const MANIFEST = chrome.runtime.getManifest();
const MY_VERSION = MANIFEST.version_name || MANIFEST.version;

function native(msg) {
  return new Promise((resolve) => {
    try {
      chrome.runtime.sendNativeMessage(HOST, msg, (reply) => {
        if (chrome.runtime.lastError) {
          resolve({ ok: false, notInstalled: true, error: chrome.runtime.lastError.message });
        } else {
          resolve(reply || { ok: false, error: "No reply from helper" });
        }
      });
    } catch (e) {
      resolve({ ok: false, error: String(e) });
    }
  });
}

async function prefs() {
  const p = await chrome.storage.local.get(DEFAULTS);
  return { speed: Number(p.speed) || 1.5, bitrateKbps: Number(p.bitrateKbps) || 192, volume: Number(p.volume) || 1.25 };
}

// ---- version check / self reload -------------------------------------------
// After a new ZIP is unpacked over the install folder, the helper reports
// the version of the extension files on disk. If that differs from what
// Edge is running, reload so Edge picks up the new files.
async function checkVersion(reply) {
  const ping = reply || (await native({ cmd: "ping" }));
  if (!ping.ok || !ping.extensionOnDisk) return ping;
  if (ping.extensionOnDisk === MY_VERSION) {
    await chrome.storage.local.remove("reloadAttemptFor");
    return ping;
  }
  const self = await chrome.management.getSelf();
  const { reloadAttemptFor } = await chrome.storage.local.get("reloadAttemptFor");
  ping.versionMismatch = true;
  // Only an unpacked ("development") install re-reads its files on reload,
  // and only try once per version so we never loop.
  if (self.installType === "development" && reloadAttemptFor !== ping.extensionOnDisk) {
    await chrome.storage.local.set({ reloadAttemptFor: ping.extensionOnDisk });
    ping.reloading = true;
    setTimeout(() => chrome.runtime.reload(), 1200);
  }
  return ping;
}

// ---- jobs ----------------------------------------------------------------
let pollTimer = null;
const known = {}; // jobId -> last state

async function startJob(url, title) {
  const p = await prefs();
  const reply = await native({ cmd: "start", url, title, speed: p.speed, bitrateKbps: p.bitrateKbps, volume: p.volume });
  if (reply.ok) {
    known[reply.jobId] = "queued";
    schedulePoll(1000);
  } else if (reply.notInstalled) {
    reply.error =
      "The Podcast MP3 helper program is not connected. Run PodcastMP3.exe once from its folder, then try again.";
  }
  return { ...reply, speed: p.speed };
}

function schedulePoll(ms) {
  clearTimeout(pollTimer);
  pollTimer = setTimeout(poll, ms);
}

async function poll() {
  const reply = await native({ cmd: "jobs", max: 10 });
  if (!reply.ok) return;
  let active = 0;
  for (const j of reply.jobs) {
    const before = known[j.id];
    const final = j.state === "done" || j.state === "failed" || j.state === "cancelled";
    if (!final) active++;
    if (before && before !== j.state && final) notifyFinished(j);
    if (before || !final) known[j.id] = j.state;
  }
  chrome.action.setBadgeBackgroundColor({ color: "#34a046" });
  chrome.action.setBadgeText({ text: active ? String(active) : "" });
  if (active) schedulePoll(2000);
}

function notifyFinished(j) {
  const ok = j.state === "done";
  chrome.notifications.create("job:" + j.id, {
    type: "basic",
    iconUrl: "icons/icon128.png",
    title: ok ? "MP3 ready" : j.state === "cancelled" ? "Download cancelled" : "MP3 download failed",
    message: (j.title || j.url) + (ok ? "\n" + j.message : "\n" + (j.error || "")),
    priority: ok ? 0 : 1,
    // Windows would hide it after ~5 s; keep it up and remove it after 30 s.
    requireInteraction: true,
  });
  chrome.alarms.create("clear:job:" + j.id, { delayInMinutes: NOTIFY_SECONDS / 60 });
}

chrome.notifications.onClicked.addListener((id) => {
  if (id.startsWith("job:")) native({ cmd: "open", jobId: id.slice(4) });
  chrome.notifications.clear(id);
});

// ---- messages from page / popup -----------------------------------------
chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  (async () => {
    switch (msg && msg.type) {
      case "start":
        sendResponse(await startJob(msg.url, msg.title || ""));
        break;
      case "ping": {
        const ping = await native({ cmd: "ping", full: !!msg.full });
        sendResponse(await checkVersion(ping));
        break;
      }
      case "jobs": {
        const r = await native({ cmd: "jobs", max: msg.max || 8 });
        if (r.ok && r.jobs.some((j) => !["done", "failed", "cancelled"].includes(j.state))) {
          for (const j of r.jobs) known[j.id] = known[j.id] || j.state;
          schedulePoll(2000);
        }
        sendResponse(r);
        break;
      }
      case "sendTab": {
        const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
        sendResponse(await sendTab(tab));
        break;
      }
      case "native":
        sendResponse(await native(msg.payload));
        break;
      default:
        sendResponse({ ok: false, error: "unknown message" });
    }
  })();
  return true; // async response
});

// ---- toolbar shortcut and context menu ------------------------------------
// Finds the video for a tab: the tab itself if it is a video page, else a
// video player embedded in the page (e.g. a YouTube player on a news site),
// else the page address itself (yt-dlp can often find the video in it).
async function resolveTabVideo(tab) {
  const title = tab.title || "";
  if (PMP3Sites.isVideoUrl(tab.url || "")) return { url: PMP3Sites.cleanUrl(tab.url), title };
  try {
    // Allowed on any site because the user just clicked our button/menu/shortcut (activeTab).
    await chrome.scripting.executeScript({ target: { tabId: tab.id }, files: ["sites.js"] });
    const [res] = await chrome.scripting.executeScript({
      target: { tabId: tab.id },
      func: () => {
        const found = [];
        for (const f of document.querySelectorAll("iframe, embed, object")) {
          const src = f.src || f.getAttribute("data-src") || f.data || "";
          if (!src || !PMP3Sites.isVideoUrl(src)) continue;
          const r = f.getBoundingClientRect();
          found.push({ url: PMP3Sites.cleanUrl(src), area: r.width * r.height });
        }
        found.sort((a, b) => b.area - a.area); // the biggest player is the main video
        return found.map((f) => f.url);
      },
    });
    const urls = (res && res.result) || [];
    if (urls.length) return { url: urls[0], title, embedded: true, count: urls.length };
  } catch (e) {
    /* page we may not script (edge:// pages, store) */
  }
  if (/^https?:/i.test(tab.url || "")) return { url: tab.url, title, guessed: true };
  return null;
}

async function sendTab(tab) {
  if (!tab) return { ok: false, error: "No tab" };
  const found = await resolveTabVideo(tab);
  if (!found) return { ok: false, error: "This tab does not show a web page with a video." };
  const { url, title } = found;
  const reply = await startJob(url, title);
  const text = reply.ok ? [`MP3 download started at ${reply.speed}x`, title] : ["Podcast MP3 could not start", reply.error];
  chrome.tabs.sendMessage(tab.id, { type: "toast", text, kind: reply.ok ? "" : "error" }).catch(() => {});
  return reply;
}

chrome.commands.onCommand.addListener(async (command, tab) => {
  if (command === "send-current-page") {
    if (!tab) [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
    sendTab(tab);
  }
});

function setupMenus() {
  chrome.contextMenus.removeAll(() => {
    chrome.contextMenus.create({
      id: "pmp3-link",
      title: "Download MP3 of this video",
      contexts: ["link"],
      targetUrlPatterns: [
        "*://*.rumble.com/v*",
        "*://*.rumble.com/embed/*",
        "*://*.youtube.com/watch*",
        "*://*.youtube.com/shorts/*",
        "*://*.youtube.com/live/*",
        "*://youtu.be/*",
      ],
    });
    chrome.contextMenus.create({
      id: "pmp3-page",
      title: "Download MP3 of this video",
      contexts: ["page", "video"],
      documentUrlPatterns: ["*://*.rumble.com/*", "*://*.youtube.com/*"],
    });
    // Right-click inside a YouTube player embedded on another site.
    chrome.contextMenus.create({
      id: "pmp3-frame",
      title: "Download MP3 of this video",
      contexts: ["frame"],
      documentUrlPatterns: ["*://*.youtube.com/embed/*", "*://*.youtube-nocookie.com/embed/*", "*://*.rumble.com/embed/*"],
    });
  });
}

chrome.contextMenus.onClicked.addListener((info, tab) => {
  if (info.menuItemId === "pmp3-link" && info.linkUrl && PMP3Sites.isVideoUrl(info.linkUrl)) {
    startJob(PMP3Sites.cleanUrl(info.linkUrl), "").then((reply) => {
      const text = reply.ok ? [`MP3 download started at ${reply.speed}x`, info.linkUrl] : ["Podcast MP3 could not start", reply.error];
      if (tab) chrome.tabs.sendMessage(tab.id, { type: "toast", text, kind: reply.ok ? "" : "error" }).catch(() => {});
    });
  } else if (info.menuItemId === "pmp3-frame" && info.frameUrl && PMP3Sites.isVideoUrl(info.frameUrl)) {
    startJob(PMP3Sites.cleanUrl(info.frameUrl), (tab && tab.title) || "");
  } else if (info.menuItemId === "pmp3-page") {
    sendTab(tab);
  }
});

// ---- lifecycle -------------------------------------------------------------
// Pages that were already open when the extension was installed or updated
// have no page script yet; add it so middle-click works without a reload.
async function injectIntoOpenTabs() {
  const { content_scripts: [cs] } = chrome.runtime.getManifest();
  const tabs = await chrome.tabs.query({ url: cs.matches });
  for (const t of tabs) {
    chrome.scripting
      .executeScript({ target: { tabId: t.id, allFrames: true }, files: cs.js })
      .catch(() => {});
  }
  // Players embedded on other sites live in frames of tabs with any address.
  const all = await chrome.tabs.query({ url: ["http://*/*", "https://*/*"] });
  for (const t of all) {
    let frames = [];
    try {
      frames = (await chrome.webNavigation.getAllFrames({ tabId: t.id })) || [];
    } catch (e) {
      continue;
    }
    for (const f of frames) {
      if (f.frameId !== 0 && PMP3Sites.isVideoUrl(f.url))
        chrome.scripting.executeScript({ target: { tabId: t.id, frameIds: [f.frameId] }, files: cs.js }).catch(() => {});
    }
  }
}

chrome.runtime.onInstalled.addListener(() => {
  setupMenus();
  injectIntoOpenTabs();
  chrome.alarms.create("versionCheck", { periodInMinutes: 5 });
  checkVersion();
});

chrome.runtime.onStartup.addListener(() => {
  chrome.alarms.create("versionCheck", { periodInMinutes: 5 });
  checkVersion();
});

chrome.alarms.onAlarm.addListener((a) => {
  if (a.name === "versionCheck") checkVersion();
  else if (a.name.startsWith("clear:")) chrome.notifications.clear(a.name.slice(6));
});
