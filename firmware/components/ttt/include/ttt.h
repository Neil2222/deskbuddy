// Tic-Tac-Toe against Buddy: the rules, Buddy's play and the rounds of a session. You are X, Buddy is O.
//
// Pure logic: no pixels (menu_ttt.h draws the board), no hardware, no clock of its own. Time is a free-running
// millisecond counter (wrap-around safe). Everything is bounded: a 9-cell board, a few timestamps, a score.
//
// A round: who starts alternates (you start the first one after boot). On your turn a tap on an empty cell places
// your X; then Buddy thinks for a moment (TTT_THINK_MIN_MS..TTT_THINK_MAX_MS) and places his O. When a line of
// three is made or the board is full, he reacts (TTT_REACTION_MS), then waits for a rematch with his post-game face.
// A rematch starts a fresh board; the score and his streak are kept until reboot (ttt_init).
//
// Buddy is good but beatable: he usually takes a win he sees, usually blocks yours and usually plays the best move
// (a full minimax search), but now and then he plays a random move instead. How often depends on his attitude:
// smug after two or more wins in a row (sloppier), keen after two or more losses in a row (sharper).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TTT_CELLS 9 // row by row: 0 1 2 / 3 4 5 / 6 7 8

// Timings, in ms.
#define TTT_OPENING_MS 1200    // a new round: his opening line ("YOU START!") before he moves first
#define TTT_THINK_MIN_MS 600   // his thinking pause before each move
#define TTT_THINK_MAX_MS 1500
#define TTT_HEY_MS 1200        // "HEY, MY TURN!" after a tap while he thinks
#define TTT_REACTION_MS 2500   // celebrating / disappointed / puzzled, then the rematch question
#define TTT_IDLE_MS 120000     // no touch this long: he gets bored and the game ends (ttt_bored)
#define TTT_BORED_MS 4000      // ...saying "BORED..." for this last part of it

typedef enum {
    TTT_EMPTY,
    TTT_X, // you
    TTT_O, // Buddy
} ttt_mark_t;

typedef enum {
    TTT_OPEN,   // still being played
    TTT_X_WINS, // you won
    TTT_O_WINS, // Buddy won
    TTT_DRAW,
} ttt_result_t;

typedef enum {
    TTT_OFF,       // no game (ttt_stop, or not started)
    TTT_YOUR_TURN,
    TTT_THINKING,  // Buddy's turn: he moves at move_at
    TTT_REACTION,  // the round is over (result); he reacts until phase_at + TTT_REACTION_MS
    TTT_REMATCH,   // ...then waits for ttt_rematch()
} ttt_phase_t;

typedef enum {
    TTT_EVEN, // his normal self
    TTT_SMUG, // won two or more in a row: cocky, so sloppier
    TTT_KEEN, // lost two or more in a row: competitive, so sharper
} ttt_attitude_t;

typedef struct {
    uint8_t board[TTT_CELLS]; // ttt_mark_t
    uint8_t phase;            // ttt_phase_t
    uint8_t result;           // ttt_result_t (TTT_OPEN until the round ends)
    uint16_t line;            // the winning line: bit n for cell n; 0 for none
    uint8_t moves;            // marks on the board
    int8_t last;              // the cell marked last; -1: none yet this round
    bool you_started;         // this round
    bool you_next;            // who starts the next round
    uint8_t say;              // which variant of his reaction line (chosen when the round ends)
    uint32_t round_at;        // when this round started
    uint32_t phase_at;        // when the phase started
    uint32_t move_at;         // TTT_THINKING: when he places his O
    uint32_t last_at;         // when `last` was marked
    uint32_t touched_at;      // the last touch in the game (ttt_touched; ttt_start counts as one)
    uint32_t hey_at;          // a tap while he was thinking...
    bool hey;                 // ...if any this turn
    // The session, kept until reboot.
    uint16_t you, buddy, draws; // rounds won by you, by him, drawn
    int8_t streak;              // > 0: his wins in a row; < 0: his losses in a row; 0 after a draw
    uint32_t rng;
} ttt_t;

// --- Rules and play (pure functions of a board) -------------------------------------------------------------------

// The board's result; *line (if not NULL) gets the winning line's cells as a bit mask (0 if none).
ttt_result_t ttt_judge(const uint8_t board[TTT_CELLS], uint16_t *line);

// A best move for `who` (TTT_X or TTT_O) by full minimax (a win soonest, a loss latest); -1 if the board is full or
// already decided. Among equally good moves the lowest cell. Bounded recursion (at most 9 deep), no allocation.
int ttt_best_move(const uint8_t board[TTT_CELLS], ttt_mark_t who);

// Buddy's move (O) with his attitude: see the header comment. *rng is advanced. -1 if no cell is free.
int ttt_ai_move(const uint8_t board[TTT_CELLS], ttt_attitude_t attitude, uint32_t *rng);

// His attitude from the session's streak.
ttt_attitude_t ttt_attitude(const ttt_t *g);

// --- A session ----------------------------------------------------------------------------------------------------

// Boot: no game, score 0, you start the first round. seed 0 picks a fixed one.
void ttt_init(ttt_t *g, uint32_t seed);

// Starts a round on a fresh board (entering the game, and every rematch). Keeps the score; who starts alternates.
// If Buddy starts, he moves after his opening line and a thinking pause.
void ttt_start(ttt_t *g, uint32_t now);

// Ends the game (leaving it): the round in progress is dropped, not counted. The score is kept.
void ttt_stop(ttt_t *g);

// Your X on `cell`. Returns false (nothing changes) if it is not your turn, the cell is taken or out of range. A tap
// on a cell while he thinks also returns false, and makes him say "HEY, MY TURN!" (ttt_line).
bool ttt_play(ttt_t *g, int cell, uint32_t now);

// Another round, from the rematch question (TTT_REMATCH only). Returns false otherwise.
bool ttt_rematch(ttt_t *g, uint32_t now);

// A touch in the game: the idle timer starts over.
void ttt_touched(ttt_t *g, uint32_t now);

// Advances to now: he moves when his pause is over, the reaction gives way to the rematch question. Returns ms until
// something changes on its own (his move, the end of the reaction, a different line, boredom); UINT32_MAX when off.
uint32_t ttt_tick(ttt_t *g, uint32_t now);

// No touch for TTT_IDLE_MS while the game is on: the caller ends it (ttt_stop and leaves the game screen).
bool ttt_bored(const ttt_t *g, uint32_t now);

// What Buddy says now (his speech bubble), or NULL. Strings live for ever (compare by pointer).
const char *ttt_line(const ttt_t *g, uint32_t now);

// Every line ttt_line can return (host tests check they fit his bubble).
extern const char *const ttt_lines[];
extern const int ttt_line_count;

#ifdef __cplusplus
}
#endif
