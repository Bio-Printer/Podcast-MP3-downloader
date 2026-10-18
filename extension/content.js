// Podcast MP3 Downloader - page script.
// Middle-click on a video (or on a link/thumbnail to a video) sends it to the
// helper program, which saves the audio as an MP3 at the chosen speed.
/* global PMP3Sites, chrome */
(function () {
  "use strict";
  if (window.__pmp3Loaded) return;
  window.__pmp3Loaded = true;

  let suppressUntil = 0;

  // Is the point (x, y) over a <video> element (even if an overlay covers it)?
  function videoAtPoint(x, y) {
    for (const v of document.querySelectorAll("video")) {
      const r = v.getBoundingClientRect();
      if (r.width > 40 && r.height > 30 && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return v;
    }
    return null;
  }

  function pageVideoUrl() {
    const canonical = document.querySelector('link[rel="canonical"]');
    const candidates = [location.href];
    if (canonical && canonical.href) candidates.unshift(canonical.href);
    const og = document.querySelector('meta[property="og:url"]');
    if (og && og.content) candidates.push(og.content);
    for (const c of candidates) if (PMP3Sites.isVideoUrl(c)) return PMP3Sites.cleanUrl(c);
    return null;
  }

  function pageTitle() {
    const og = document.querySelector('meta[property="og:title"]');
    return (og && og.content) || document.title || "";
  }

  // Works out which video the user middle-clicked, or null.
  function findTarget(e) {
    const path = e.composedPath ? e.composedPath() : [e.target];
    for (const el of path) {
      if (el && el.tagName === "A" && el.href && PMP3Sites.isVideoUrl(el.href)) {
        const title = (el.getAttribute("title") || el.textContent || "").trim().slice(0, 200);
        return { url: PMP3Sites.cleanUrl(el.href), title, kind: "link" };
      }
    }
    if (videoAtPoint(e.clientX, e.clientY)) {
      const url = pageVideoUrl();
      if (url) return { url, title: pageTitle(), kind: "player" };
    }
    return null;
  }

  // ---- toast (in a shadow root so the page's CSS cannot disturb it) ----
  let toastHost = null;
  let toastTimer = null;

  function toast(text, kind, sticky) {
    if (!toastHost) {
      toastHost = document.createElement("div");
      toastHost.style.cssText = "all:initial;position:fixed;z-index:2147483647;right:16px;bottom:16px;";
      const root = toastHost.attachShadow({ mode: "open" });
      root.innerHTML = `
        <style>
          .t { font: 14px/1.4 "Segoe UI", system-ui, sans-serif; color: #fff; background: #1f2a22;
               border-left: 5px solid #34a046; border-radius: 8px; padding: 10px 14px; max-width: 380px;
               box-shadow: 0 6px 24px rgba(0,0,0,.35); display: flex; gap: 10px; align-items: flex-start; }
          .t.error { border-left-color: #e5484d; }
          .t.busy { border-left-color: #f5a524; }
          .i { font-size: 18px; line-height: 1.1; }
          .s { opacity: .75; font-size: 12px; margin-top: 2px; }
        </style>
        <div class="t"><div class="i">🎧</div><div><div class="m"></div><div class="s"></div></div></div>`;
      (document.body || document.documentElement).appendChild(toastHost);
    }
    const root = toastHost.shadowRoot;
    const box = root.querySelector(".t");
    box.className = "t " + (kind || "");
    const [main, sub] = Array.isArray(text) ? text : [text, ""];
    root.querySelector(".m").textContent = main;
    root.querySelector(".s").textContent = sub || "";
    toastHost.style.display = "block";
    clearTimeout(toastTimer);
    if (!sticky) toastTimer = setTimeout(() => (toastHost.style.display = "none"), kind === "error" ? 9000 : 5000);
  }

  function send(target) {
    toast(["Sending to Podcast MP3...", target.title || target.url], "busy", true);
    chrome.runtime.sendMessage({ type: "start", url: target.url, title: target.title }, (reply) => {
      if (chrome.runtime.lastError || !reply) {
        toast(["Podcast MP3: extension error", (chrome.runtime.lastError || {}).message || "no reply"], "error");
        return;
      }
      if (reply.ok) {
        toast([`MP3 download started at ${reply.speed}x`, target.title || target.url], "");
      } else {
        toast(["Podcast MP3 could not start", reply.error || "unknown error"], "error");
      }
    });
  }

  function onMouseDown(e) {
    if (e.button !== 1) return;
    const target = findTarget(e);
    if (!target) return;
    // Stop the browser's auto-scroll and the page's own middle-click handling.
    e.preventDefault();
    e.stopImmediatePropagation();
    suppressUntil = Date.now() + 1500;
    send(target);
  }

  // Middle-click on a link would also open it in a new tab: swallow that.
  function swallow(e) {
    if (e.button === 1 && Date.now() < suppressUntil) {
      e.preventDefault();
      e.stopImmediatePropagation();
    }
  }

  window.addEventListener("mousedown", onMouseDown, true);
  window.addEventListener("auxclick", swallow, true);
  window.addEventListener("click", swallow, true);
  window.addEventListener("mouseup", swallow, true);

  // The background script asks for the page's video when the keyboard
  // shortcut or the toolbar button is used.
  chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
    if (msg && msg.type === "getPageVideo") {
      if (window.top !== window) return; // only the top frame answers
      const url = pageVideoUrl();
      sendResponse({ url, title: pageTitle() });
    } else if (msg && msg.type === "toast") {
      if (window.top !== window) return;
      toast(msg.text, msg.kind);
    }
  });
})();
