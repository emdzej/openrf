# Gallery

All screenshots are taken from OpenRF itself (regenerate them with `tools/screenshots.sh`).

<script setup>
const shots = [
  ["title", "Title screen"],
  ["intro-logo", "Intro movie (Williams logo), decoded by OpenRF's own Cinepak decoder"],
  ["select", "Bunker lift: choosing a vehicle"],
  ["lift", "The lift doors open"],
  ["drive", "Driving the tank off the pad"],
  ["tank-fire", "Tank cannon against a building"],
  ["turret", "Enemy tower destroyed"],
  ["jeep", "Jeep and grenades"],
  ["msv", "Armoured support vehicle launcher"],
  ["heli", "Helicopter with its own dashboard"],
  ["drone", "The drone hunts vehicles that sit still"],
  ["sub", "Fly off the map and the submarine appears"],
  ["rules", "Soldiers, gates and flag buildings"],
  ["win", "Victory banner"],
  ["2p-select", "Two players: both bunkers"],
  ["2p-drive", "Two-player split screen"],
  ["2p-heli", "Split screen with the helicopter dashboard"],
  ["2p-spectate", "Out of vehicles: the skull waits for the end of the game"],
];
</script>

<div class="gallery">
  <figure v-for="[f, cap] in shots" :key="f">
    <img :src="`/screenshots/${f}.png`" :alt="cap">
    <figcaption>{{ cap }}</figcaption>
  </figure>
</div>

<style>
.gallery { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 16px; }
.gallery img { width: 100%; image-rendering: pixelated; border-radius: 6px; }
.gallery figcaption { font-size: 0.85em; color: var(--vp-c-text-2); }
</style>
