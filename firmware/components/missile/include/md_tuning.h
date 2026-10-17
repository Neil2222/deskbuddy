// Buddy Missile Defense: every number that decides how the game plays, in one place, to be tuned after playing it on
// the board. Nothing else in the game hard-codes difficulty. Times in ms, distances in screen pixels, heat in percent.
#pragma once

// --- Waves -----------------------------------------------------------------------------------------------------------
// One row per wave, in order: missiles in the wave, how many may fly at once, how long one takes from the top of the
// sky to the desk (its fall time; slower is easier), and the pause before the next one is launched (a random time
// between the two values). The first waves are meant to be gentle on a touchscreen.
// After the last row the game keeps that row's speed and pace, and adds MD_WAVE_GROWTH missiles per wave (up to
// MD_WAVE_MISSILES_MAX).
#define MD_WAVE_TABLE(X)                \
    /* missiles, at once, fall, gap */  \
    X(5, 2, 11000, 2400, 3400)          \
    X(7, 2, 10000, 2000, 3000)          \
    X(9, 3, 9000, 1700, 2600)           \
    X(11, 3, 8200, 1500, 2300)          \
    X(12, 4, 7400, 1300, 2100)          \
    X(14, 4, 6800, 1150, 1900)          \
    X(16, 5, 6200, 1000, 1700)          \
    X(18, 5, 5600, 900, 1500)
#define MD_WAVE_GROWTH 2
#define MD_WAVE_MISSILES_MAX 30

// About one missile in MD_BUDDY_ONE_IN goes for Buddy himself instead of a desk item (it stuns him, nothing more).
#define MD_BUDDY_ONE_IN 6

// --- His eye lasers and the blasts ------------------------------------------------------------------------------------
#define MD_BLAST_RADIUS 44    // a shot's blast at its largest
#define MD_CHAIN_RADIUS 30    // a missile caught in a blast explodes too, a little smaller (and catches others)
#define MD_IMPACT_RADIUS 36   // a missile hitting the desk or Buddy (shows only: it catches nothing)
#define MD_BLAST_GROW_MS 280  // a blast grows to its size...
#define MD_BLAST_HOLD_MS 220  // ...stays...
#define MD_BLAST_SHRINK_MS 320 // ...and dies down
#define MD_BEAM_MS 120        // how long the two beams show for a shot

#define MD_HEAT_PER_SHOT 22   // every shot heats his eyes this much (100 = overheated)...
#define MD_COOL_PER_S 40      // ...and they cool down this much a second
#define MD_OVERHEAT_MS 1400   // overheated: no shots for this long

#define MD_STUN_MS 1600       // a missile on his head: dazed, no shots for this long

// --- Score ------------------------------------------------------------------------------------------------------------
#define MD_POINTS_MISSILE 25  // per missile; the 2nd and 3rd caught by the same shot count double and triple
#define MD_POINTS_ITEM 100    // per desk item still standing when a wave is cleared
#define MD_SCORE_MAX 99999

// --- Pace of the screens around the play ------------------------------------------------------------------------------
#define MD_READY_MS 1800      // "WAVE n" before the missiles come
#define MD_CLEAR_MS 3200      // "WAVE n CLEAR!" and the bonus
#define MD_OVER_BUTTONS_MS 1500 // after the last item is gone: RETRY and QUIT appear
#define MD_OVER_IDLE_MS 120000  // nobody touches the game-over screen this long: the game closes

// --- His mood and lines -----------------------------------------------------------------------------------------------
#define MD_PANIC_PROGRESS 55  // a missile counts as close once it has come this far down (percent of its fall)...
#define MD_PANIC_ON 3         // ...he panics with this many close at once...
#define MD_PANIC_OFF 1        // ...and calms down again at this many
#define MD_SMUG_STREAK 6      // missiles in a row without a miss: he gets cocky
#define MD_SAY_MS 1800        // a line shows this long...
#define MD_SAY_GAP_MS 2600    // ...and the next ordinary one waits this long after it began (big moments do not)
#define MD_FIRE_FACE_MS 260   // his firing face after a shot
#define MD_LOST_FACE_MS 1300  // dismay after a desk item is lost
