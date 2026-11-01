// Which pages and links count as "a video" on each supported site.
// Adding a site means adding an entry here and its domains to manifest.json
// (host_permissions + content_scripts.matches).
/* exported PMP3Sites */
var PMP3Sites = (function () {
  const sites = [
    {
      name: "Rumble",
      host: /(^|\.)rumble\.com$/i,
      // https://rumble.com/v6abc12-some-title.html  or  https://rumble.com/embed/v5xyz/
      videoUrl: /^https?:\/\/([a-z0-9-]+\.)?rumble\.com\/(v[a-z0-9]+-[^\/?#]*\.html|embed\/v[a-z0-9]+)/i,
      // Behind Cloudflare: hand Edge's rumble.com cookies (incl. the
      // "passed the check" cookie) to yt-dlp so it is let in like Edge.
      cookieDomain: "rumble.com",
    },
    {
      name: "YouTube",
      host: /(^|\.)(youtube\.com|youtube-nocookie\.com|youtu\.be)$/i,
      // watch?v=ID, youtu.be/ID, /embed/ID (players embedded on other sites), /shorts/ID, /live/ID
      videoUrl: /^https?:\/\/([a-z0-9-]+\.)?(youtube\.com\/(watch\?(.*&)?v=|embed\/|shorts\/|live\/|v\/)|youtube-nocookie\.com\/embed\/|youtu\.be\/)[A-Za-z0-9_-]{11}/i,
    },
    {
      name: "X",
      host: /(^|\.)(x\.com|twitter\.com)$/i,
      // https://x.com/user/status/123, .../status/123/video/1, and posts
      // embedded on other sites (platform.twitter.com/embed/Tweet.html?id=123).
      videoUrl: /^https?:\/\/((www\.|mobile\.)?(x|twitter)\.com\/[A-Za-z0-9_]+\/status(es)?\/\d+|platform\.twitter\.com\/embed\/Tweet\.html\?([^#]*&)?id=\d+)/i,
      // A post is not always a video: links to posts are left alone (middle-
      // click keeps opening them in a new tab); only a click on a video player
      // inside a post sends it.
      posts: true,
      // The post a video player belongs to (timelines show many posts).
      postUrlNear(el) {
        const art = el.closest && el.closest("article");
        if (!art) return null;
        for (const a of art.querySelectorAll('a[href*="/status/"]')) {
          if (a.querySelector("time") && /\/status\/\d+/.test(a.href)) return a.href;
        }
        return null;
      },
    },
    {
      name: "Telegram",
      host: /^(t\.me|telegram\.me)$/i,
      // https://t.me/channel/54104, t.me/s/channel/54104 (public channels only)
      videoUrl: /^https?:\/\/(t|telegram)\.me\/(s\/)?(?!c\/)[A-Za-z0-9_]{4,}\/\d+/i,
      posts: true,
      // In the channel view (t.me/s/channel) every message carries data-post="channel/54104".
      postUrlNear(el) {
        const msg = el.closest && el.closest("[data-post]");
        return msg ? "https://t.me/" + msg.getAttribute("data-post") : null;
      },
    },
  ];

  // YouTube video id from any of its URL forms, or null.
  function youTubeId(url) {
    const m =
      /[?&]v=([A-Za-z0-9_-]{11})/.exec(url) ||
      /\/(?:embed|shorts|live|v)\/([A-Za-z0-9_-]{11})/.exec(url) ||
      /youtu\.be\/([A-Za-z0-9_-]{11})/.exec(url);
    return m ? m[1] : null;
  }

  function siteForUrl(url) {
    try {
      const u = new URL(url);
      return sites.find((s) => s.host.test(u.hostname)) || null;
    } catch (e) {
      return null;
    }
  }

  function isVideoUrl(url) {
    const s = siteForUrl(url);
    return !!(s && s.videoUrl.test(url));
  }

  // A link that certainly leads to a video (links to X / Telegram posts may
  // lead to plain text posts, so they do not count).
  function isVideoLink(url) {
    const s = siteForUrl(url);
    return !!(s && !s.posts && s.videoUrl.test(url));
  }

  // On a site with posts (X, Telegram): the address of the post that the
  // element (a video player) belongs to, or null.
  function postUrlNear(el, pageUrl) {
    const s = siteForUrl(pageUrl);
    if (!s || !s.postUrlNear) return null;
    try {
      const url = s.postUrlNear(el);
      return url && isVideoUrl(url) ? cleanUrl(url) : null;
    } catch (e) {
      return null;
    }
  }

  // Strip tracking parameters so the same video always gives the same URL.
  function cleanUrl(url) {
    try {
      const s = siteForUrl(url);
      if (s && s.name === "YouTube") {
        // One canonical form, without playlist/time/tracking parameters.
        const id = youTubeId(url);
        if (id) return "https://www.youtube.com/watch?v=" + id;
      }
      const u = new URL(url);
      if (s && s.name === "X") {
        // One form: https://x.com/user/status/ID[/video/N], no tracking (?s=20&t=...).
        if (/^platform\./i.test(u.hostname)) return "https://x.com/i/status/" + u.searchParams.get("id");
        const m = /^\/([A-Za-z0-9_]+)\/status(?:es)?\/(\d+)(\/video\/\d+)?/.exec(u.pathname);
        if (m) return "https://x.com/" + m[1] + "/status/" + m[2] + (m[3] || "");
      }
      if (s && s.name === "Telegram") {
        // t.me/s/channel/ID and t.me/channel/ID?embed=1 -> t.me/channel/ID
        const m = /^\/(?:s\/)?([A-Za-z0-9_]+)\/(\d+)/.exec(u.pathname);
        if (m) return "https://t.me/" + m[1] + "/" + m[2];
      }
      if (/rumble\.com$/i.test(u.hostname) && !/\/embed\//.test(u.pathname)) {
        u.search = "";
      }
      u.hash = "";
      return u.toString();
    } catch (e) {
      return url;
    }
  }

  return { sites, siteForUrl, isVideoUrl, isVideoLink, postUrlNear, cleanUrl };
})();
