// Which pages and links count as "a video" on each supported site.
// Adding a site later (e.g. YouTube) means adding an entry here and its
// domain to manifest.json (host_permissions + content_scripts.matches).
/* exported PMP3Sites */
var PMP3Sites = (function () {
  const sites = [
    {
      name: "Rumble",
      host: /(^|\.)rumble\.com$/i,
      // https://rumble.com/v6abc12-some-title.html  or  https://rumble.com/embed/v5xyz/
      videoUrl: /^https?:\/\/([a-z0-9-]+\.)?rumble\.com\/(v[a-z0-9]+-[^\/?#]*\.html|embed\/v[a-z0-9]+)/i,
    },
  ];

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
