---
layout: home

hero:
  name: OpenRF
  text: Return Fire, native on macOS
  tagline: A faithful, from-scratch reimplementation of the 1996 classic for Apple Silicon and Intel Macs — using the data from your own CD.
  image:
    src: /screenshots/tank-fire.png
    alt: A tank firing at a building in OpenRF
  actions:
    - theme: brand
      text: Get started
      link: /guide/
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
    details: A single universal app (arm64 + x86_64) built with C11 and SDL3. No Wine, no emulator, no virtual machine.
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
