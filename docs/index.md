---
layout: home

hero:
  name: OpenRF
  text: Return Fire, native again
  tagline: A faithful, from-scratch reimplementation of the 1996 classic, using the data from your own CD. Runs on macOS and in your browser today; Windows and Linux are next.
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
    details: Portable C11 and SDL3 with no emulator, no Wine and no virtual machine. Available now as a universal macOS app (Apple Silicon and Intel); Windows and Linux builds are in the works.
  - title: In the browser
    details: The same engine as a WebAssembly module on the gasm runtime. Choose your CD folder once and play in Chrome, Edge, Firefox or Safari, with sound, gamepads and two players on one keyboard.
    link: /play/
    linkText: Play
    target: _self
  - title: Complete experience
    details: Intro movies, the bunker lift, all four vehicles, turrets, drones, the submarine, soldiers, the flag hunt, victory movies and high scores.
---

<div class="vp-doc" style="max-width: 1152px; margin: 48px auto 0; padding: 0 24px;">

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
