// What Buddy feels in Missile Defense: the face he pulls (buddy_game.h) for the game at a moment, and where his eyes
// point. The biggest thing going on wins: the game lost, a missile on his head, overheated eyes, a desk item just lost,
// a cleared wave, a shot, too many missiles too close, a new wave, a streak; otherwise he tracks the next missile.
// Each face starts when its reason did (md_t's timestamps), so it plays through without restarting every frame.
#pragma once

#include <stdint.h>

#include "buddy_game.h"
#include "md.h"

#ifdef __cplusplus
extern "C" {
#endif

buddy_game_t md_buddy_face(const md_t *g, uint32_t now);

#ifdef __cplusplus
}
#endif
