// Tic-Tac-Toe against Buddy: see ttt.h. Pure logic, no allocation; the search is at most 9 plies deep.
#include "ttt.h"

#include <stddef.h>

// The eight lines of three, as cell triples.
static const uint8_t k_lines[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8}, // rows
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, // columns
    {0, 4, 8}, {2, 4, 6},            // diagonals
};

#define RNG_FALLBACK 0x2545F491u
#define STREAK_CAP 100

// A win is worth WIN_SCORE minus the plies it takes, so the soonest win and the latest loss score best.
#define WIN_SCORE 10

// Free-running counter compare, safe over wrap-around (the same rule as buddy_reached).
static bool reached(uint32_t now, uint32_t t)
{
    return (int32_t)(now - t) >= 0;
}

// Strings live for ever; ttt_line returns these very pointers.
static const char s_bored[] = "BORED...";
static const char s_hey[] = "HEY, MY TURN!";
static const char s_me_first[] = "MY TURN FIRST!";
static const char s_hmm[] = "HMM...";
static const char s_you_start[] = "YOU START!";
static const char s_unbeatable[] = "I'M UNBEATABLE!";
static const char s_too_easy[] = "TOO EASY!";
static const char s_i_win[] = "I WIN!";
static const char s_serious[] = "OK. NOW I'M SERIOUS.";
static const char s_noooo[] = "NOOO...";
static const char s_draw[] = "HMM... A DRAW?";
static const char s_another[] = "ANOTHER ONE?";
static const char s_rematch[] = "REMATCH?";
static const char s_again[] = "AGAIN?";

const char *const ttt_lines[] = {
    s_bored,     s_hey,   s_me_first, s_hmm,      s_you_start, s_unbeatable, s_too_easy,
    s_i_win,     s_serious, s_noooo,  s_draw,     s_another,   s_rematch,    s_again,
};
const int ttt_line_count = (int)(sizeof(ttt_lines) / sizeof(ttt_lines[0]));

// --- Rules ---------------------------------------------------------------------------------------------------------

ttt_result_t ttt_judge(const uint8_t board[TTT_CELLS], uint16_t *line)
{
    if (line) {
        *line = 0;
    }
    for (int i = 0; i < 8; i++) {
        const uint8_t a = board[k_lines[i][0]];
        if (a != TTT_EMPTY && a == board[k_lines[i][1]] && a == board[k_lines[i][2]]) {
            if (line) {
                *line = (uint16_t)((1u << k_lines[i][0]) | (1u << k_lines[i][1]) | (1u << k_lines[i][2]));
            }
            return a == TTT_X ? TTT_X_WINS : TTT_O_WINS;
        }
    }
    for (int i = 0; i < TTT_CELLS; i++) {
        if (board[i] == TTT_EMPTY) {
            return TTT_OPEN;
        }
    }
    return TTT_DRAW;
}

// --- Search --------------------------------------------------------------------------------------------------------

// The value for `who`, to move, of the position b (modified and restored), `depth` plies below the root.
static int negamax(uint8_t b[TTT_CELLS], uint8_t who, int depth, int alpha, int beta)
{
    const ttt_result_t r = ttt_judge(b, NULL);
    if (r == TTT_DRAW) {
        return 0;
    }
    // The side that just moved made the line, so `who` has lost it.
    if (r != TTT_OPEN) {
        return -(WIN_SCORE - depth);
    }
    const uint8_t other = who == TTT_X ? TTT_O : TTT_X;
    int best = -100;
    for (int c = 0; c < TTT_CELLS; c++) {
        if (b[c] != TTT_EMPTY) {
            continue;
        }
        b[c] = who;
        const int v = -negamax(b, other, depth + 1, -beta, -alpha);
        b[c] = TTT_EMPTY;
        if (v > best) {
            best = v;
        }
        if (v > alpha) {
            alpha = v;
        }
        if (alpha >= beta) {
            break;
        }
    }
    return best;
}

int ttt_best_move(const uint8_t board[TTT_CELLS], ttt_mark_t who)
{
    if (who != TTT_X && who != TTT_O) {
        return -1;
    }
    if (ttt_judge(board, NULL) != TTT_OPEN) {
        return -1;
    }
    uint8_t b[TTT_CELLS];
    for (int i = 0; i < TTT_CELLS; i++) {
        b[i] = board[i];
    }
    const uint8_t other = who == TTT_X ? TTT_O : TTT_X;
    int best_cell = -1;
    int best = -100;
    for (int c = 0; c < TTT_CELLS; c++) {
        if (b[c] != TTT_EMPTY) {
            continue;
        }
        b[c] = (uint8_t)who;
        // Window below `best`: a move that only ties it fails low, so the lowest cell stays.
        const int v = -negamax(b, other, 1, -WIN_SCORE - 1, -best);
        b[c] = TTT_EMPTY;
        if (v > best) {
            best = v;
            best_cell = c;
        }
    }
    return best_cell;
}

// --- Buddy's play --------------------------------------------------------------------------------------------------

static uint32_t rng_next(uint32_t *rng)
{
    uint32_t x = *rng ? *rng : RNG_FALLBACK; // xorshift32
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0) {
        x = RNG_FALLBACK;
    }
    *rng = x;
    return x;
}

// The first free cell where `who` would complete a line, or -1.
static int winning_cell(const uint8_t board[TTT_CELLS], uint8_t who)
{
    uint8_t b[TTT_CELLS];
    for (int i = 0; i < TTT_CELLS; i++) {
        b[i] = board[i];
    }
    for (int c = 0; c < TTT_CELLS; c++) {
        if (b[c] != TTT_EMPTY) {
            continue;
        }
        b[c] = who;
        const ttt_result_t r = ttt_judge(b, NULL);
        b[c] = TTT_EMPTY;
        if (r == (who == TTT_X ? TTT_X_WINS : TTT_O_WINS)) {
            return c;
        }
    }
    return -1;
}

// One of the best cells for O, chosen at random among all the equally best (variety, not a fixed reply).
static int best_cell_random(const uint8_t board[TTT_CELLS], uint32_t *rng)
{
    uint8_t b[TTT_CELLS];
    for (int i = 0; i < TTT_CELLS; i++) {
        b[i] = board[i];
    }
    int cells[TTT_CELLS];
    int n = 0;
    int best = -100;
    for (int c = 0; c < TTT_CELLS; c++) {
        if (b[c] != TTT_EMPTY) {
            continue;
        }
        b[c] = TTT_O;
        const int v = -negamax(b, TTT_X, 1, -WIN_SCORE - 1, WIN_SCORE + 1);
        b[c] = TTT_EMPTY;
        if (v > best) {
            best = v;
            n = 0;
        }
        if (v == best) {
            cells[n++] = c;
        }
    }
    return n ? cells[rng_next(rng) % (uint32_t)n] : -1;
}

static int random_free_cell(const uint8_t board[TTT_CELLS], int free_cells, uint32_t *rng)
{
    int k = (int)(rng_next(rng) % (uint32_t)free_cells);
    for (int c = 0; c < TTT_CELLS; c++) {
        if (board[c] == TTT_EMPTY && k-- == 0) {
            return c;
        }
    }
    return -1;
}

// Chance (percent) of playing it right, per attitude (EVEN, SMUG, KEEN).
static const uint8_t k_take_win[3] = {95, 85, 100};
static const uint8_t k_block[3] = {75, 55, 90};
static const uint8_t k_best[3] = {70, 50, 90};

static bool roll(uint32_t *rng, int percent)
{
    return (int)(rng_next(rng) % 100u) < percent;
}

int ttt_ai_move(const uint8_t board[TTT_CELLS], ttt_attitude_t attitude, uint32_t *rng)
{
    int free_cells = 0;
    for (int i = 0; i < TTT_CELLS; i++) {
        free_cells += board[i] == TTT_EMPTY;
    }
    if (free_cells == 0) {
        return -1;
    }
    const int a = (attitude == TTT_SMUG || attitude == TTT_KEEN) ? (int)attitude : 0;

    // Opening: the centre or a corner (a full search of the empty board would only say the same, slowly).
    if (free_cells == TTT_CELLS) {
        return 2 * (int)(rng_next(rng) % 5u);
    }

    int cell = winning_cell(board, TTT_O);
    if (cell >= 0) {
        return roll(rng, k_take_win[a]) ? cell : random_free_cell(board, free_cells, rng);
    }
    cell = winning_cell(board, TTT_X);
    if (cell >= 0) {
        return roll(rng, k_block[a]) ? cell : random_free_cell(board, free_cells, rng);
    }
    if (roll(rng, k_best[a])) {
        return best_cell_random(board, rng);
    }
    return random_free_cell(board, free_cells, rng);
}

ttt_attitude_t ttt_attitude(const ttt_t *g)
{
    if (g->streak >= 2) {
        return TTT_SMUG;
    }
    if (g->streak <= -2) {
        return TTT_KEEN;
    }
    return TTT_EVEN;
}

// --- Session -------------------------------------------------------------------------------------------------------

static uint32_t think(ttt_t *g)
{
    return TTT_THINK_MIN_MS + rng_next(&g->rng) % (TTT_THINK_MAX_MS - TTT_THINK_MIN_MS + 1u);
}

void ttt_init(ttt_t *g, uint32_t seed)
{
    *g = (ttt_t){0};
    g->phase = TTT_OFF;
    g->you_next = true;
    g->rng = seed ? seed : RNG_FALLBACK;
    g->last = -1;
}

void ttt_start(ttt_t *g, uint32_t now)
{
    for (int i = 0; i < TTT_CELLS; i++) {
        g->board[i] = TTT_EMPTY;
    }
    g->result = TTT_OPEN;
    g->line = 0;
    g->moves = 0;
    g->last = -1;
    g->hey = false;
    g->paused = false;
    g->you_started = g->you_next;
    g->you_next = !g->you_next;
    g->round_at = g->phase_at = g->touched_at = now;
    if (g->you_started) {
        g->phase = TTT_YOUR_TURN;
    } else {
        g->phase = TTT_THINKING;
        g->move_at = now + TTT_OPENING_MS + think(g);
    }
}

void ttt_stop(ttt_t *g)
{
    g->phase = TTT_OFF;
    g->paused = false;
}

void ttt_pause(ttt_t *g, uint32_t now)
{
    if (g->phase != TTT_OFF && !g->paused) {
        g->paused = true;
        g->paused_at = now;
    }
}

bool ttt_resume(ttt_t *g, uint32_t now)
{
    if (!g->paused) {
        return false;
    }
    // The paused time did not happen for the round: every moment in it moves on by as much.
    const uint32_t d = now - g->paused_at;
    g->round_at += d;
    g->phase_at += d;
    g->move_at += d;
    g->last_at += d;
    g->touched_at += d;
    g->hey_at += d;
    g->paused = false;
    return true;
}

// The round is over: score it and start his reaction.
static void finish(ttt_t *g, ttt_result_t r, uint16_t line, uint32_t now)
{
    g->phase = TTT_REACTION;
    g->phase_at = now;
    g->result = (uint8_t)r;
    g->line = line;
    g->say = (uint8_t)(rng_next(&g->rng) % 2u);
    if (r == TTT_O_WINS) {
        if (g->buddy < UINT16_MAX) {
            g->buddy++;
        }
        g->streak = (int8_t)(g->streak > 0 ? (g->streak < STREAK_CAP ? g->streak + 1 : STREAK_CAP) : 1);
    } else if (r == TTT_X_WINS) {
        if (g->you < UINT16_MAX) {
            g->you++;
        }
        g->streak = (int8_t)(g->streak < 0 ? (g->streak > -STREAK_CAP ? g->streak - 1 : -STREAK_CAP) : -1);
    } else {
        if (g->draws < UINT16_MAX) {
            g->draws++;
        }
        g->streak = 0;
    }
}

// After a mark: the round ends (true), or it is the other side's turn (the caller sets that phase).
static bool judge_round(ttt_t *g, uint32_t now)
{
    uint16_t line;
    const ttt_result_t r = ttt_judge(g->board, &line);
    if (r == TTT_OPEN) {
        return false;
    }
    finish(g, r, line, now);
    return true;
}

bool ttt_play(ttt_t *g, int cell, uint32_t now)
{
    if (g->paused) {
        return false;
    }
    if (g->phase == TTT_THINKING) {
        g->hey = true;
        g->hey_at = now;
        return false;
    }
    if (g->phase != TTT_YOUR_TURN || cell < 0 || cell >= TTT_CELLS || g->board[cell] != TTT_EMPTY) {
        return false;
    }
    g->board[cell] = TTT_X;
    g->last = (int8_t)cell;
    g->last_at = now;
    g->moves++;
    if (!judge_round(g, now)) {
        g->phase = TTT_THINKING;
        g->phase_at = now;
        g->move_at = now + think(g);
        g->hey = false;
    }
    return true;
}

bool ttt_rematch(ttt_t *g, uint32_t now)
{
    if (g->phase != TTT_REMATCH || g->paused) {
        return false;
    }
    ttt_start(g, now);
    return true;
}

void ttt_touched(ttt_t *g, uint32_t now)
{
    g->touched_at = now;
}

bool ttt_bored(const ttt_t *g, uint32_t now)
{
    return g->phase != TTT_OFF && !g->paused && now - g->touched_at >= TTT_IDLE_MS;
}

// The smaller of *next and the time left until `t + span` (if that is still ahead of `now`).
static void soonest(uint32_t *next, uint32_t now, uint32_t t, uint32_t span)
{
    const uint32_t elapsed = now - t;
    if (elapsed < span && span - elapsed < *next) {
        *next = span - elapsed;
    }
}

uint32_t ttt_tick(ttt_t *g, uint32_t now)
{
    if (g->phase == TTT_OFF) {
        return UINT32_MAX;
    }
    if (g->paused) { // frozen; dropped if it waits too long
        if (now - g->paused_at >= TTT_PAUSE_MS) {
            ttt_stop(g);
            return UINT32_MAX;
        }
        return TTT_PAUSE_MS - (now - g->paused_at);
    }
    if (g->phase == TTT_THINKING && reached(now, g->move_at)) {
        const int c = ttt_ai_move(g->board, ttt_attitude(g), &g->rng);
        if (c >= 0) {
            g->board[c] = TTT_O;
            g->last = (int8_t)c;
            g->last_at = now;
            g->moves++;
        }
        if (!judge_round(g, now)) {
            g->phase = TTT_YOUR_TURN;
            g->phase_at = now;
        }
    } else if (g->phase == TTT_REACTION && now - g->phase_at >= TTT_REACTION_MS) {
        g->phase = TTT_REMATCH;
        g->phase_at += TTT_REACTION_MS;
    }

    uint32_t next = UINT32_MAX;
    if (g->phase == TTT_THINKING) {
        next = g->move_at - now;
        if (g->hey) {
            soonest(&next, now, g->hey_at, TTT_HEY_MS);
        }
        if (g->moves == 0) {
            soonest(&next, now, g->round_at, TTT_OPENING_MS);
        }
    } else {
        if (g->phase == TTT_REACTION) {
            soonest(&next, now, g->phase_at, TTT_REACTION_MS);
        }
        if (g->phase == TTT_YOUR_TURN && g->you_started && g->moves == 0) {
            soonest(&next, now, g->round_at, TTT_OPENING_MS);
        }
        soonest(&next, now, g->touched_at, TTT_IDLE_MS - TTT_BORED_MS);
        soonest(&next, now, g->touched_at, TTT_IDLE_MS);
    }
    return next ? next : 1;
}

const char *ttt_line(const ttt_t *g, uint32_t now)
{
    if (g->phase == TTT_OFF || g->paused) {
        return NULL;
    }
    if (g->phase == TTT_THINKING) {
        if (g->hey && now - g->hey_at < TTT_HEY_MS) {
            return s_hey;
        }
        if (g->moves == 0 && now - g->round_at < TTT_OPENING_MS) {
            return s_me_first;
        }
        return s_hmm;
    }
    if (now - g->touched_at >= TTT_IDLE_MS - TTT_BORED_MS) {
        return s_bored;
    }
    switch (g->phase) {
    case TTT_YOUR_TURN:
        if (g->you_started && g->moves == 0 && now - g->round_at < TTT_OPENING_MS) {
            return s_you_start;
        }
        return NULL;
    case TTT_REACTION:
        if (g->result == TTT_O_WINS) {
            return g->streak >= 3 ? s_unbeatable : (g->say ? s_too_easy : s_i_win);
        }
        if (g->result == TTT_X_WINS) {
            return g->streak <= -2 ? s_serious : s_noooo;
        }
        return s_draw;
    default: // TTT_REMATCH
        if (g->result == TTT_O_WINS) {
            return s_another;
        }
        if (g->result == TTT_X_WINS) {
            return s_rematch;
        }
        return s_again;
    }
}
