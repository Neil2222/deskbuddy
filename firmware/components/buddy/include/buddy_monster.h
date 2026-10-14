// "Drink a Monster": DeskBuddy charges himself from a can of Monster Energy through a cable plugged into his head.
//
// A fixed choreography, pure function of the time since it started: no state, no hardware, no pixels. The brain
// runs it in BUDDY_STATE_MONSTER; the view draws the props it describes.
//
//   DISCOVER    a can drops in beside him; double take, "!", excited hop and wiggle
//   PREPARE     a telescopic hand produces a cable and plugs one end into the can
//   CONNECT     the other end goes into the port on the side of his head: click
//   CHARGE      green energy flows, bolts and sparks, glowing eyes, trembling, battery fills, can drains
//   OVERCHARGE  lightning eyes, strobing antenna, negative flashes, violent vibration, battery overflows
//   FINISH      the plug pops out, the empty can crumples, three happy hops, the can vanishes
#pragma once

#include <stdint.h>

#include "buddy_pose.h"

#ifdef __cplusplus
extern "C" {
#endif

// Phase start times, in ms since the sequence started.
#define BUDDY_MONSTER_DISCOVER_AT 0u
#define BUDDY_MONSTER_PREPARE_AT 1100u
#define BUDDY_MONSTER_CONNECT_AT 2400u
#define BUDDY_MONSTER_CHARGE_AT 2900u
#define BUDDY_MONSTER_OVERCHARGE_AT 4500u
#define BUDDY_MONSTER_FINISH_AT 5300u
#define BUDDY_MONSTER_MS 6800u // total length; the brain returns to idle afterwards

// Phase at t ms after the start; BUDDY_PHASE_NONE once the sequence is over.
buddy_phase_t buddy_monster_phase(uint32_t t);

// Writes the pose t ms after the start (t < BUDDY_MONSTER_MS). Returns ms until the pose may change next (>= 1).
uint32_t buddy_monster_pose(uint32_t t, buddy_pose_t *pose);

#ifdef __cplusplus
}
#endif
