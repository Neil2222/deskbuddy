// Buddy Missile Defense: missiles come down on Buddy's desk and he shoots them with the lasers from his eyes. The
// rules, the waves, the score and what he feels about it, as pure logic: no pixels (md_view.h draws it), no hardware,
// no clock of its own. Time is a free-running millisecond counter (wrap-around safe); everything is in fixed-size arrays.
//
// The field is the whole 1024 x 600 screen in screen pixels: the sky between MD_SKY_TOP and MD_GROUND_Y, and on the
// desk below it six items (cities, in the original) either side of Buddy. A missile flies in a straight line from the
// top of the sky to one item, or now and then to Buddy. A tap in the sky is a shot: both eye lasers hit that point at
// once and a blast grows there; a missile inside a blast explodes, and its blast catches others (chains). A missile
// that gets through destroys its item (or stuns Buddy: no shots for a moment, nothing lost). Shots heat his eyes;
// too many too fast and they overheat (no shots for a moment). A wave is cleared when its last missile is gone: a bonus
// per item still standing, then the next, harder wave (md_tuning.h). The game is over when every item is gone: the
// world freezes at that moment (no missile moves, no blast grows, nothing scores; md_world_ms), and the best score is
// kept until md_init (reboot).
//
// The world advances in fixed MD_STEP_MS steps (md_tick catches up to the present), so a slow frame never lets a
// missile jump through a blast, and the same inputs at the same times always give the same game.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "md_tuning.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- The field (screen pixels) ------------------------------------------------------------------------------------
#define MD_W 1024
#define MD_H 600
#define MD_SKY_TOP 46     // missiles start here (the score bar is above it)
#define MD_GROUND_Y 528   // the desk's top edge
#define MD_FIRE_MIN_Y (MD_SKY_TOP + 8)
#define MD_FIRE_MAX_Y (MD_GROUND_Y - 24) // a tap lower down aims at this height (his blasts stay off the desk)
#define MD_ITEMS 6
#define MD_ITEM_HIT_Y (MD_GROUND_Y - 20) // where a missile hits an item: its middle
#define MD_BUDDY_X 512    // where a missile hits Buddy: his face
#define MD_BUDDY_Y 430
#define MD_TARGET_BUDDY MD_ITEMS

extern const int16_t md_item_x[MD_ITEMS]; // the items' centres, left to right (mug, plant, can | laptop, duck, donut)

#define MD_MAX_MISSILES 10 // flying or fading at once (no wave asks for more than this at once, host-tested)
#define MD_MAX_BLASTS 8    // the oldest one gives way when a ninth starts
#define MD_SHOT_RING 8     // shots remembered (for the kills each one made)
#define MD_STEP_MS 10
#define MD_TRAIL_FADE_MS 300 // after a missile ends, its trail shrinks back into where it ended
#define MD_FRAME_MS 15       // while anything moves: draw as often as the display allows

typedef enum {
    MD_OFF,     // no game
    MD_READY,   // "WAVE n": the wave starts at phase_at + MD_READY_MS (shots allowed)
    MD_PLAYING,
    MD_CLEAR,   // the wave is cleared: the bonus, then the next wave at phase_at + MD_CLEAR_MS
    MD_OVER,    // every item is gone: RETRY and QUIT from phase_at + MD_OVER_BUTTONS_MS
} md_phase_t;

typedef enum {
    MD_MISSILE_FREE,
    MD_MISSILE_FLYING,
    MD_MISSILE_FADING, // it ended at (ex, ey) at end_at: shot down, or hit its target; the trail shrinks back
} md_missile_state_t;

typedef struct {
    uint8_t state;   // md_missile_state_t
    uint8_t target;  // 0..MD_ITEMS-1, or MD_TARGET_BUDDY
    bool shot_down;  // FADING: shot down (else it hit its target)
    int16_t x0, y0;  // launched here...
    int16_t x1, y1;  // ...towards here
    uint16_t fall_ms;
    uint32_t born;
    uint32_t end_at;
    int16_t ex, ey;
} md_missile_t;

typedef enum {
    MD_BLAST_SHOT,   // where a shot hit
    MD_BLAST_CHAIN,  // a missile caught in a blast
    MD_BLAST_IMPACT, // a missile that got through (catches nothing)
} md_blast_kind_t;

typedef struct {
    bool on;
    uint8_t kind; // md_blast_kind_t
    uint8_t shot; // SHOT, CHAIN: the shot it comes from (its kills count for that shot)
    int16_t x, y;
    uint32_t born;
} md_blast_t;

// What he says (md_line gives the text).
typedef enum {
    MD_SAY_NONE,
    MD_SAY_START,   // a new game
    MD_SAY_WAVE,    // a later wave begins
    MD_SAY_DOUBLE,  // two with one shot
    MD_SAY_TRIPLE,  // three or more with one shot
    MD_SAY_MISS,    // a shot that caught nothing
    MD_SAY_SMUG,    // a streak without a miss
    MD_SAY_ITEM,    // an item lost: MD_SAY_ITEM + item
    MD_SAY_HIT = MD_SAY_ITEM + MD_ITEMS, // a missile on his head
    MD_SAY_HOT,     // overheated
    MD_SAY_COOLING, // a tap while overheated
    MD_SAY_DAZED,   // a tap while stunned
    MD_SAY_PANIC,
    MD_SAY_CLEAR,
    MD_SAY_PERFECT, // a wave cleared without losing anything
    MD_SAY_OVER,
    MD_SAY_COUNT,
} md_say_t;

typedef struct {
    uint8_t phase; // md_phase_t
    uint8_t wave;  // 1..
    uint32_t phase_at;
    md_missile_t missile[MD_MAX_MISSILES];
    md_blast_t blast[MD_MAX_BLASTS];
    uint8_t items;                 // bit n: item n still stands
    uint32_t item_lost_at[MD_ITEMS]; // when it was destroyed
    uint8_t spawned;               // missiles launched in this wave
    uint8_t wave_lost;             // items lost in this wave
    uint32_t next_spawn;
    uint32_t score, best;
    uint32_t best_before;          // the best score when this game started (a higher one now is a new best)
    uint8_t saved;                 // MD_CLEAR: items that earned the bonus
    // His eyes.
    uint8_t heat;                  // percent, as of heat_at (it cools from there)
    uint32_t heat_at;
    uint32_t hot_at;               // overheated at hot_at, until hot_at + MD_OVERHEAT_MS (hot)
    bool hot;
    uint32_t stun_at;              // stunned at stun_at, until stun_at + MD_STUN_MS (stunned)
    bool stunned;
    // Shots.
    uint8_t shots;                 // shots so far (the next shot's id)
    uint8_t shot_kills[MD_SHOT_RING]; // kills of shot id % MD_SHOT_RING
    uint32_t shot_at;              // the last shot: when and where
    int16_t shot_x, shot_y;
    uint8_t streak;                // missiles shot down in a row without a missed shot
    uint32_t smug_at;              // when the streak reached MD_SMUG_STREAK
    // The last points scored (shown where they were made).
    uint16_t pop_points;
    uint8_t pop_mult;
    int16_t pop_x, pop_y;
    uint32_t pop_at;
    // How he feels.
    uint8_t lost_item;             // the last item lost, at lost_at
    uint32_t lost_at;
    bool panic;
    uint32_t panic_at;
    uint8_t say;                   // md_say_t, since say_at
    uint8_t say_variant;
    uint32_t say_at;
    uint32_t touched_at;           // the last touch (md_start counts as one)
    uint32_t stepped;              // the world is advanced to here
    uint32_t rng;
} md_t;

typedef enum {
    MD_FIRED,
    MD_REFUSED_HOT,     // overheated: he says so
    MD_REFUSED_STUNNED, // dazed: he says so
    MD_IGNORED,         // not now (no wave on, the wave is cleared, the game is over)
} md_fire_t;

// Boot: no game, best score 0. seed 0 picks a fixed one.
void md_init(md_t *g, uint32_t seed);

// A new game (entering it, or RETRY): wave 1, every item standing, score 0. The best score stays.
void md_start(md_t *g, uint32_t now);

// No game (leaving it). The best score stays.
void md_stop(md_t *g);

// A tap at (x, y): a shot, if he can (see md_fire_t). y is kept inside MD_FIRE_MIN_Y..MD_FIRE_MAX_Y. The world is
// first advanced to now.
md_fire_t md_fire(md_t *g, int x, int y, uint32_t now);

// Any touch in the game (the game-over screen closes after MD_OVER_IDLE_MS without one).
void md_touched(md_t *g, uint32_t now);

// Advances the world to now. Returns ms until something changes on its own (MD_FRAME_MS while anything moves);
// UINT32_MAX when off.
uint32_t md_tick(md_t *g, uint32_t now);

// Where a flying missile's head is at now (t clamped to its fall).
void md_missile_head(const md_missile_t *m, uint32_t now, int *x, int *y);

// A blast's radius at now (0 once it is over).
int md_blast_radius(const md_blast_t *b, uint32_t now);

// The heat of his eyes at now, percent.
int md_heat(const md_t *g, uint32_t now);

int md_items_left(const md_t *g);

// The wave's numbers (md_tuning.h): missiles in it, at once, fall time, the pause between launches.
typedef struct {
    uint8_t missiles, at_once;
    uint16_t fall_ms, gap_min_ms, gap_max_ms;
} md_wave_t;
md_wave_t md_wave(int wave);

// RETRY and QUIT show (the game is over and their moment has come).
bool md_buttons(const md_t *g, uint32_t now);

// The time the world is shown at: now, or, once the game is over, the moment it was lost (the frozen picture).
uint32_t md_world_ms(const md_t *g, uint32_t now);

// Nobody touched the game-over screen for MD_OVER_IDLE_MS since its buttons appeared: the caller closes the game.
bool md_bored(const md_t *g, uint32_t now);

// What he says now (one or two lines, split by '\n'), or NULL. A line shows for MD_SAY_MS; his last words on the
// game-over screen stay up. Strings live for ever.
const char *md_line(const md_t *g, uint32_t now);

// Every line md_line can return (host tests check they fit his bubble).
extern const char *const md_lines[];
extern const int md_line_count;

#ifdef __cplusplus
}
#endif
