---
layout: home

hero:
  name: OpenRF
  text: Return Fire, native again
  tagline: A faithful, from-scratch reimplementation of the 1996 classic, using the data from your own CD. Runs natively on macOS, and on Linux, Windows and in your browser through the gasm WebAssembly runtime.
  image:
    src: /screenshots/tank-fire.png
    alt: A tank firing at a building in OpenRF
  actions:
    - theme: brand
      text: Get started
      link: /guide/
    - theme: alt
      text: Play in the browser
      link: /play/
      target: _self
    - theme: alt
      text: Download
      link: https://github.com/emdzej/openrf/releases
    - theme: alt
      text: How it works
      link: /internals/

features:
  - title: Faithful
    details: Game logic, physics, AI and rendering are ported function by function from the original executable, in the same fixed-point maths. The renderer is verified pixel-identical against a reference implementation.
  - title: Your original data
    details: Reads the sprites, maps, movies, sound effects and the orchestral score straight from the Return Fire CD. No assets are included or converted.
  - title: Native
    details: Portable C11 and SDL3 with no emulator, no Wine and no virtual machine. Available now as a universal macOS app (Apple Silicon and Intel); native Windows and Linux builds are in the works.
  - title: Runs on gasm
    details: One openrf.wasm for macOS, Linux, Windows and the browser, on the gasm WebAssembly game runtime. Deterministic and pixel-identical to the native app, with ready-to-run bundles for each system.
    link: /guide/gasm
    linkText: Running on gasm
  - title: In the browser
    details: The same engine as a WebAssembly module on the gasm runtime. Choose your CD folder once and play in Chrome, Edge, Firefox or Safari, with sound, gamepads and two players on one keyboard.
    link: /play/
    linkText: Play
    target: _self
  - title: Complete experience
    details: Intro movies, the bunker lift, all four vehicles, turrets, drones, the submarine, soldiers, the flag hunt, victory movies and high scores.
---

<div class="vp-doc" style="max-width: 1152px; margin: 48px auto 0; padding: 0 24px;">

## Built for gasm

<p><a class="gasm-badge" href="https://gasm.emdzej.pl"><img class="gasm-badge-light" src="https://gasm.emdzej.pl/badge/built-for-gasm-light.svg" alt="Built for gasm" width="120" height="44"><img class="gasm-badge-dark" src="https://gasm.emdzej.pl/badge/built-for-gasm-dark.svg" alt="Built for gasm" width="120" height="44"></a></p>

The Linux, Windows and browser versions, and `Return Fire (gasm).app` on the Mac, are one WebAssembly
module, `openrf.wasm`, running on [gasm](https://gasm.emdzej.pl), a portable game runtime. It is the same
engine as the native app with a different platform layer: the same inputs give the same frames and sound
on every runner, checked hash for hash. [Download a bundle](/guide/install), [play in the browser](/play/){target="_self"}
or read [Running on gasm](/guide/gasm).

## Screenshots

<div class="shots">
  <img src="/screenshots/select.png" alt="Bunker vehicle select">
  <img src="/screenshots/heli.png" alt="Helicopter over the island">
  <img src="/screenshots/turret.png" alt="Destroying an enemy tower">
  <img src="/screenshots/title.png" alt="Title screen">
</div>

[More in the gallery →](/gallery)

</div>

<style>
.shots { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 12px; }
.shots img { width: 100%; image-rendering: pixelated; border-radius: 6px; }
</style>
