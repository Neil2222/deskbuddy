// Tic-Tac-Toe (components/ttt): rules, the minimax, Buddy's imperfect play and the rounds of a session.
#include <stdint.h>
#include <string.h>

#include "test.h"
#include "ttt.h"

#define X TTT_X
#define O TTT_O
#define E TTT_EMPTY

static const uint8_t k_lines[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6},
};

static uint32_t next_rand(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

void test_ttt_judge_finds_every_line_and_draws(void)
{
    for (int who = X; who <= O; who++) {
        for (int l = 0; l < 8; l++) {
            uint8_t b[TTT_CELLS] = {0};
            for (int k = 0; k < 3; k++) {
                b[k_lines[l][k]] = (uint8_t)who;
            }
            uint16_t line = 0xFFFF;
            CHECK_EQ(ttt_judge(b, &line), who == X ? TTT_X_WINS : TTT_O_WINS);
            CHECK_EQ(line, (1 << k_lines[l][0]) | (1 << k_lines[l][1]) | (1 << k_lines[l][2]));
        }
    }
    uint8_t open[TTT_CELLS] = {X, O, X, O, X, O, O, X, E};
    uint16_t line = 0xFFFF;
    CHECK_EQ(ttt_judge(open, &line), TTT_OPEN);
    CHECK_EQ(line, 0);
    uint8_t draw[TTT_CELLS] = {X, O, X, X, O, O, O, X, X};
    CHECK_EQ(ttt_judge(draw, &line), TTT_DRAW);
    CHECK_EQ(line, 0);
    uint8_t empty[TTT_CELLS] = {0};
    CHECK_EQ(ttt_judge(empty, NULL), TTT_OPEN);
    // A win on a full board is a win, not a draw.
    uint8_t full_win[TTT_CELLS] = {X, O, X, O, X, O, O, X, X};
    CHECK_EQ(ttt_judge(full_win, NULL), TTT_X_WINS);
}

void test_ttt_best_move_wins_blocks_and_knows_positions(void)
{
    uint8_t win[TTT_CELLS] = {O, O, E, X, X, E, E, E, E};
    CHECK_EQ(ttt_best_move(win, O), 2); // wins at once rather than blocking at 5
    CHECK_EQ(ttt_best_move(win, X), 5);
    uint8_t block[TTT_CELLS] = {X, X, E, E, O, E, E, E, E};
    CHECK_EQ(ttt_best_move(block, O), 2);
    // O must answer a corner opening in the centre.
    uint8_t corner[TTT_CELLS] = {X, E, E, E, E, E, E, E, E};
    CHECK_EQ(ttt_best_move(corner, O), 4);
    // X in a corner, O on an edge: X can force a win with the opposite... any best move must still win.
    uint8_t fork[TTT_CELLS] = {X, E, E, E, O, E, E, E, X};
    CHECK_EQ(ttt_best_move(fork, O), 1); // an edge (not a corner, which loses to a fork); lowest such cell
    // Soonest win: two wins available, both at once; lowest cell.
    uint8_t two[TTT_CELLS] = {O, E, O, E, O, E, E, E, E};
    CHECK_EQ(ttt_best_move(two, O), 1);
    // Full and decided boards: none.
    uint8_t draw[TTT_CELLS] = {X, O, X, X, O, O, O, X, X};
    CHECK_EQ(ttt_best_move(draw, O), -1);
    uint8_t won[TTT_CELLS] = {X, X, X, O, O, E, E, E, E};
    CHECK_EQ(ttt_best_move(won, O), -1);
    CHECK_EQ(ttt_best_move(won, X), -1);
}

void test_ttt_perfect_play_draws(void)
{
    uint8_t b[TTT_CELLS] = {0};
    uint8_t who = X;
    for (int i = 0; i < TTT_CELLS; i++) {
        const int c = ttt_best_move(b, (ttt_mark_t)who);
        CHECK(c >= 0 && b[c] == E);
        b[c] = who;
        who = who == X ? O : X;
    }
    CHECK_EQ(ttt_judge(b, NULL), TTT_DRAW);
}

void test_ttt_ai_plays_only_free_cells(void)
{
    uint32_t s = 12345;
    for (int n = 0; n < 5000; n++) {
        uint8_t b[TTT_CELLS] = {0};
        const int marks = (int)(next_rand(&s) % 8);
        for (int k = 0; k < marks; k++) {
            int c;
            do {
                c = (int)(next_rand(&s) % TTT_CELLS);
            } while (b[c] != E);
            b[c] = (k & 1) ? O : X;
        }
        uint32_t rng = next_rand(&s);
        const int c = ttt_ai_move(b, (ttt_attitude_t)(n % 3), &rng);
        CHECK(c >= 0 && c < TTT_CELLS);
        CHECK_EQ(b[c], E);
        CHECK(rng != 0);
    }
    uint8_t full[TTT_CELLS] = {X, O, X, X, O, O, O, X, X};
    uint32_t rng = 1;
    CHECK_EQ(ttt_ai_move(full, TTT_EVEN, &rng), -1);
}

// A whole round Buddy (O) against X; x_perfect: X plays the best move, else at random. Returns the result.
static ttt_result_t play_round(uint32_t *rng, ttt_attitude_t att, bool buddy_first, bool x_perfect)
{
    uint8_t b[TTT_CELLS] = {0};
    bool buddy = buddy_first;
    while (ttt_judge(b, NULL) == TTT_OPEN) {
        if (buddy) {
            b[ttt_ai_move(b, att, rng)] = O;
        } else if (x_perfect) {
            b[ttt_best_move(b, X)] = X;
        } else {
            int c;
            do {
                c = (int)(next_rand(rng) % TTT_CELLS);
            } while (b[c] != E);
            b[c] = X;
        }
        buddy = !buddy;
    }
    return ttt_judge(b, NULL);
}

typedef struct {
    int buddy, you, draw;
} tally_t;

static tally_t tally(ttt_attitude_t att, bool x_perfect, int games)
{
    tally_t t = {0, 0, 0};
    for (int i = 0; i < games; i++) {
        uint32_t rng = 0x9E3779B9u * (uint32_t)(i + 1);
        switch (play_round(&rng, att, (i & 1) != 0, x_perfect)) {
        case TTT_O_WINS:
            t.buddy++;
            break;
        case TTT_X_WINS:
            t.you++;
            break;
        default:
            t.draw++;
            break;
        }
    }
    return t;
}

void test_ttt_buddy_is_good_but_beatable(void)
{
    const tally_t perfect = tally(TTT_EVEN, true, 1000);
    printf("    ttt EVEN vs perfect X: buddy %d, you %d, draws %d\n", perfect.buddy, perfect.you, perfect.draw);
    CHECK(perfect.you >= 50);                      // beatable: at least 5% even against a perfect player
    CHECK(perfect.buddy + perfect.draw >= 300);    // but not a pushover
    const tally_t rnd = tally(TTT_EVEN, false, 1000);
    printf("    ttt EVEN vs random X: buddy %d, you %d, draws %d\n", rnd.buddy, rnd.you, rnd.draw);
    CHECK(rnd.buddy >= 600);
    CHECK(rnd.you <= 150);
}

void test_ttt_attitude_changes_his_strength(void)
{
    const tally_t smug = tally(TTT_SMUG, true, 1000);
    const tally_t keen = tally(TTT_KEEN, true, 1000);
    printf("    ttt SMUG vs perfect X: you %d; KEEN: you %d\n", smug.you, keen.you);
    CHECK(keen.you < smug.you);
}

void test_ttt_buddy_usually_takes_a_win(void)
{
    // O: 0 1 _ ; X: 3 4 _ ; Buddy to move with a win at 2.
    const uint8_t b[TTT_CELLS] = {O, O, E, X, X, E, E, X, E};
    int taken = 0;
    uint32_t rng = 777;
    for (int i = 0; i < 1000; i++) {
        taken += ttt_ai_move(b, TTT_EVEN, &rng) == 2;
    }
    printf("    ttt EVEN takes an immediate win %d/1000\n", taken);
    CHECK(taken >= 900);
    CHECK(taken < 1000); // and sometimes plays something else
    int keen = 0;
    for (int i = 0; i < 200; i++) {
        keen += ttt_ai_move(b, TTT_KEEN, &rng) == 2;
    }
    CHECK_EQ(keen, 200);
}

// --- A session ------------------------------------------------------------------------------------------------------

static void play_to_the_end(ttt_t *g, uint32_t *now)
{
    // X plays the first free cell; Buddy replies when due; stops once the reaction starts.
    for (int guard = 0; guard < 40 && g->phase != TTT_REACTION; guard++) {
        if (g->phase == TTT_YOUR_TURN) {
            for (int c = 0; c < TTT_CELLS; c++) {
                if (g->board[c] == E) {
                    ttt_play(g, c, *now);
                    break;
                }
            }
        } else {
            *now += TTT_THINK_MAX_MS + TTT_OPENING_MS;
            ttt_tick(g, *now);
        }
    }
}

void test_ttt_session_turns_and_thinking(void)
{
    ttt_t g;
    ttt_init(&g, 5);
    CHECK_EQ(g.phase, TTT_OFF);
    CHECK(ttt_line(&g, 0) == NULL);
    CHECK_EQ(ttt_tick(&g, 0), UINT32_MAX);
    CHECK(!ttt_play(&g, 4, 0));
    ttt_start(&g, 1000);
    CHECK_EQ(g.phase, TTT_YOUR_TURN);
    CHECK(g.you_started);
    CHECK(ttt_line(&g, 1000) != NULL && strcmp(ttt_line(&g, 1000), "YOU START!") == 0);
    CHECK(ttt_line(&g, 1000 + TTT_OPENING_MS) == NULL);
    CHECK(!ttt_play(&g, -1, 1100));
    CHECK(!ttt_play(&g, 9, 1100));
    CHECK(ttt_play(&g, 4, 1100));
    CHECK_EQ(g.board[4], X);
    CHECK_EQ(g.last, 4);
    CHECK_EQ(g.phase, TTT_THINKING);
    const uint32_t think = g.move_at - 1100;
    CHECK(think >= TTT_THINK_MIN_MS && think <= TTT_THINK_MAX_MS);
    CHECK(strcmp(ttt_line(&g, 1100), "HMM...") == 0);
    // A tap while he thinks: refused, he complains.
    CHECK(!ttt_play(&g, 0, 1300));
    CHECK_EQ(g.board[0], E);
    CHECK(strcmp(ttt_line(&g, 1300), "HEY, MY TURN!") == 0);
    CHECK(strcmp(ttt_line(&g, 1300 + TTT_HEY_MS), "HMM...") == 0);
    // The next change is his move (or the end of the complaint, if sooner); never 0.
    uint32_t next = ttt_tick(&g, 1300);
    CHECK(next >= 1 && next <= g.move_at - 1300);
    ttt_tick(&g, g.move_at - 1);
    CHECK_EQ(g.phase, TTT_THINKING);
    CHECK_EQ(g.moves, 1);
    CHECK_EQ(ttt_tick(&g, g.move_at - 1), 1);
    ttt_tick(&g, g.move_at);
    CHECK_EQ(g.phase, TTT_YOUR_TURN);
    CHECK_EQ(g.moves, 2);
    CHECK(g.last >= 0 && g.board[g.last] == O);
    CHECK(!ttt_play(&g, g.last, g.move_at)); // taken
    CHECK(ttt_tick(&g, g.move_at) >= 1);
}

void test_ttt_session_starters_alternate_and_he_opens_late(void)
{
    ttt_t g;
    ttt_init(&g, 9);
    ttt_start(&g, 100);
    CHECK(g.you_started);
    ttt_stop(&g);
    CHECK_EQ(g.phase, TTT_OFF);
    CHECK_EQ(g.you + g.buddy + g.draws, 0); // dropped, not counted
    ttt_start(&g, 5000);
    CHECK(!g.you_started);
    CHECK_EQ(g.phase, TTT_THINKING);
    CHECK(strcmp(ttt_line(&g, 5000), "MY TURN FIRST!") == 0);
    const uint32_t d = g.move_at - 5000;
    CHECK(d >= TTT_OPENING_MS + TTT_THINK_MIN_MS && d <= TTT_OPENING_MS + TTT_THINK_MAX_MS);
    CHECK(strcmp(ttt_line(&g, 5000 + TTT_OPENING_MS), "HMM...") == 0);
    ttt_tick(&g, g.move_at - 1);
    CHECK_EQ(g.moves, 0);
    ttt_tick(&g, g.move_at);
    CHECK_EQ(g.moves, 1);
    CHECK_EQ(g.phase, TTT_YOUR_TURN);
    CHECK(ttt_line(&g, g.move_at + 10) == NULL);
    ttt_stop(&g);
    ttt_start(&g, 9000);
    CHECK(g.you_started);
}

void test_ttt_session_reaction_rematch_and_score(void)
{
    ttt_t g;
    ttt_init(&g, 3);
    uint32_t now = 1000;
    ttt_start(&g, now);
    CHECK(!ttt_rematch(&g, now)); // not at the rematch question
    play_to_the_end(&g, &now);
    CHECK_EQ(g.phase, TTT_REACTION);
    CHECK(g.result != TTT_OPEN);
    CHECK_EQ(g.you + g.buddy + g.draws, 1);
    const uint32_t end = g.phase_at;
    const uint32_t next = ttt_tick(&g, end + 100);
    CHECK_EQ(next, TTT_REACTION_MS - 100);
    CHECK(ttt_line(&g, end + 100) != NULL);
    CHECK(!ttt_rematch(&g, end + 100));
    ttt_tick(&g, end + TTT_REACTION_MS - 1);
    CHECK_EQ(g.phase, TTT_REACTION);
    ttt_tick(&g, end + TTT_REACTION_MS);
    CHECK_EQ(g.phase, TTT_REMATCH);
    CHECK(ttt_line(&g, end + TTT_REACTION_MS) != NULL);
    CHECK(!ttt_play(&g, 0, end + TTT_REACTION_MS));
    const int total = g.you + g.buddy + g.draws;
    CHECK(ttt_rematch(&g, end + TTT_REACTION_MS + 50));
    CHECK_EQ(g.moves, 0);
    CHECK_EQ(g.result, TTT_OPEN);
    CHECK_EQ(g.line, 0);
    for (int i = 0; i < TTT_CELLS; i++) {
        CHECK_EQ(g.board[i], E);
    }
    CHECK_EQ(g.you + g.buddy + g.draws, total);
    CHECK(!g.you_started); // alternated
}

// Finishes a round with a forced result by setting up the board: X to play the winning/last cell.
static void round_result(ttt_t *g, ttt_result_t want, uint32_t now)
{
    ttt_start(g, now);
    if (!g->you_started) {
        // let him open, then it is our move
        ttt_tick(g, g->move_at);
    }
    static const uint8_t x_wins[TTT_CELLS] = {X, X, E, O, O, E, E, E, E};
    static const uint8_t o_wins[TTT_CELLS] = {X, X, E, O, O, E, E, X, E}; // 5 wins for O? no: O at 3,4 -> 5
    static const uint8_t draw[TTT_CELLS] = {X, O, X, X, O, O, O, X, E};
    const uint8_t *src = want == TTT_X_WINS ? x_wins : want == TTT_O_WINS ? o_wins : draw;
    memcpy(g->board, src, TTT_CELLS);
    g->phase = TTT_YOUR_TURN;
    g->moves = 4;
    if (want == TTT_X_WINS) {
        CHECK(ttt_play(g, 2, now + 10));
    } else if (want == TTT_DRAW) {
        g->moves = 8;
        CHECK(ttt_play(g, 8, now + 10));
    } else {
        // Hand him the win: he is to move with an immediate win at 5 (KEEN-level sureness not needed: force it).
        g->phase = TTT_THINKING;
        g->move_at = now + 10;
        uint32_t rng_probe = g->rng;
        // Find a state where he takes it: retry with the rng until he does (he takes it at least 85%).
        int guard = 0;
        while (ttt_ai_move(g->board, ttt_attitude(g), &rng_probe) != 5 && guard++ < 100) {
            g->rng = rng_probe;
        }
        ttt_tick(g, now + 10);
    }
    CHECK_EQ(g->result, want);
}

void test_ttt_session_streaks_attitude_and_lines(void)
{
    ttt_t g;
    ttt_init(&g, 11);
    uint32_t now = 1000;
    CHECK_EQ(ttt_attitude(&g), TTT_EVEN);
    round_result(&g, TTT_O_WINS, now);
    CHECK_EQ(g.streak, 1);
    CHECK_EQ(g.buddy, 1);
    CHECK_EQ(g.line, (1 << 3) | (1 << 4) | (1 << 5));
    CHECK_EQ(ttt_attitude(&g), TTT_EVEN);
    const char *l = ttt_line(&g, g.phase_at);
    CHECK(strcmp(l, "TOO EASY!") == 0 || strcmp(l, "I WIN!") == 0);
    now += 10000;
    round_result(&g, TTT_O_WINS, now);
    CHECK_EQ(g.streak, 2);
    CHECK_EQ(ttt_attitude(&g), TTT_SMUG);
    now += 10000;
    round_result(&g, TTT_O_WINS, now);
    CHECK_EQ(g.streak, 3);
    CHECK(strcmp(ttt_line(&g, g.phase_at), "I'M UNBEATABLE!") == 0);
    ttt_tick(&g, g.phase_at + TTT_REACTION_MS);
    CHECK(strcmp(ttt_line(&g, g.phase_at), "ANOTHER ONE?") == 0);
    now += 10000;
    round_result(&g, TTT_DRAW, now);
    CHECK_EQ(g.streak, 0);
    CHECK_EQ(g.draws, 1);
    CHECK_EQ(ttt_attitude(&g), TTT_EVEN);
    CHECK(strcmp(ttt_line(&g, g.phase_at), "HMM... A DRAW?") == 0);
    ttt_tick(&g, g.phase_at + TTT_REACTION_MS);
    CHECK(strcmp(ttt_line(&g, g.phase_at), "AGAIN?") == 0);
    now += 10000;
    round_result(&g, TTT_X_WINS, now);
    CHECK_EQ(g.streak, -1);
    CHECK_EQ(g.you, 1);
    CHECK(strcmp(ttt_line(&g, g.phase_at), "NOOO...") == 0);
    CHECK_EQ(g.line, 7);
    now += 10000;
    round_result(&g, TTT_X_WINS, now);
    CHECK_EQ(g.streak, -2);
    CHECK_EQ(ttt_attitude(&g), TTT_KEEN);
    CHECK(strcmp(ttt_line(&g, g.phase_at), "OK. NOW I'M SERIOUS.") == 0);
    ttt_tick(&g, g.phase_at + TTT_REACTION_MS);
    CHECK(strcmp(ttt_line(&g, g.phase_at), "REMATCH?") == 0);
    // Stopping keeps the score.
    ttt_stop(&g);
    CHECK_EQ(g.you, 2);
    CHECK_EQ(g.buddy, 3);
    CHECK_EQ(g.draws, 1);
    CHECK(ttt_line(&g, now) == NULL);
}

void test_ttt_session_gets_bored_without_touches(void)
{
    ttt_t g;
    ttt_init(&g, 21);
    ttt_start(&g, 50000);
    CHECK(!ttt_bored(&g, 50000));
    CHECK(!ttt_bored(&g, 50000 + TTT_IDLE_MS - 1));
    CHECK(ttt_bored(&g, 50000 + TTT_IDLE_MS));
    // Before the last TTT_BORED_MS no bored line; from there on, yes.
    CHECK(ttt_line(&g, 50000 + TTT_IDLE_MS - TTT_BORED_MS - 1) == NULL);
    CHECK(strcmp(ttt_line(&g, 50000 + TTT_IDLE_MS - TTT_BORED_MS), "BORED...") == 0);
    // The tick points at the start of boredom.
    CHECK_EQ(ttt_tick(&g, 50000 + TTT_OPENING_MS), TTT_IDLE_MS - TTT_BORED_MS - TTT_OPENING_MS);
    // A touch starts the timer over.
    ttt_touched(&g, 50000 + TTT_IDLE_MS - 10);
    CHECK(!ttt_bored(&g, 50000 + TTT_IDLE_MS + 10));
    CHECK(ttt_line(&g, 50000 + TTT_IDLE_MS + 10) == NULL);
    ttt_stop(&g);
    CHECK(!ttt_bored(&g, 50000 + 10 * TTT_IDLE_MS));
}

void test_ttt_session_survives_clock_wraparound(void)
{
    ttt_t g;
    ttt_init(&g, 31);
    const uint32_t t0 = UINT32_MAX - 300;
    ttt_start(&g, t0);
    CHECK(ttt_play(&g, 0, t0 + 10));
    CHECK_EQ(ttt_tick(&g, t0 + 11) > 0, 1);
    ttt_tick(&g, g.move_at - 1);
    CHECK_EQ(g.moves, 1);
    ttt_tick(&g, g.move_at);
    CHECK_EQ(g.moves, 2);
    CHECK(!ttt_bored(&g, g.move_at));
}

void test_ttt_lines_are_known_strings(void)
{
    CHECK(ttt_line_count > 0);
    for (int i = 0; i < ttt_line_count; i++) {
        CHECK(ttt_lines[i] != NULL && ttt_lines[i][0] != 0);
    }
    // Every line any scenario returns is listed.
    ttt_t g;
    ttt_init(&g, 41);
    uint32_t now = 0;
    for (int round = 0; round < 12; round++) {
        ttt_start(&g, now);
        for (int step = 0; step < 400; step++) {
            const char *l = ttt_line(&g, now);
            if (l) {
                bool found = false;
                for (int i = 0; i < ttt_line_count; i++) {
                    found |= ttt_lines[i] == l;
                }
                CHECK(found);
            }
            if (g.phase == TTT_YOUR_TURN) {
                ttt_play(&g, step % TTT_CELLS, now);
            }
            if (g.phase == TTT_THINKING && step % 3 == 0) {
                ttt_play(&g, 0, now);
            }
            if (g.phase == TTT_REMATCH) {
                break;
            }
            now += 97;
            ttt_tick(&g, now);
        }
    }
}

// Clock Mode pauses a round: while paused nothing moves (his move, the reaction, boredom, lines, taps); resumed, every
// timer has exactly as long to go as it had, also across the counter's wrap-around.
void test_ttt_pause_freezes_and_resumes_exactly(void)
{
    static const uint32_t k_starts[] = {1000, 0xFFFFF000u}; // the second wraps during the pause
    for (unsigned k = 0; k < sizeof(k_starts) / sizeof(k_starts[0]); k++) {
        ttt_t g;
        ttt_init(&g, 21);
        uint32_t now = k_starts[k];
        ttt_start(&g, now); // you start
        CHECK(ttt_play(&g, 4, now));
        CHECK_EQ(g.phase, TTT_THINKING);
        now += 200;
        const uint32_t left = g.move_at - now; // his thinking still to go
        ttt_touched(&g, now - 100000);         // nearly bored already
        ttt_pause(&g, now);
        CHECK(g.paused);
        const uint32_t paused_at = now;
        ttt_pause(&g, now + 5); // paused already: nothing changes
        CHECK_EQ(g.paused_at, paused_at);

        // Five minutes in the clock: no move, no boredom, no line, no taps; it only waits to be dropped.
        for (uint32_t t = 0; t < 5 * 60000u; t += 1000) {
            CHECK(ttt_tick(&g, now + t) <= TTT_PAUSE_MS);
            CHECK(!ttt_bored(&g, now + t));
            CHECK(ttt_line(&g, now + t) == NULL);
        }
        now += 5 * 60000u;
        CHECK(!ttt_play(&g, 0, now));
        CHECK(!g.hey); // a tap on a paused board is no protest either
        CHECK_EQ(g.moves, 1);
        CHECK_EQ(g.phase, TTT_THINKING);

        CHECK(ttt_resume(&g, now));
        CHECK(!g.paused);
        CHECK(!ttt_resume(&g, now)); // once
        CHECK_EQ(g.move_at - now, left);
        CHECK(ttt_line(&g, now) != NULL);  // "HMM..." again
        ttt_tick(&g, now + left - 1);
        CHECK_EQ(g.moves, 1);               // not a moment early...
        ttt_tick(&g, now + left);
        CHECK_EQ(g.moves, 2);               // ...and on time
        now += left;
        // The idle timer had 20 s to go when paused: still 20 s, not over at once.
        CHECK(!ttt_bored(&g, now + 19000 - left - 200));
        CHECK(ttt_bored(&g, now + 20000 - left));

        // Paused in his reaction: it runs out after the time it still had, then the rematch question.
        ttt_t r;
        ttt_init(&r, 3);
        uint32_t t = k_starts[k];
        ttt_start(&r, t);
        for (int guard = 0; guard < 40 && r.phase != TTT_REACTION; guard++) {
            if (r.phase == TTT_YOUR_TURN) {
                ttt_play(&r, ttt_best_move(r.board, TTT_X), t);
            } else {
                t = r.move_at;
                ttt_tick(&r, t);
            }
        }
        CHECK_EQ(r.phase, TTT_REACTION);
        const uint32_t over_at = r.phase_at;
        ttt_pause(&r, over_at + 1000);
        ttt_tick(&r, over_at + 1000 + 9 * 60000u);
        CHECK_EQ(r.phase, TTT_REACTION); // frozen, not over
        const uint32_t back = over_at + 1000 + 9 * 60000u;
        CHECK(ttt_resume(&r, back));
        ttt_tick(&r, back + TTT_REACTION_MS - 1000 - 1);
        CHECK_EQ(r.phase, TTT_REACTION);
        CHECK(!ttt_rematch(&r, back + 1));
        ttt_tick(&r, back + TTT_REACTION_MS - 1000);
        CHECK_EQ(r.phase, TTT_REMATCH);
        // A pause on the rematch question: the button does nothing until resumed.
        ttt_pause(&r, back + 5000);
        CHECK(!ttt_rematch(&r, back + 6000));
        CHECK(ttt_resume(&r, back + 7000));
        CHECK(ttt_rematch(&r, back + 7000));
        CHECK_EQ(r.moves, 0); // a fresh board
    }
}

// A round paused for TTT_PAUSE_MS is dropped, not counted: the score and the streak stay, the next round is fresh and
// the starters keep alternating. Stop and start (Back, then the game again) never resume anything.
void test_ttt_pause_expires_and_keeps_the_score(void)
{
    ttt_t g;
    ttt_init(&g, 8);
    uint32_t now = 1000;
    ttt_start(&g, now);
    play_to_the_end(&g, &now); // one round decided...
    const uint16_t you = g.you, buddy = g.buddy, draws = g.draws;
    const int8_t streak = g.streak;
    CHECK_EQ(you + buddy + draws, 1);
    now += TTT_REACTION_MS;
    ttt_tick(&g, now);
    CHECK(ttt_rematch(&g, now)); // ...the next one under way
    const bool second_by_you = g.you_started;
    if (g.phase == TTT_THINKING) {
        ttt_tick(&g, g.move_at);
        now = g.move_at;
    }
    CHECK(ttt_play(&g, ttt_best_move(g.board, TTT_X), now));
    ttt_pause(&g, now);
    ttt_tick(&g, now + TTT_PAUSE_MS - 1);
    CHECK(g.paused); // still kept
    CHECK_EQ(ttt_tick(&g, now + TTT_PAUSE_MS), UINT32_MAX);
    CHECK(!g.paused);
    CHECK_EQ(g.phase, TTT_OFF); // dropped...
    CHECK(!ttt_resume(&g, now + TTT_PAUSE_MS + 1));
    CHECK_EQ(g.you, you);       // ...not counted
    CHECK_EQ(g.buddy, buddy);
    CHECK_EQ(g.draws, draws);
    CHECK_EQ(g.streak, streak);
    ttt_start(&g, now + TTT_PAUSE_MS + 10);
    CHECK_EQ(g.moves, 0);
    CHECK_EQ(g.you_started, !second_by_you);

    // Back (stop) while paused: the round is gone, nothing to resume.
    ttt_pause(&g, now + TTT_PAUSE_MS + 20);
    ttt_stop(&g);
    CHECK(!g.paused);
    CHECK(!ttt_resume(&g, now + TTT_PAUSE_MS + 30));
    ttt_pause(&g, now); // no round on: nothing to pause
    CHECK(!g.paused);
}
