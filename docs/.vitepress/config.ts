import { defineConfig } from "vitepress";

export default defineConfig({
  title: "OpenRF",
  description: "A native, cross-platform reimplementation of Return Fire (1996), using the data from your own CD.",
  cleanUrls: true,
  lastUpdated: true,
  srcExclude: ["CONTEXT.md", "README.md", "AGENTS.md"],
  sitemap: { hostname: "https://openrf.emdzej.pl" },
  // The browser player is a static page in public/play/, not a Markdown page.
  ignoreDeadLinks: [/^\/play\//],

  head: [
    ["link", { rel: "icon", href: "/favicon.png", type: "image/png" }],
    ["meta", { name: "theme-color", content: "#727431" }],
    ["meta", { property: "og:title", content: "OpenRF — Return Fire, native again" }],
    ["meta", { property: "og:description", content: "A faithful, from-scratch reimplementation of Return Fire. macOS today, Windows and Linux next." }],
    ["meta", { property: "og:image", content: "https://openrf.emdzej.pl/screenshots/tank-fire.png" }],
    ["meta", { property: "og:url", content: "https://openrf.emdzej.pl/" }],
  ],

  themeConfig: {
    siteTitle: "OpenRF",
    logo: "/favicon.png",

    nav: [
      { text: "User guide", link: "/guide/", activeMatch: "/guide/" },
      { text: "How-tos", link: "/howto/build-from-source", activeMatch: "/howto/" },
      { text: "Internals", link: "/internals/", activeMatch: "/(internals|architecture|render|game|car|rfm|stm)" },
      { text: "Gallery", link: "/gallery" },
      // A static app in docs/public/play/: target _self makes it a full page load, not a VitePress route.
      { text: "Play", link: "/play/", target: "_self" },
    ],

    sidebar: {
      "/guide/": [
        {
          text: "User guide",
          items: [
            { text: "Introduction", link: "/guide/" },
            { text: "Installing", link: "/guide/install" },
            { text: "Game data", link: "/guide/game-data" },
            { text: "Playing", link: "/guide/playing" },
            { text: "Vehicles", link: "/guide/vehicles" },
            { text: "Controls", link: "/guide/controls" },
            { text: "Running on gasm", link: "/guide/gasm" },
            { text: "Troubleshooting", link: "/guide/troubleshooting" },
          ],
        },
      ],
      "/howto/": [
        {
          text: "How-tos",
          items: [
            { text: "Build from source", link: "/howto/build-from-source" },
            { text: "Extract the CD image", link: "/howto/extract-cd" },
            { text: "Choose a level", link: "/howto/choose-level" },
            { text: "Debug options & screenshots", link: "/howto/debug" },
            { text: "Run the tests", link: "/howto/tests" },
            { text: "Make a release", link: "/howto/release" },
            { text: "Reverse-engineering workflow", link: "/howto/reverse-engineering" },
          ],
        },
      ],
      "/": [
        {
          text: "Internals",
          items: [
            { text: "Overview", link: "/internals/" },
            { text: "Portable core", link: "/internals/portable-core" },
            { text: "Engine architecture", link: "/architecture" },
            { text: "Renderer", link: "/render" },
            { text: "Game simulation", link: "/game" },
          ],
        },
        {
          text: "File formats",
          items: [
            { text: "ART.CAR sprites", link: "/car" },
            { text: "RFM level maps", link: "/rfm" },
            { text: "STM movies", link: "/stm" },
          ],
        },
      ],
    },

    socialLinks: [{ icon: "github", link: "https://github.com/emdzej/openrf" }],

    editLink: {
      pattern: "https://github.com/emdzej/openrf/edit/main/docs/:path",
      text: "Edit this page on GitHub",
    },

    search: { provider: "local" },

    footer: {
      message:
        "OpenRF is released under the GPL-3.0. Return Fire is © 1995–1996 Silent Software / Prolific. OpenRF contains no original code or assets. The cross-platform and browser builds run on <a href=\"https://gasm.emdzej.pl\">gasm</a>.",
    },
  },
});
