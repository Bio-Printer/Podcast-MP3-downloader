// Podcast MP3 Downloader - background service worker.
// Talks to the helper program (podcastmp3-host.exe) through native messaging.
/* global chrome, importScripts, PMP3Sites */
importScripts("sites.js");

const HOST = "com.bioprinter.podcastmp3";
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
  });
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
async function sendTab(tab) {
  if (!tab) return { ok: false, error: "No tab" };
  let url = PMP3Sites.isVideoUrl(tab.url || "") ? PMP3Sites.cleanUrl(tab.url) : null;
  let title = tab.title || "";
  if (!url) {
    try {
      const r = await chrome.tabs.sendMessage(tab.id, { type: "getPageVideo" });
      if (r && r.url) {
        url = r.url;
        title = r.title || title;
      }
    } catch (e) {
      /* no content script on this page */
    }
  }
  if (!url) return { ok: false, error: "This page is not a Rumble video page." };
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
      targetUrlPatterns: ["*://*.rumble.com/v*", "*://*.rumble.com/embed/*"],
    });
    chrome.contextMenus.create({
      id: "pmp3-page",
      title: "Download MP3 of this video",
      contexts: ["page", "video"],
      documentUrlPatterns: ["*://*.rumble.com/*"],
    });
  });
}

chrome.contextMenus.onClicked.addListener((info, tab) => {
  if (info.menuItemId === "pmp3-link" && info.linkUrl && PMP3Sites.isVideoUrl(info.linkUrl)) {
    startJob(PMP3Sites.cleanUrl(info.linkUrl), "").then((reply) => {
      const text = reply.ok ? [`MP3 download started at ${reply.speed}x`, info.linkUrl] : ["Podcast MP3 could not start", reply.error];
      if (tab) chrome.tabs.sendMessage(tab.id, { type: "toast", text, kind: reply.ok ? "" : "error" }).catch(() => {});
    });
  } else if (info.menuItemId === "pmp3-page") {
    sendTab(tab);
  }
});

// ---- lifecycle -------------------------------------------------------------
chrome.runtime.onInstalled.addListener(() => {
  setupMenus();
  chrome.alarms.create("versionCheck", { periodInMinutes: 5 });
  checkVersion();
});

chrome.runtime.onStartup.addListener(() => {
  chrome.alarms.create("versionCheck", { periodInMinutes: 5 });
  checkVersion();
});

chrome.alarms.onAlarm.addListener((a) => {
  if (a.name === "versionCheck") checkVersion();
});
