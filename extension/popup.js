// Podcast MP3 Downloader - toolbar popup.
/* global chrome, PMP3Sites */
"use strict";

const SPEEDS = [1, 1.25, 1.5, 1.75, 2, 2.5];
const DEFAULTS = { speed: 1.5, bitrateKbps: 192 };
const manifest = chrome.runtime.getManifest();
const myVersion = manifest.version_name || manifest.version;
const $ = (id) => document.getElementById(id);

function bg(msg) {
  return new Promise((resolve) => chrome.runtime.sendMessage(msg, (r) => resolve(r || { ok: false })));
}

function native(payload) {
  return bg({ type: "native", payload });
}

function notice(html) {
  const n = $("notice");
  if (!html) {
    n.hidden = true;
    return;
  }
  n.innerHTML = html;
  n.hidden = false;
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
}

async function renderSpeeds() {
  const p = await chrome.storage.local.get(DEFAULTS);
  const box = $("speeds");
  box.innerHTML = "";
  for (const s of SPEEDS) {
    const b = document.createElement("button");
    b.textContent = s + "x";
    b.setAttribute("role", "radio");
    b.setAttribute("aria-checked", String(Math.abs(s - p.speed) < 0.001));
    b.addEventListener("click", async () => {
      await chrome.storage.local.set({ speed: s });
      native({ cmd: "setSettings", settings: { speed: s } });
      renderSpeeds();
    });
    box.appendChild(b);
  }
  $("bitrate").value = String(p.bitrateKbps);
}

$("bitrate").addEventListener("change", async () => {
  const v = Number($("bitrate").value);
  await chrome.storage.local.set({ bitrateKbps: v });
  native({ cmd: "setSettings", settings: { bitrateKbps: v } });
});

async function showVersions() {
  $("extVersion").textContent = myVersion;
  const ping = await bg({ type: "ping", full: true });
  const v = $("versions");
  if (!ping.ok) {
    v.innerHTML = `Extension v${escapeHtml(myVersion)} &nbsp;·&nbsp; <span class="bad">Helper not connected</span>`;
    notice(
      "The helper program is not connected.<br>Open the folder where you unzipped Podcast MP3 and run " +
        "<b>PodcastMP3.exe</b> once. Then reopen this popup.<br><small>" +
        escapeHtml(ping.error || "") +
        "</small>"
    );
    $("sendTab").disabled = true;
    return false;
  }
  const match = ping.hostVersionFull && myVersion === ping.hostVersionFull;
  v.innerHTML =
    `Extension v${escapeHtml(myVersion)}<br>Helper v${escapeHtml(ping.hostVersionFull)} ` +
    (match ? '<span class="ok">✓ matched</span>' : '<span class="bad">⚠ different version</span>') +
    (ping.ytdlp ? `<br>yt-dlp ${escapeHtml(ping.ytdlp)}` : "");
  if (ping.reloading) {
    notice(`New extension files found on disk (v${escapeHtml(ping.extensionOnDisk)}). Reloading the extension now - reopen this popup in a moment.`);
  } else if (ping.versionMismatch) {
    notice(
      `Edge is running extension v${escapeHtml(myVersion)}, but the files in <code>${escapeHtml(ping.extensionDir)}</code> ` +
        `are v${escapeHtml(ping.extensionOnDisk)}. Open <b>edge://extensions</b> and press <b>Reload</b> on Podcast MP3 Downloader ` +
        `(or check that Edge loaded the extension from that folder).`
    );
  } else if (!match) {
    notice("The helper program and the extension are from different releases. Unzip the latest release over your install folder.");
  }
  return true;
}

function jobLine(j) {
  const li = document.createElement("li");
  const title = (j.channel ? j.channel + " - " : "") + (j.title || j.url);
  const final = ["done", "failed", "cancelled"].includes(j.state);
  const msg = j.state === "failed" ? j.error || "Failed" : j.message || j.state;
  li.innerHTML =
    `<div class="t" title="${escapeHtml(title)}">${escapeHtml(title)}</div>` +
    (final ? "" : `<div class="bar"><div style="width:${Math.max(2, j.percent || 0)}%"></div></div>`) +
    `<div class="m ${j.state === "failed" ? "err" : ""}">${escapeHtml(msg)}${j.state === "done" ? "" : " · " + escapeHtml(j.speed) + "x"}</div>` +
    `<div class="acts"></div>`;
  const acts = li.querySelector(".acts");
  const add = (label, fn) => {
    const b = document.createElement("button");
    b.className = "link";
    b.textContent = label;
    b.addEventListener("click", fn);
    acts.appendChild(b);
  };
  if (j.state === "done") add("Show file", () => native({ cmd: "open", jobId: j.id }));
  if (!final) add("Cancel", () => native({ cmd: "cancel", jobId: j.id }).then(refreshJobs));
  if (j.state === "failed") add("Try again", () => bg({ type: "start", url: j.url, title: j.title }).then(refreshJobs));
  return li;
}

let refreshTimer = null;
async function refreshJobs() {
  clearTimeout(refreshTimer);
  const r = await bg({ type: "jobs", max: 8 });
  const ul = $("jobs");
  if (!r.ok) return;
  ul.innerHTML = "";
  if (!r.jobs.length) ul.innerHTML = '<li class="empty">None yet</li>';
  for (const j of r.jobs) ul.appendChild(jobLine(j));
  const active = r.jobs.some((j) => !["done", "failed", "cancelled"].includes(j.state));
  refreshTimer = setTimeout(refreshJobs, active ? 1000 : 4000);
}

$("sendTab").addEventListener("click", async () => {
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  let url = tab && PMP3Sites.isVideoUrl(tab.url || "") ? PMP3Sites.cleanUrl(tab.url) : null;
  let title = (tab && tab.title) || "";
  if (!url && tab) {
    try {
      const r = await chrome.tabs.sendMessage(tab.id, { type: "getPageVideo" });
      if (r && r.url) ({ url, title } = { url: r.url, title: r.title || title });
    } catch (e) {
      /* not a Rumble page */
    }
  }
  if (!url) {
    notice("This tab is not showing a Rumble video. Open the video's own page, or middle-click a video thumbnail.");
    return;
  }
  const reply = await bg({ type: "start", url, title });
  if (!reply.ok) notice(escapeHtml(reply.error || "Could not start"));
  refreshJobs();
});

$("openFolder").addEventListener("click", () => native({ cmd: "open" }));
$("openApp").addEventListener("click", () => native({ cmd: "openApp" }));
$("updateYtdlp").addEventListener("click", async () => {
  $("updateYtdlp").textContent = "Updating...";
  const r = await native({ cmd: "updateYtdlp" });
  $("updateYtdlp").textContent = r.ok ? "yt-dlp " + (r.ytdlp || "updated") : "Update failed";
  if (!r.ok) notice("yt-dlp update: " + escapeHtml(r.output || r.error || "failed"));
});

(async () => {
  await renderSpeeds();
  if (await showVersions()) refreshJobs();
})();
