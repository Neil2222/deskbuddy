// Buddy as a player (Tic-Tac-Toe, ttt.h): the faces he pulls across the board, from the moment a round begins to the
// rematch question. The game decides which face and since when; the brain plays it (BUDDY_STATE_GAME) for as long as
// the game lasts, over his mood, which carries on unchanged when the game ends.
//
//   ready  a round begins: a little hop, bright eyes
//   watch  your turn: he looks at you, glances at the board now and then. Smug after a winning streak (heavy lids,
//          a smirk), keen after a losing streak (narrowed eyes, clenched teeth)
//   think  his turn: narrowed eyes darting to the board and back, tongue out
//   won    hops and sparkles with a big grin (BUDDY_GAME_REACT_MS), then smug and self-satisfied until the next round
//   lost   a frustrated shake, then he droops: dim face, a sigh and a tear (BUDDY_GAME_REACT_MS); then he sulks,
//          looking away from the board and glaring back at it now and then
//   drew   eyes darting about, his head wobbling, a "?" over it; then puzzled, the "?" staying, staring at the board
//
// Pure logic: no clock of its own (see buddy_brain.h).
#pragma once

#include <stdint.h>

#include "buddy_pose.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUDDY_GAME_REACT_MS 2500u // the celebration / disappointment / confusion; the post-game face follows
#define BUDDY_GAME_READY_MS 600u  // the hop at the start of a round (then he watches or thinks)

typedef enum {
    BUDDY_GAME_READY,
    BUDDY_GAME_WATCH,
    BUDDY_GAME_THINK,
    BUDDY_GAME_WON,
    BUDDY_GAME_LOST,
    BUDDY_GAME_DREW,
    BUDDY_GAME_FACE_COUNT,
} buddy_game_face_t;

typedef enum {
    BUDDY_GAME_EVEN,
    BUDDY_GAME_SMUG, // a winning streak
    BUDDY_GAME_KEEN, // a losing streak
} buddy_game_attitude_t;

typedef struct {
    uint8_t face;     // buddy_game_face_t
    uint8_t attitude; // buddy_game_attitude_t
    int8_t side;      // where the board is: +1 to his right on screen (the panel on the right), -1 to his left
    uint32_t since;   // when the face began (ms, the brain's clock)
} buddy_game_t;

// The pose t ms into the face. Writes over the zeroed *pose. Returns ms until the pose may change next (>= 1).
uint32_t buddy_game_pose(const buddy_game_t *g, uint32_t t, buddy_pose_t *pose);

#ifdef __cplusplus
}
#endif
