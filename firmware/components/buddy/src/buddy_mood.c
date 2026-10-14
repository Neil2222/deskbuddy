#include "buddy_mood.h"

#include <string.h>

#include "buddy_brain.h" // BUDDY_MOTION_TICK_MS, BUDDY_MAX_LIFT
#include "buddy_motion.h"

static const char *const k_names[BUDDY_MOOD_COUNT] = {
    [BUDDY_MOOD_IDLE] = "idle",       [BUDDY_MOOD_RELAXED] = "relaxed", [BUDDY_MOOD_PEUKIE] = "peukie",
    [BUDDY_MOOD_ANGRY] = "angry",     [BUDDY_MOOD_SLEEPY] = "sleepy",   [BUDDY_MOOD_WORKING] = "working",
    [BUDDY_MOOD_HAPPY] = "happy",
};

const char *buddy_mood_name(buddy_mood_t mood)
{
    return mood < BUDDY_MOOD_COUNT ? k_names[mood] : k_names[BUDDY_MOOD_IDLE];
}

bool buddy_mood_parse(const char *name, buddy_mood_t *out)
{
    for (int i = 0; name && i < BUDDY_MOOD_COUNT; i++) {
        if (strcmp(name, k_names[i]) == 0) {
            *out = (buddy_mood_t)i;
            return true;
        }
    }
    return false;
}

static uint32_t mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

// A value in lo..hi chosen by the seed; `salt` gives independent choices from one seed.
static uint32_t pick(uint32_t seed, uint32_t salt, uint32_t lo, uint32_t hi)
{
    return lo + mix(seed ^ (salt * 0x9E3779B9u)) % (hi - lo + 1);
}

uint32_t buddy_mood_cycle_ms(buddy_mood_t mood, uint32_t seed)
{
    switch (mood) {
    case BUDDY_MOOD_RELAXED:
        return pick(seed, 1, 6000, 9000);
    case BUDDY_MOOD_PEUKIE:
        return pick(seed, 1, 7000, 10000);
    case BUDDY_MOOD_ANGRY:
        return pick(seed, 1, 3200, 4800);
    case BUDDY_MOOD_SLEEPY:
        return pick(seed, 1, 7600, 10600);
    case BUDDY_MOOD_WORKING:
        return pick(seed, 1, 5200, 7600);
    case BUDDY_MOOD_HAPPY:
        return pick(seed, 1, 4200, 6200);
    default:
        return 5000;
    }
}

static void emit(buddy_pose_t *p, int slot, buddy_part_kind_t kind, buddy_from_t from, int dx, int dy, int frame)
{
    p->props.particle[slot] = (buddy_particle_t){(uint8_t)kind, (uint8_t)from, (uint8_t)frame, (int8_t)dx, (int8_t)dy};
}

// Shortens *next so the pose is recomputed when `t` reaches `at`.
static void edge(uint32_t *next, uint32_t t, uint32_t at)
{
    if (t < at && at - t < *next) {
        *next = at - t;
    }
}

// Shortens *next to the next multiple of `period`.
static void beat(uint32_t *next, uint32_t t, uint32_t period)
{
    *next = buddy_min_u32(*next, period - t % period);
}

// Gentle sideways drift for rising smoke and Zs, in art pixels.
static const int8_t k_sway[8] = {0, 1, 1, 0, 0, -1, -1, 0};

// --- Relaxed: calm and content -------------------------------------------------------------------------------------

#define RELAX_BREATH_MS 1400 // breathing at half idle's tempo
#define RELAX_BLINK_MS 420   // a long, contented blink
#define RELAX_LOOK_MS 1600

static uint32_t relaxed(const buddy_mood_clock_t *c, buddy_pose_t *p)
{
    const uint32_t t = c->t;
    uint32_t next = 1000;
    p->eyes = BUDDY_EYES_LIDDED;
    p->mouth = BUDDY_MOUTH_SMILE;
    p->antenna = BUDDY_ANTENNA_CALM;
    p->lift = (uint8_t)((c->total / RELAX_BREATH_MS) & 1u);
    beat(&next, c->total, RELAX_BREATH_MS);

    const uint32_t b0 = pick(c->seed, 2, 1500, c->len - 1500);
    if (t >= b0 && t < b0 + RELAX_BLINK_MS) {
        p->eyes = BUDDY_EYES_CLOSED;
    }
    edge(&next, t, b0);
    edge(&next, t, b0 + RELAX_BLINK_MS);

    // In some cycles an unhurried look aside.
    const uint32_t l0 = pick(c->seed, 3, 500, c->len - RELAX_LOOK_MS - 200);
    if (pick(c->seed, 4, 0, 2) == 0 && t >= l0 && t < l0 + RELAX_LOOK_MS) {
        p->look = (int8_t)(c->seed & 1u ? 1 : -1);
    }
    edge(&next, t, l0);
    edge(&next, t, l0 + RELAX_LOOK_MS);
    return next;
}

// --- Peukie: a smoke break -----------------------------------------------------------------------------------------

#define SMOKE_MOVE_MS 650    // bringing the cigarette to his mouth, or back
#define SMOKE_DRAG_MS 1300   // a long drag: the tip lights up, his eyes close
#define SMOKE_EXHALE_MS 1600 // smoke out
#define SMOKE_AWAY_MS (2 * SMOKE_MOVE_MS + SMOKE_DRAG_MS)
#define WISP_EVERY_MS 650    // a curl of smoke leaves the tip this often...
#define WISP_LIFE_MS 1950    // ...and lasts this long (three in the air)
#define WISP_SLOTS 3
#define PUFF_LIFE_MS 1500
#define PUFF_GAP_MS 380

static uint32_t peukie(const buddy_mood_clock_t *c, buddy_pose_t *p)
{
    buddy_props_t *pr = &p->props;
    const uint32_t t = c->t;
    const uint32_t s = c->len - (SMOKE_AWAY_MS + SMOKE_EXHALE_MS) - 500; // the drag starts here
    uint32_t next = 120;

    p->eyes = BUDDY_EYES_LIDDED;
    p->mouth = BUDDY_MOUTH_SMIRK;
    p->antenna = BUDDY_ANTENNA_CALM;
    p->lift = (uint8_t)((c->total / 1300) & 1u); // slow, easy breathing
    beat(&next, c->total, 1300);

    pr->cig = true;
    pr->cig_left = (uint8_t)(BUDDY_CIG_LENGTHS - 1 - c->n % BUDDY_CIG_LENGTHS); // a fresh one every few drags
    pr->hand_from = pr->hand_to = BUDDY_HAND_REST;
    pr->ember = (mix(c->total / 700 + c->seed) & 3u) == 0 ? 1 : 0; // the tip glows up now and then
    beat(&next, c->total, 700);

    if (t < s) {
        // Just holding it: a slow, content blink, and in some cycles a look out over the desk.
        if (t >= 900 && t < 1080) {
            p->eyes = BUDDY_EYES_CLOSED;
        }
        if ((c->seed & 1u) && t >= 1800 && t < 3000) {
            p->look = 1;
        }
        edge(&next, t, 900);
        edge(&next, t, 1080);
        edge(&next, t, 1800);
        edge(&next, t, 3000);
        edge(&next, t, s);
    } else if (t < s + SMOKE_MOVE_MS) { // up to the mouth
        pr->hand_to = BUDDY_HAND_MOUTH;
        pr->hand_mix = buddy_ease(t - s, SMOKE_MOVE_MS);
        p->mouth = BUDDY_MOUTH_NEUTRAL;
        next = buddy_min_u32(BUDDY_MOTION_TICK_MS * 2, s + SMOKE_MOVE_MS - t);
    } else if (t < s + SMOKE_MOVE_MS + SMOKE_DRAG_MS) { // the drag
        pr->hand_from = pr->hand_to = BUDDY_HAND_MOUTH;
        pr->ember = 2;
        p->eyes = BUDDY_EYES_HAPPY; // blissful
        p->mouth = BUDDY_MOUTH_NEUTRAL;
        p->lift = 1; // breathing in
        edge(&next, t, s + SMOKE_MOVE_MS + SMOKE_DRAG_MS);
    } else if (t < s + SMOKE_AWAY_MS) { // down again, holding the smoke in
        pr->hand_from = BUDDY_HAND_MOUTH;
        pr->hand_mix = buddy_ease(t - s - SMOKE_MOVE_MS - SMOKE_DRAG_MS, SMOKE_MOVE_MS);
        pr->ember = 1;
        p->eyes = BUDDY_EYES_HAPPY;
        p->mouth = BUDDY_MOUTH_NEUTRAL;
        p->lift = 1;
        next = buddy_min_u32(BUDDY_MOTION_TICK_MS * 2, s + SMOKE_AWAY_MS - t);
    } else if (t < s + SMOKE_AWAY_MS + SMOKE_EXHALE_MS) { // and out it goes
        p->mouth = BUDDY_MOUTH_PUFF;
        edge(&next, t, s + SMOKE_AWAY_MS + SMOKE_EXHALE_MS);
    }

    // Exhaled smoke: two puffs from the mouth, drifting away sideways at mouth height (clear of his eyes).
    for (uint32_t k = 0; k < 2; k++) {
        const uint32_t born = s + SMOKE_AWAY_MS + k * PUFF_GAP_MS;
        if (t >= born && t - born < PUFF_LIFE_MS) {
            const uint32_t a = t - born;
            emit(p, WISP_SLOTS + (int)k, BUDDY_PART_SMOKE, BUDDY_FROM_MOUTH, -1 - (int)(a * 8 / PUFF_LIFE_MS),
                 -(int)(a * 2 / PUFF_LIFE_MS), a < 350 ? 0 : a < 1000 ? 1 : 2);
            next = buddy_min_u32(next, 100);
        }
        edge(&next, t, born);
    }

    // Curls rising from the tip while the cigarette rests in his hand (none are lit while it is at his mouth).
    const uint32_t latest = c->total / WISP_EVERY_MS;
    for (uint32_t k = 0; k < WISP_SLOTS; k++) {
        if (latest < k) {
            continue;
        }
        const uint32_t i = latest - (latest - k) % WISP_SLOTS; // the newest wisp of this slot
        const uint32_t age = c->total - i * WISP_EVERY_MS;
        if (age >= WISP_LIFE_MS || (age <= t && t - age >= s && t - age < s + SMOKE_AWAY_MS)) {
            continue;
        }
        emit(p, (int)k, BUDDY_PART_SMOKE, BUDDY_FROM_EMBER, k_sway[(age / 240 + i) % 8] + (int)(age * 2 / WISP_LIFE_MS),
             -1 - (int)(age * 12 / WISP_LIFE_MS), age < 450 ? 0 : age < 1250 ? 1 : 2);
    }
    return buddy_min_u32(next, 100);
}

// --- Angry: glaring and fuming ------------------------------------------------------------------------------------

#define STEAM_MS 900
#define FIT_MS 950 // a fit of fury: red face, clenched teeth, shaking

static uint32_t angry(const buddy_mood_clock_t *c, buddy_pose_t *p)
{
    const uint32_t t = c->t;
    uint32_t next = 120;
    p->eyes = BUDDY_EYES_ANGRY;
    p->mouth = BUDDY_MOUTH_FROWN;
    p->antenna = BUDDY_ANTENNA_EXCITED;
    p->lift = (uint8_t)((c->total / 450) & 1u); // huffing
    beat(&next, c->total, 450);

    // Steam from both sides of his head at the start of every cycle.
    if (t < STEAM_MS) {
        const int f = t < STEAM_MS / 2 ? 0 : 1, out = (int)(t * 3 / STEAM_MS), up = 1 + (int)(t * 5 / STEAM_MS);
        emit(p, 0, BUDDY_PART_STEAM, BUDDY_FROM_HEAD_L, -out, -up, f);
        emit(p, 1, BUDDY_PART_STEAM, BUDDY_FROM_HEAD_R, out, -up, f);
        next = buddy_min_u32(next, 90);
    }
    edge(&next, t, STEAM_MS);

    const bool fit = c->seed % 3u != 0;
    const uint32_t f0 = c->len - FIT_MS - 250;
    if (fit && t >= f0 && t < f0 + FIT_MS) {
        const uint32_t a = t - f0;
        p->tint = BUDDY_TINT_FURY;
        p->eyes = BUDDY_EYES_SQUEEZE;
        p->mouth = BUDDY_MOUTH_GRIT;
        p->lift = 0;
        p->shake = (int8_t)((a / 50) & 1u ? 1 : -1);
        emit(p, 2, BUDDY_PART_ANGER, BUDDY_FROM_ABOVE, 0, 0, (int)((a / 160) & 1u));
        if (a < 700) {
            const int f = a < 350 ? 0 : 1, out = (int)(a * 4 / 700), up = 2 + (int)(a * 6 / 700);
            emit(p, 3, BUDDY_PART_STEAM, BUDDY_FROM_HEAD_L, -out, -up, f);
            emit(p, 4, BUDDY_PART_STEAM, BUDDY_FROM_HEAD_R, out, -up, f);
        }
        next = 50 - a % 50;
    } else if (!fit && t >= c->len / 2 && t < c->len / 2 + 900) {
        p->look = -1; // a sideways glare
        p->mouth = BUDDY_MOUTH_GRIT;
    }
    edge(&next, t, f0);
    edge(&next, t, f0 + FIT_MS);
    edge(&next, t, c->len / 2);
    edge(&next, t, c->len / 2 + 900);
    return next;
}

// --- Sleepy: struggling to stay awake ---------------------------------------------------------------------------

#define DOZE_AT_MS 2200
#define JOLT_MS 380
#define Z_LIFE_MS 2100
#define Z_GAP_MS 650
#define YAWN_MS 1600

static uint32_t sleepy(const buddy_mood_clock_t *c, buddy_pose_t *p)
{
    const uint32_t t = c->t;
    uint32_t next = 200;
    p->eyes = BUDDY_EYES_HALF;
    p->mouth = BUDDY_MOUTH_NEUTRAL;
    p->antenna = BUDDY_ANTENNA_DIM;
    p->tint = BUDDY_TINT_DROWSY;
    p->lift = (uint8_t)((c->total / 1700) & 1u); // slow, heavy breathing
    beat(&next, c->total, 1700);

    // A slow blink...
    if (t >= 1100 && t < 1650) {
        p->eyes = BUDDY_EYES_CLOSED;
    }
    edge(&next, t, 1100);
    edge(&next, t, 1650);

    // ...then he nods off: eyes shut, head sinks, a little snore...
    const uint32_t dur = pick(c->seed, 2, 1800, 2400), d1 = DOZE_AT_MS + dur;
    if (t >= DOZE_AT_MS && t < d1) {
        p->eyes = BUDDY_EYES_CLOSED;
        p->nod = 1;
        p->lift = 0;
        if (t - DOZE_AT_MS > dur / 2) {
            p->mouth = BUDDY_MOUTH_PUFF;
        }
        edge(&next, t, DOZE_AT_MS + dur / 2 + 1);
    } else if (t >= d1 && t < d1 + JOLT_MS) { // ...and jerks awake
        p->eyes = BUDDY_EYES_WIDE;
        p->lift = 1;
    }
    edge(&next, t, DOZE_AT_MS);
    edge(&next, t, d1);
    edge(&next, t, d1 + JOLT_MS);

    // Zs float up from the dozing head.
    for (uint32_t k = 0; k < 3; k++) {
        const uint32_t born = DOZE_AT_MS + 250 + k * Z_GAP_MS;
        if (t >= born && t - born < Z_LIFE_MS) {
            const uint32_t a = t - born;
            emit(p, (int)k, BUDDY_PART_Z, BUDDY_FROM_ABOVE, (int)(a * 5 / Z_LIFE_MS) + k_sway[(a / 300 + k) % 8],
                 -(int)(a * 10 / Z_LIFE_MS), a < 1000 ? 0 : 1);
            next = buddy_min_u32(next, 120);
        }
        edge(&next, t, born);
    }

    // In most cycles a big yawn near the end.
    const uint32_t y0 = c->len - YAWN_MS - 700;
    if (pick(c->seed, 3, 0, 2) != 0 && t >= y0 && t < y0 + YAWN_MS) {
        const uint32_t a = t - y0;
        p->eyes = BUDDY_EYES_CLOSED;
        p->mouth = a < 250 || a >= YAWN_MS - 250 ? BUDDY_MOUTH_PUFF : BUDDY_MOUTH_OPEN;
        p->lift = a >= 300 && a < YAWN_MS - 300 ? 1 : 0; // a stretch
        edge(&next, t, y0 + 250);
        edge(&next, t, y0 + 300);
        edge(&next, t, y0 + YAWN_MS - 300);
        edge(&next, t, y0 + YAWN_MS - 250);
    }
    edge(&next, t, y0);
    edge(&next, t, y0 + YAWN_MS);
    return next;
}

// --- Working: concentrating hard ---------------------------------------------------------------------------------

#define TYPE_MS 160  // the busy little bob
#define SCAN_MS 450  // eyes running along the lines
#define SWEAT_MS 1400
#define EFFORT_MS 650
#define PAUSE_MS 1300

static const int8_t k_scan[4] = {-1, 0, 1, 0};

static uint32_t working(const buddy_mood_clock_t *c, buddy_pose_t *p)
{
    const uint32_t t = c->t;
    uint32_t next = 200;
    p->eyes = BUDDY_EYES_FOCUS;
    p->mouth = c->n & 1u ? BUDDY_MOUTH_TONGUE : BUDDY_MOUTH_NEUTRAL;
    p->antenna = BUDDY_ANTENNA_EXCITED;
    p->lift = (uint8_t)((c->total / TYPE_MS) & 1u);
    p->look = k_scan[(c->total / SCAN_MS) % 4];
    beat(&next, c->total, TYPE_MS);
    beat(&next, c->total, SCAN_MS);

    // A drop of sweat runs down the side of his head.
    const uint32_t ts = pick(c->seed, 2, 500, 2100);
    if (t >= ts && t - ts < SWEAT_MS) {
        emit(p, 0, BUDDY_PART_SWEAT, BUDDY_FROM_BROW, 0, (int)((t - ts) * 7 / SWEAT_MS), 0);
    }
    edge(&next, t, ts);
    edge(&next, t, ts + SWEAT_MS);

    // Every other cycle a moment of real effort: squeezed eyes, trembling, sweat flying off.
    const uint32_t e0 = c->len / 2;
    if ((c->seed & 1u) == 0 && t >= e0 && t < e0 + EFFORT_MS) {
        const uint32_t a = t - e0;
        p->eyes = BUDDY_EYES_SQUEEZE;
        p->lift = 0;
        p->shake = (int8_t)((a / 60) & 1u ? 1 : -1);
        emit(p, 1, BUDDY_PART_SWEAT, BUDDY_FROM_HEAD_R, (int)(a * 4 / EFFORT_MS), (int)(a * 5 / EFFORT_MS), 0);
        next = 60 - a % 60;
    }
    edge(&next, t, e0);
    edge(&next, t, e0 + EFFORT_MS);

    // A short pause: he looks up with a "phew", blinks, and gets back to it.
    const uint32_t p0 = c->len - PAUSE_MS;
    if (t >= p0) {
        const uint32_t a = t - p0;
        p->lift = 0;
        p->look = 0;
        p->eyes = a >= 600 && a < 760 ? BUDDY_EYES_CLOSED : BUDDY_EYES_OPEN;
        p->mouth = a < 600 ? BUDDY_MOUTH_PUFF : BUDDY_MOUTH_NEUTRAL;
        p->antenna = BUDDY_ANTENNA_CALM;
        next = 200; // no typing bob during the pause
        edge(&next, t, p0 + 600);
        edge(&next, t, p0 + 760);
    }
    edge(&next, t, p0);
    return next;
}

// --- Happy: celebrating -------------------------------------------------------------------------------------------

#define HOP_MS 450
#define HOPS 3
#define SWAY_MS 380
#define SPARKLE_MS 1100

static const int8_t k_dance[4] = {0, 1, 0, -1};

static const struct {
    uint8_t from;
    int8_t dx, dy;
} k_sparkle_at[4] = {
    {BUDDY_FROM_HEAD_L, -2, 0}, {BUDDY_FROM_HEAD_R, 2, -2}, {BUDDY_FROM_ABOVE, -12, 1}, {BUDDY_FROM_ABOVE, 3, 4},
};

static uint32_t happy(const buddy_mood_clock_t *c, buddy_pose_t *p)
{
    const uint32_t t = c->t, cel = HOPS * HOP_MS;
    uint32_t next = 120;
    p->eyes = BUDDY_EYES_HAPPY;
    p->mouth = BUDDY_MOUTH_SMILE;
    p->antenna = BUDDY_ANTENNA_EXCITED;

    if (t < cel) { // a burst of joy: hops with a big grin
        p->lift = buddy_hop(t % HOP_MS, HOP_MS, BUDDY_MAX_LIFT);
        p->mouth = BUDDY_MOUTH_GRIN;
        next = buddy_min_u32(BUDDY_MOTION_TICK_MS, cel - t);
    } else { // then a happy little dance on the spot
        p->wobble = k_dance[(c->total / SWAY_MS) % 4];
        p->lift = (uint8_t)((c->total / SWAY_MS) & 1u);
        beat(&next, c->total, SWAY_MS);
        if ((c->seed & 1u) && t >= cel + 900 && t < cel + 1700) { // looks around, beaming
            p->eyes = BUDDY_EYES_OPEN;
            p->look = (int8_t)((t / 400) & 1u ? 1 : -1);
            beat(&next, t, 400);
        }
        edge(&next, t, cel + 900);
        edge(&next, t, cel + 1700);
    }

    // Sparkles pop up around him while he jumps.
    for (uint32_t i = 0; i < 4; i++) {
        const uint32_t born = i * 170;
        if (t >= born && t < born + SPARKLE_MS) {
            const uint32_t a = t - born;
            const int jitter = (int)((c->seed >> (2 * i)) & 3u) - 1;
            emit(p, (int)i, BUDDY_PART_SPARKLE, (buddy_from_t)k_sparkle_at[i].from, k_sparkle_at[i].dx + jitter,
                 k_sparkle_at[i].dy - (int)(a * 2 / SPARKLE_MS), (int)((a / 180) & 1u));
            next = buddy_min_u32(next, 90);
        }
        edge(&next, t, born + SPARKLE_MS);
    }
    // And one more twinkle between celebrations.
    const uint32_t m0 = c->len / 2 + 400;
    if (t >= m0 && t < m0 + 540) {
        emit(p, 4, BUDDY_PART_SPARKLE, BUDDY_FROM_ABOVE, (int)pick(c->seed, 4, 0, 6) - 3, 0, (int)(((t - m0) / 180) & 1u));
        next = buddy_min_u32(next, 90);
    }
    edge(&next, t, m0);
    edge(&next, t, m0 + 540);
    return next;
}

uint32_t buddy_mood_pose(buddy_mood_t mood, const buddy_mood_clock_t *clock, buddy_pose_t *pose)
{
    uint32_t next;
    switch (mood) {
    case BUDDY_MOOD_RELAXED:
        next = relaxed(clock, pose);
        break;
    case BUDDY_MOOD_PEUKIE:
        next = peukie(clock, pose);
        break;
    case BUDDY_MOOD_ANGRY:
        next = angry(clock, pose);
        break;
    case BUDDY_MOOD_SLEEPY:
        next = sleepy(clock, pose);
        break;
    case BUDDY_MOOD_WORKING:
        next = working(clock, pose);
        break;
    case BUDDY_MOOD_HAPPY:
        next = happy(clock, pose);
        break;
    default:
        pose->eyes = BUDDY_EYES_OPEN;
        pose->mouth = BUDDY_MOUTH_NEUTRAL;
        pose->antenna = BUDDY_ANTENNA_CALM;
        next = 1000;
        break;
    }
    return next ? next : 1;
}
