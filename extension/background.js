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

// Edge's cookies for a site (both the normal ones and those Edge keeps
// separately for a site embedded in another site's page).
async function siteCookies(domain) {
  const out = [];
  const seen = new Set();
  const add = (list) => {
    for (const c of list || []) {
      const key = c.domain + "|" + c.path + "|" + c.name;
      if (seen.has(key)) continue;
      seen.add(key);
      out.push({ domain: c.domain, hostOnly: c.hostOnly, path: c.path, secure: c.secure,
                 expirationDate: c.expirationDate || 0, name: c.name, value: c.value });
    }
  };
  try {
    add(await chrome.cookies.getAll({ domain }));
  } catch (e) {
    /* ignore */
  }
  try {
    add(await chrome.cookies.getAll({ domain, partitionKey: {} }));
  } catch (e) {
    /* older Edge: no partitioned cookies */
  }
  return out;
}

async function startJob(url, title) {
  const p = await prefs();
  const msg = { cmd: "start", url, title, speed: p.speed, bitrateKbps: p.bitrateKbps, volume: p.volume };
  const site = PMP3Sites.siteForUrl(url);
  if (site && site.cookieDomain) {
    msg.cookies = await siteCookies(site.cookieDomain);
    msg.userAgent = navigator.userAgent;
  }
  const reply = await native(msg);
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
// Human-readable titles for the chooser. First the site's public "oEmbed"
// lookup (title, channel, and for Rumble the length); if that fails, the
// title shown inside the embedded player itself.
async function fetchJson(url, ms) {
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), ms);
  try {
    const r = await fetch(url, { signal: ctl.signal, credentials: "omit" });
    return r.ok ? await r.json() : null;
  } catch (e) {
    return null;
  } finally {
    clearTimeout(timer);
  }
}

async function oembedInfo(video) {
  let api = null;
  if (video.site === "YouTube") api = "https://www.youtube.com/oembed?format=json&url=" + encodeURIComponent(video.url);
  else if (video.site === "Rumble") api = "https://rumble.com/api/Media/oembed.json?url=" + encodeURIComponent(video.url);
  if (!api) return null;
  const j = await fetchJson(api, 4000);
  if (!j || !j.title) return null;
  return { title: j.title, channel: j.author_name || "", duration: Number(j.duration) || 0 };
}

async function addVideoTitles(tabId, videos) {
  // Titles of the embedded players that have loaded ("Some title - Rumble").
  const frameTitles = {};
  try {
    const results = await chrome.scripting.executeScript({
      target: { tabId, allFrames: true },
      func: () => ({ url: location.href, title: document.title }),
    });
    for (const r of results || []) {
      if (!r.result || !PMP3Sites.isVideoUrl(r.result.url)) continue;
      const t = (r.result.title || "").replace(/\s+[-|]\s+(Rumble|YouTube)\s*$/i, "").trim();
      // (a player that has not loaded yet is titled just "YouTube")
      if (t && !/^(YouTube|Rumble)$/i.test(t)) frameTitles[PMP3Sites.cleanUrl(r.result.url)] = t;
    }
  } catch (e) {
    /* frames we may not read */
  }
  const infos = await Promise.all(videos.map((v) => oembedInfo(v)));
  return videos.map((v, i) => {
    const info = infos[i] || {};
    return {
      ...v,
      title: info.title || frameTitles[v.url] || v.title || "",
      channel: info.channel || "",
      duration: info.duration || 0,
    };
  });
}

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
        const seen = new Set();
        let index = -1;
        for (const f of document.querySelectorAll("iframe, embed, object")) {
          index++;
          const src = f.src || f.getAttribute("data-src") || f.data || "";
          if (!src || !PMP3Sites.isVideoUrl(src)) continue;
          const url = PMP3Sites.cleanUrl(src);
          if (seen.has(url)) continue; // the same video embedded twice
          seen.add(url);
          const r = f.getBoundingClientRect();
          const site = PMP3Sites.siteForUrl(src);
          found.push({
            url,
            index, // position among iframe/embed/object, used to outline it
            title: (f.getAttribute("title") || "").trim(),
            site: site ? site.name : "",
            width: Math.round(r.width),
            height: Math.round(r.height),
          });
        }
        found.sort((a, b) => b.width * b.height - a.width * a.height); // biggest first
        return found;
      },
    });
    const videos = (res && res.result) || [];
    if (videos.length === 1) return { url: videos[0].url, title: videos[0].title || title, embedded: true };
    if (videos.length > 1) return { choices: await addVideoTitles(tab.id, videos), title };
  } catch (e) {
    /* page we may not script (edge:// pages, store) */
  }
  if (/^https?:/i.test(tab.url || "")) return { url: tab.url, title, guessed: true };
  return null;
}

// Shown in the page when it has several videos: a drop-down sorted by size
// (largest first); the selected player is outlined so it is clear which is
// which. Runs inside the page, so it must not use anything from this file.
function showVideoChooser(videos, speed) {
  const old = document.getElementById("pmp3-chooser");
  if (old) old.remove();
  const frames = document.querySelectorAll("iframe, embed, object");
  let outlined = null;
  const outline = (v) => {
    if (outlined) {
      outlined.el.style.outline = outlined.prev;
      outlined = null;
    }
    const el = v && frames[v.index];
    if (!el) return;
    outlined = { el, prev: el.style.outline };
    el.style.outline = "5px solid #34a046";
    el.scrollIntoView({ block: "center", behavior: "smooth" });
  };
  const esc = (t) => String(t).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
  const hms = (s) => {
    s = Math.round(s);
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), x = s % 60;
    return (h ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(x).padStart(2, "0");
  };
  const host = document.createElement("div");
  host.id = "pmp3-chooser";
  host.style.cssText = "all:initial;position:fixed;z-index:2147483647;top:16px;right:16px;";
  const root = host.attachShadow({ mode: "open" });
  root.innerHTML = `
    <style>
      .b { font: 14px/1.35 "Segoe UI", system-ui, sans-serif; color: #fff; background: #1f2a22; width: 470px;
           border-left: 5px solid #34a046; border-radius: 8px; padding: 12px 14px; box-shadow: 0 6px 24px rgba(0,0,0,.4); }
      .h { font-weight: 600; margin-bottom: 8px; }
      ul { list-style: none; margin: 0 0 10px; padding: 0; max-height: 60vh; overflow-y: auto; }
      li { background: #2b372e; border: 2px solid transparent; border-radius: 6px; padding: 7px 9px; margin-bottom: 6px;
           cursor: pointer; display: flex; gap: 8px; }
      li:hover { background: #33423a; }
      li[aria-selected="true"] { border-color: #34a046; background: #2f4535; }
      .n { font-weight: 700; color: #8fd19e; min-width: 1.4em; }
      .t { font-weight: 600; }
      .d { font-size: 12px; opacity: .75; margin-top: 2px; }
      .r { display: flex; gap: 8px; justify-content: flex-end; }
      button { font: inherit; padding: 6px 14px; border-radius: 6px; border: 0; cursor: pointer; }
      .go { background: #34a046; color: #fff; font-weight: 600; }
      .no { background: #3a463d; color: #fff; }
      .s { font-size: 12px; opacity: .8; margin-top: 8px; }
    </style>
    <div class="b">
      <div class="h">🎧 This page has ${videos.length} videos. Which one?</div>
      <ul role="listbox" tabindex="0"></ul>
      <div class="r"><button class="no">Cancel</button><button class="go">Download MP3 at ${speed}x</button></div>
      <div class="s">Largest first. Click to select (outlined in green on the page), double-click to download.</div>
    </div>`;
  const list = root.querySelector("ul");
  let selected = 0;
  const items = videos.map((v, i) => {
    const li = document.createElement("li");
    li.setAttribute("role", "option");
    const id = (v.url.match(/[?&]v=([\w-]+)/) || v.url.match(/embed\/(\w+)/) || [])[1] || "";
    const name = v.title || `${v.site || "Video"} ${id} (title not available)`;
    const details = [v.channel, v.duration ? hms(v.duration) : "", v.site, `${v.width}x${v.height}`, i === 0 ? "largest" : ""]
      .filter(Boolean)
      .join(" · ");
    li.innerHTML = `<span class="n">${i + 1}</span><span><div class="t">${esc(name)}</div><div class="d">${esc(details)}</div></span>`;
    li.addEventListener("click", () => select(i));
    li.addEventListener("dblclick", () => {
      select(i);
      go();
    });
    list.appendChild(li);
    return li;
  });
  function select(i) {
    selected = i;
    items.forEach((li, k) => li.setAttribute("aria-selected", String(k === i)));
    items[i].scrollIntoView({ block: "nearest" });
    outline(videos[i]);
  }
  const close = () => {
    outline(null);
    host.remove();
    document.removeEventListener("keydown", onKey, true);
  };
  const onKey = (e) => {
    if (e.key === "Escape") close();
    else if (e.key === "ArrowDown") { select(Math.min(videos.length - 1, selected + 1)); e.preventDefault(); }
    else if (e.key === "ArrowUp") { select(Math.max(0, selected - 1)); e.preventDefault(); }
    else if (e.key === "Enter") { go(); e.preventDefault(); }
  };
  function go() {
    const v = videos[selected];
    if (!root.querySelector(".r")) return; // already sent
    root.querySelector(".r").remove();
    list.style.pointerEvents = "none";
    root.querySelector(".s").textContent = "Sending to Podcast MP3...";
    chrome.runtime.sendMessage({ type: "start", url: v.url, title: v.title }, (reply) => {
      root.querySelector(".s").textContent =
        reply && reply.ok ? `MP3 download started at ${reply.speed}x: ${v.title || v.url}` : "Could not start: " + ((reply && reply.error) || "no answer");
      setTimeout(close, reply && reply.ok ? 3000 : 9000);
    });
  }
  document.addEventListener("keydown", onKey, true);
  root.querySelector(".no").addEventListener("click", close);
  root.querySelector(".go").addEventListener("click", go);
  (document.body || document.documentElement).appendChild(host);
  select(0);
}

async function sendTab(tab) {
  if (!tab) return { ok: false, error: "No tab" };
  const found = await resolveTabVideo(tab);
  if (!found) return { ok: false, error: "This tab does not show a web page with a video." };
  if (found.choices) {
    const p = await prefs();
    await chrome.scripting.executeScript({ target: { tabId: tab.id }, func: showVideoChooser, args: [found.choices, p.speed] });
    return { ok: true, choosing: true, count: found.choices.length };
  }
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
    // One entry for the page itself and for embedded players (right-clicking
    // inside an embedded player counts as both "page" and "frame"; separate
    // entries for each showed up twice). Works on any web site.
    chrome.contextMenus.create({
      id: "pmp3-page",
      title: "Download MP3 of this video",
      contexts: ["page", "frame", "video"],
      documentUrlPatterns: ["http://*/*", "https://*/*"],
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
    if (info.frameUrl && PMP3Sites.isVideoUrl(info.frameUrl)) {
      // Right-click inside an embedded player: that player's video.
      startJob(PMP3Sites.cleanUrl(info.frameUrl), "").then((reply) => {
        const text = reply.ok ? [`MP3 download started at ${reply.speed}x`, info.frameUrl] : ["Podcast MP3 could not start", reply.error];
        if (tab) chrome.tabs.sendMessage(tab.id, { type: "toast", text, kind: reply.ok ? "" : "error" }, { frameId: info.frameId }).catch(() => {});
      });
    } else {
      sendTab(tab); // the page's video, or the chooser if it has several
    }
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
