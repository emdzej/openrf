/* Music director, after Mus_Request/Mus_Service/Mus_OnTrackEnd (docs/architecture.md §7.3). */
#pragma once
#include <stdbool.h>

enum {
    MUS_TANK1, MUS_TANK2, MUS_TANK3, MUS_TANK_DEATH, MUS_JEEP, MUS_JEEP_DEATH, MUS_MSV, MUS_MSV_DEATH,
    MUS_HELI1, MUS_HELI2, MUS_HELI_DEATH, MUS_FLAG_DISCOVERY, MUS_FLAG_PICKUP, MUS_WIN, MUS_BUNKER,
    MUS_DEATH, MUS_SUB, MUS_DRUMS, MUS_COUNT, MUS_SILENCE = -1
};

void music_init(void);
/* owner: id of the object the music belongs to (0 = none); if it changes, music stops. */
bool music_request(int track, int prio, int owner);
void music_owner_changed(int owner);   /* call when an owner object dies */
void music_service(void);              /* once per game frame */
void music_set_enabled(bool on);
bool music_is_playing(void);          /* Mus_IsPlaying: a track is current (the Win one-shot ends in silence) */
int music_current(void);               /* current track or MUS_SILENCE */
