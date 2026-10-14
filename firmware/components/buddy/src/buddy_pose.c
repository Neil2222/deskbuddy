#include "buddy_pose.h"

#include <string.h>

static const char *const k_states[] = {"idle", "surprised", "happy"};
static const char *const k_eyes[] = {"open", "half", "closed", "happy", "wide"};
static const char *const k_mouths[] = {"neutral", "smile", "open"};
static const char *const k_antennas[] = {"calm", "excited"};

#define NAME(table, i) ((unsigned)(i) < sizeof(table) / sizeof((table)[0]) ? (table)[i] : "?")

const char *buddy_state_name(buddy_state_t s)
{
    return NAME(k_states, s);
}

const char *buddy_eyes_name(buddy_eyes_t e)
{
    return NAME(k_eyes, e);
}

const char *buddy_mouth_name(buddy_mouth_t m)
{
    return NAME(k_mouths, m);
}

const char *buddy_antenna_name(buddy_antenna_t a)
{
    return NAME(k_antennas, a);
}

bool buddy_state_from_name(const char *name, buddy_state_t *out)
{
    for (unsigned i = 0; i < sizeof(k_states) / sizeof(k_states[0]); i++) {
        if (name && strcmp(name, k_states[i]) == 0) {
            *out = (buddy_state_t)i;
            return true;
        }
    }
    return false;
}
