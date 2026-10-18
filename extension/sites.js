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
    },
    {
      name: "YouTube",
      host: /(^|\.)(youtube\.com|youtube-nocookie\.com|youtu\.be)$/i,
      // watch?v=ID, youtu.be/ID, /embed/ID (players embedded on other sites), /shorts/ID, /live/ID
      videoUrl: /^https?:\/\/([a-z0-9-]+\.)?(youtube\.com\/(watch\?(.*&)?v=|embed\/|shorts\/|live\/|v\/)|youtube-nocookie\.com\/embed\/|youtu\.be\/)[A-Za-z0-9_-]{11}/i,
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
      if (/rumble\.com$/i.test(u.hostname) && !/\/embed\//.test(u.pathname)) {
        u.search = "";
      }
      u.hash = "";
      return u.toString();
    } catch (e) {
      return url;
    }
  }

  return { sites, siteForUrl, isVideoUrl, cleanUrl };
})();
