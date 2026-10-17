#include "md.h"

#include <string.h>

const int16_t md_item_x[MD_ITEMS] = {80, 210, 340, 684, 814, 944};

#define ROW(missiles, at_once, fall, gap_min, gap_max) {missiles, at_once, fall, gap_min, gap_max},
static const md_wave_t k_waves[] = {MD_WAVE_TABLE(ROW)};
#undef ROW
#define N_WAVES ((int)(sizeof(k_waves) / sizeof(k_waves[0])))

#define BLAST_MS (MD_BLAST_GROW_MS + MD_BLAST_HOLD_MS + MD_BLAST_SHRINK_MS)
#define CATCH_UP_MS 250 // a frame later than this: the world skips ahead instead of stepping through all of it

_Static_assert(MD_MAX_MISSILES <= 255 && MD_WAVE_MISSILES_MAX <= 255, "counts are bytes");
_Static_assert(MD_HEAT_PER_SHOT > 0 && MD_HEAT_PER_SHOT <= 100 && MD_COOL_PER_S > 0, "heat in percent");

// Two variants of every line, in md_say_t order from MD_SAY_START. Each line is at most 16 characters; the game-over
// lines have two of them.
const char *const md_lines[] = {
    "PROTECT THE DESK!", "NOT ON MY DESK!",  // START
    "HERE THEY COME!",   "INCOMING!",        // WAVE
    "DOUBLE!",           "TWO FOR ONE!",     // DOUBLE
    "TRIPLE!",           "DID YOU SEE THAT?", // TRIPLE
    "WARNING SHOT.",     "I MEANT THAT.",    // MISS
    "TOO EASY.",         "I'M ON FIRE!",     // SMUG
    "MY COFFEE!!",       "NOT THE COFFEE!",  // ITEM + 0: the mug
    "MY PLANT!",         "NOOO, MY PLANT!",  // the plant
    "NOT THE MONSTER!",  "MY MONSTER!!",     // the Monster can
    "MY LAPTOP!",        "MY WORK! ...OH.",  // the laptop
    "DUCK! ...NO!",      "NOT MY DUCK!",     // the rubber duck
    "MY DONUT...",       "THE DONUT!!",      // the donut
    "OW! OW! OW!",       "MY HEAD!",         // HIT
    "EYES... TOO HOT",   "OVERHEATING!",     // HOT
    "COOLING DOWN!",     "GIVE ME A SEC!",   // COOLING
    "SEEING STARS...",   "WHICH WAY IS UP?", // DAZED
    "A LITTLE HELP?",    "TOO MANY!",        // PANIC
    "WAVE CLEAR!",       "GOT THEM ALL!",    // CLEAR
    "NOT A SCRATCH!",    "FLAWLESS!",        // PERFECT
    "THEY HAD...\nSO MANY...", "I REGRET\nNOTHING.", // OVER
};
const int md_line_count = (int)(sizeof(md_lines) / sizeof(md_lines[0]));
_Static_assert(sizeof(md_lines) / sizeof(md_lines[0]) == 2 * (MD_SAY_COUNT - 1), "two lines for every md_say_t");

static bool reached(uint32_t now, uint32_t at)
{
    return (int32_t)(now - at) >= 0;
}

static uint32_t rnd(md_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

static int rnd_range(md_t *g, int lo, int hi) // lo..hi, inclusive
{
    return lo + (int)(rnd(g) % (uint32_t)(hi - lo + 1));
}

// A line; an ordinary one waits until the last one has had its time (force: a big moment, said at once).
static void say(md_t *g, md_say_t s, uint32_t now, bool force)
{
    if (!force && g->say != MD_SAY_NONE && now - g->say_at < MD_SAY_GAP_MS) {
        return;
    }
    g->say = (uint8_t)s;
    g->say_at = now;
    g->say_variant = (uint8_t)(rnd(g) & 1u);
}

static void add_score(md_t *g, uint32_t points)
{
    g->score = g->score + points > MD_SCORE_MAX ? MD_SCORE_MAX : g->score + points;
    g->best = g->score > g->best ? g->score : g->best;
}

md_wave_t md_wave(int wave)
{
    if (wave < 1) {
        wave = 1;
    }
    if (wave <= N_WAVES) {
        return k_waves[wave - 1];
    }
    md_wave_t w = k_waves[N_WAVES - 1];
    const int n = w.missiles + (wave - N_WAVES) * MD_WAVE_GROWTH;
    w.missiles = (uint8_t)(n > MD_WAVE_MISSILES_MAX ? MD_WAVE_MISSILES_MAX : n);
    return w;
}

void md_init(md_t *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0x2545F491u;
}

void md_start(md_t *g, uint32_t now)
{
    // A RETRY from the frozen game-over screen: its missiles do not vanish all at once (one very large redraw) but
    // shrink back into where they stood, as a trail does after its missile has gone.
    md_missile_t left[MD_MAX_MISSILES];
    const uint32_t frozen = md_world_ms(g, now);
    const bool was_on = g->phase != MD_OFF;
    for (int i = 0; i < MD_MAX_MISSILES; i++) {
        left[i] = g->missile[i];
        md_missile_t *m = &left[i];
        if (!was_on || m->state == MD_MISSILE_FREE) {
            m->state = MD_MISSILE_FREE;
        } else if (m->state == MD_MISSILE_FLYING) {
            int x, y;
            md_missile_head(m, frozen, &x, &y);
            m->state = MD_MISSILE_FADING;
            m->shot_down = false;
            m->ex = (int16_t)x;
            m->ey = (int16_t)y;
            m->end_at = now;
        } else {
            m->end_at = now - (frozen - m->end_at); // the rest of its fade, from now
        }
    }
    const uint32_t best = g->best, rng = g->rng;
    memset(g, 0, sizeof(*g));
    memcpy(g->missile, left, sizeof(left));
    g->best = best;
    g->best_before = best;
    g->rng = rng ? rng : 0x2545F491u;
    g->phase = MD_READY;
    g->wave = 1;
    g->phase_at = now;
    g->items = (1u << MD_ITEMS) - 1;
    g->heat_at = now;
    g->touched_at = now;
    g->stepped = now;
    say(g, MD_SAY_START, now, true);
}

void md_stop(md_t *g)
{
    g->phase = MD_OFF;
}

void md_touched(md_t *g, uint32_t now)
{
    g->touched_at = now;
}

void md_missile_head(const md_missile_t *m, uint32_t now, int *x, int *y)
{
    uint32_t t = now - m->born;
    if ((int32_t)t < 0) {
        t = 0;
    }
    if (t > m->fall_ms) {
        t = m->fall_ms;
    }
    *x = m->x0 + (int)((int32_t)(m->x1 - m->x0) * (int32_t)t / (int32_t)m->fall_ms);
    *y = m->y0 + (int)((int32_t)(m->y1 - m->y0) * (int32_t)t / (int32_t)m->fall_ms);
}

static int max_radius(uint8_t kind)
{
    return kind == MD_BLAST_SHOT ? MD_BLAST_RADIUS : kind == MD_BLAST_CHAIN ? MD_CHAIN_RADIUS : MD_IMPACT_RADIUS;
}

int md_blast_radius(const md_blast_t *b, uint32_t now)
{
    if (!b->on || !reached(now, b->born)) {
        return 0;
    }
    uint32_t t = now - b->born;
    const int r = max_radius(b->kind);
    if (t < MD_BLAST_GROW_MS) {
        return 1 + (r - 1) * (int)t / MD_BLAST_GROW_MS;
    }
    t -= MD_BLAST_GROW_MS;
    if (t < MD_BLAST_HOLD_MS) {
        return r;
    }
    t -= MD_BLAST_HOLD_MS;
    if (t < MD_BLAST_SHRINK_MS) {
        return r - r * (int)t / MD_BLAST_SHRINK_MS;
    }
    return 0;
}

int md_heat(const md_t *g, uint32_t now)
{
    const uint32_t t = now - g->heat_at;
    const uint32_t cooled = t >= 100000u ? 100u : t * MD_COOL_PER_S / 1000u;
    return cooled >= g->heat ? 0 : (int)(g->heat - cooled);
}

int md_items_left(const md_t *g)
{
    int n = 0;
    for (int i = 0; i < MD_ITEMS; i++) {
        n += (g->items >> i) & 1u;
    }
    return n;
}

bool md_buttons(const md_t *g, uint32_t now)
{
    return g->phase == MD_OVER && now - g->phase_at >= MD_OVER_BUTTONS_MS;
}

uint32_t md_world_ms(const md_t *g, uint32_t now)
{
    return g->phase == MD_OVER ? g->phase_at : now;
}

// The game-over screen's quiet time starts when its buttons appear, or at a touch after that.
static uint32_t idle_since(const md_t *g)
{
    const uint32_t shown = g->phase_at + MD_OVER_BUTTONS_MS;
    return (int32_t)(g->touched_at - shown) > 0 ? g->touched_at : shown;
}

bool md_bored(const md_t *g, uint32_t now)
{
    return md_buttons(g, now) && now - idle_since(g) >= MD_OVER_IDLE_MS;
}

const char *md_line(const md_t *g, uint32_t now)
{
    if (g->phase == MD_OFF || g->say == MD_SAY_NONE || g->say >= MD_SAY_COUNT) {
        return NULL;
    }
    // His last words stay up on the game-over screen; everything else for MD_SAY_MS.
    if (now - g->say_at >= MD_SAY_MS && !(g->say == MD_SAY_OVER && g->phase == MD_OVER)) {
        return NULL;
    }
    return md_lines[2 * (g->say - 1) + (g->say_variant & 1u)];
}

// --- The world ------------------------------------------------------------------------------------------------------

static void add_blast(md_t *g, md_blast_kind_t kind, uint8_t shot, int x, int y, uint32_t t)
{
    int slot = 0;
    for (int i = 0; i < MD_MAX_BLASTS; i++) {
        if (!g->blast[i].on) {
            slot = i;
            break;
        }
        if (t - g->blast[i].born > t - g->blast[slot].born) { // full: the oldest gives way
            slot = i;
        }
    }
    g->blast[slot] = (md_blast_t){true, (uint8_t)kind, shot, (int16_t)x, (int16_t)y, t};
}

static void launch(md_t *g, uint32_t t)
{
    const md_wave_t w = md_wave(g->wave);
    if (g->spawned >= w.missiles || !reached(t, g->next_spawn)) {
        return;
    }
    int flying = 0, slot = -1;
    for (int i = 0; i < MD_MAX_MISSILES; i++) {
        flying += g->missile[i].state == MD_MISSILE_FLYING;
        if (slot < 0 && g->missile[i].state == MD_MISSILE_FREE) {
            slot = i;
        }
    }
    if (flying >= w.at_once || slot < 0) {
        return; // the next one goes as soon as there is room
    }
    // Mostly a desk item still standing; now and then Buddy himself.
    int target = MD_TARGET_BUDDY;
    const int left = md_items_left(g);
    if (left > 0 && rnd(g) % MD_BUDDY_ONE_IN != 0) {
        int k = (int)(rnd(g) % (uint32_t)left);
        for (int i = 0; i < MD_ITEMS; i++) {
            if (((g->items >> i) & 1u) && k-- == 0) {
                target = i;
                break;
            }
        }
    }
    md_missile_t *m = &g->missile[slot];
    *m = (md_missile_t){0};
    m->state = MD_MISSILE_FLYING;
    m->target = (uint8_t)target;
    m->x0 = (int16_t)rnd_range(g, 24, MD_W - 24);
    m->y0 = MD_SKY_TOP;
    m->x1 = target == MD_TARGET_BUDDY ? MD_BUDDY_X : md_item_x[target];
    m->y1 = target == MD_TARGET_BUDDY ? MD_BUDDY_Y : MD_ITEM_HIT_Y;
    m->fall_ms = w.fall_ms;
    m->born = t;
    g->spawned++;
    g->next_spawn = t + (uint32_t)rnd_range(g, w.gap_min_ms, w.gap_max_ms);
}

static void end_missile(md_missile_t *m, bool shot_down, int x, int y, uint32_t t)
{
    m->state = MD_MISSILE_FADING;
    m->shot_down = shot_down;
    m->ex = (int16_t)x;
    m->ey = (int16_t)y;
    m->end_at = t;
}

// A missile got through: its item is gone (or Buddy is dazed). One landing in the same step as the last item's only
// explodes.
static void impact(md_t *g, md_missile_t *m, uint32_t t)
{
    end_missile(m, false, m->x1, m->y1, t);
    add_blast(g, MD_BLAST_IMPACT, 0, m->x1, m->y1, t);
    if (g->phase == MD_OVER) {
        return;
    }
    if (m->target == MD_TARGET_BUDDY) {
        g->stunned = true;
        g->stun_at = t;
        say(g, MD_SAY_HIT, t, true);
        return;
    }
    const uint8_t bit = (uint8_t)(1u << m->target);
    if (!(g->items & bit)) {
        return; // already rubble
    }
    g->items &= (uint8_t)~bit;
    g->item_lost_at[m->target] = t;
    g->lost_item = m->target;
    g->lost_at = t;
    g->wave_lost++;
    if (!g->items) {
        g->phase = MD_OVER;
        g->phase_at = t;
        g->panic = false;
        say(g, MD_SAY_OVER, t, true);
    } else {
        say(g, (md_say_t)(MD_SAY_ITEM + m->target), t, true);
    }
}

// Caught in blast b at (x, y): it explodes too, and scores for the shot that started it.
static void shot_down(md_t *g, md_missile_t *m, const md_blast_t *b, int x, int y, uint32_t t)
{
    const uint8_t shot = b->shot;
    end_missile(m, true, x, y, t);
    add_blast(g, MD_BLAST_CHAIN, shot, x, y, t);
    uint8_t *kills = &g->shot_kills[shot % MD_SHOT_RING];
    *kills = (uint8_t)(*kills < 255 ? *kills + 1 : 255);
    const int mult = *kills > 3 ? 3 : *kills;
    add_score(g, (uint32_t)(MD_POINTS_MISSILE * mult));
    g->pop_points = (uint16_t)(MD_POINTS_MISSILE * mult);
    g->pop_mult = (uint8_t)mult;
    g->pop_x = (int16_t)x;
    g->pop_y = (int16_t)y;
    g->pop_at = t;
    g->streak = (uint8_t)(g->streak < 255 ? g->streak + 1 : 255);
    if (*kills == 2) {
        say(g, MD_SAY_DOUBLE, t, false);
    } else if (*kills == 3) {
        say(g, MD_SAY_TRIPLE, t, true);
    } else if (g->streak == MD_SMUG_STREAK) {
        g->smug_at = t;
        say(g, MD_SAY_SMUG, t, false);
    }
}

static void step(md_t *g, uint32_t t)
{
    if (g->phase == MD_OVER) {
        return; // the world stands still from the moment the game was lost
    }
    if (g->hot && t - g->hot_at >= MD_OVERHEAT_MS) {
        g->hot = false;
    }
    if (g->stunned && t - g->stun_at >= MD_STUN_MS) {
        g->stunned = false;
    }
    switch (g->phase) {
    case MD_READY:
        if (t - g->phase_at >= MD_READY_MS) {
            g->phase = MD_PLAYING;
            g->phase_at = t;
            g->next_spawn = t + 300;
        }
        break;
    case MD_CLEAR:
        if (t - g->phase_at >= MD_CLEAR_MS) {
            g->wave = (uint8_t)(g->wave < 255 ? g->wave + 1 : 255);
            g->phase = MD_READY;
            g->phase_at = t;
            g->spawned = 0;
            g->wave_lost = 0;
            say(g, MD_SAY_WAVE, t, true);
        }
        break;
    case MD_PLAYING:
        launch(g, t);
        break;
    default:
        break;
    }

    int close = 0;
    bool flying = false;
    for (int i = 0; i < MD_MAX_MISSILES; i++) {
        md_missile_t *m = &g->missile[i];
        if (m->state == MD_MISSILE_FADING) {
            if (t - m->end_at >= MD_TRAIL_FADE_MS) {
                m->state = MD_MISSILE_FREE;
            }
            continue;
        }
        if (m->state != MD_MISSILE_FLYING) {
            continue;
        }
        if (t - m->born >= m->fall_ms) {
            impact(g, m, t);
            continue;
        }
        int x, y;
        md_missile_head(m, t, &x, &y);
        bool caught = false;
        for (int b = 0; b < MD_MAX_BLASTS && !caught; b++) {
            const md_blast_t *bl = &g->blast[b];
            if (!bl->on || bl->kind == MD_BLAST_IMPACT) {
                continue;
            }
            const int r = md_blast_radius(bl, t), dx = x - bl->x, dy = y - bl->y;
            if (r > 0 && dx * dx + dy * dy <= r * r) {
                shot_down(g, m, bl, x, y, t);
                caught = true;
            }
        }
        if (!caught) {
            flying = true;
            close += (t - m->born) * 100u >= (uint32_t)m->fall_ms * MD_PANIC_PROGRESS;
        }
    }

    bool blasting = false;
    for (int b = 0; b < MD_MAX_BLASTS; b++) {
        md_blast_t *bl = &g->blast[b];
        if (!bl->on) {
            continue;
        }
        if (t - bl->born >= BLAST_MS) {
            bl->on = false;
            if (bl->kind == MD_BLAST_SHOT && g->shot_kills[bl->shot % MD_SHOT_RING] == 0 && g->phase != MD_OVER) {
                g->streak = 0; // a miss
                g->smug_at = 0;
                say(g, MD_SAY_MISS, t, false);
            }
            continue;
        }
        blasting = blasting || bl->kind != MD_BLAST_IMPACT;
    }

    if (g->phase != MD_OVER) {
        if (!g->panic && close >= MD_PANIC_ON) {
            g->panic = true;
            g->panic_at = t;
            say(g, MD_SAY_PANIC, t, false);
        } else if (g->panic && close <= MD_PANIC_OFF) {
            g->panic = false;
        }
    }

    // The wave is cleared once its last missile is gone and the last blast that could still score has died down.
    if (g->phase == MD_PLAYING && g->spawned >= md_wave(g->wave).missiles && !flying && !blasting) {
        g->phase = MD_CLEAR;
        g->phase_at = t;
        g->panic = false;
        g->saved = (uint8_t)md_items_left(g);
        add_score(g, (uint32_t)g->saved * MD_POINTS_ITEM);
        say(g, g->wave_lost ? MD_SAY_CLEAR : MD_SAY_PERFECT, t, true);
    }
}

static void advance(md_t *g, uint32_t now)
{
    if ((int32_t)(now - g->stepped) > CATCH_UP_MS) {
        g->stepped = now - CATCH_UP_MS;
    }
    while ((int32_t)(now - g->stepped) >= MD_STEP_MS) {
        g->stepped += MD_STEP_MS;
        step(g, g->stepped);
    }
}

static void until(uint32_t *next, uint32_t now, uint32_t at)
{
    const int32_t d = (int32_t)(at - now);
    const uint32_t ms = d <= 0 ? 1u : (uint32_t)d;
    *next = ms < *next ? ms : *next;
}

uint32_t md_tick(md_t *g, uint32_t now)
{
    if (g->phase == MD_OFF) {
        return UINT32_MAX;
    }
    advance(g, now);
    for (int i = 0; i < MD_MAX_MISSILES && g->phase != MD_OVER; i++) {
        if (g->missile[i].state != MD_MISSILE_FREE) {
            return MD_FRAME_MS;
        }
    }
    for (int b = 0; b < MD_MAX_BLASTS && g->phase != MD_OVER; b++) {
        if (g->blast[b].on) {
            return MD_FRAME_MS;
        }
    }
    uint32_t next = UINT32_MAX;
    switch (g->phase) {
    case MD_READY:
        until(&next, now, g->phase_at + MD_READY_MS);
        break;
    case MD_PLAYING:
        until(&next, now, g->next_spawn);
        break;
    case MD_CLEAR:
        until(&next, now, g->phase_at + MD_CLEAR_MS);
        break;
    case MD_OVER:
        until(&next, now, md_buttons(g, now) ? idle_since(g) + MD_OVER_IDLE_MS : g->phase_at + MD_OVER_BUTTONS_MS);
        break;
    default:
        break;
    }
    if (g->say != MD_SAY_NONE && now - g->say_at < MD_SAY_MS) {
        until(&next, now, g->say_at + MD_SAY_MS);
    }
    if (g->hot) {
        until(&next, now, g->hot_at + MD_OVERHEAT_MS);
    }
    if (g->stunned) {
        until(&next, now, g->stun_at + MD_STUN_MS);
    }
    return next;
}

md_fire_t md_fire(md_t *g, int x, int y, uint32_t now)
{
    if (g->phase == MD_OFF) {
        return MD_IGNORED;
    }
    advance(g, now);
    const uint32_t t = g->stepped; // the world's time: a shot is never stamped ahead of it
    if (g->phase != MD_READY && g->phase != MD_PLAYING) {
        return MD_IGNORED;
    }
    if (g->stunned) {
        say(g, MD_SAY_DAZED, t, false);
        return MD_REFUSED_STUNNED;
    }
    if (g->hot) {
        say(g, MD_SAY_COOLING, t, false);
        return MD_REFUSED_HOT;
    }
    x = x < 0 ? 0 : x >= MD_W ? MD_W - 1 : x;
    y = y < MD_FIRE_MIN_Y ? MD_FIRE_MIN_Y : y > MD_FIRE_MAX_Y ? MD_FIRE_MAX_Y : y;
    int heat = md_heat(g, t) + MD_HEAT_PER_SHOT;
    g->heat_at = t;
    if (heat >= 100) {
        heat = 100;
        g->hot = true;
        g->hot_at = t;
        say(g, MD_SAY_HOT, t, true);
    }
    g->heat = (uint8_t)heat;
    const uint8_t id = g->shots++;
    g->shot_kills[id % MD_SHOT_RING] = 0;
    add_blast(g, MD_BLAST_SHOT, id, x, y, t);
    g->shot_at = t;
    g->shot_x = (int16_t)x;
    g->shot_y = (int16_t)y;
    return MD_FIRED;
}
