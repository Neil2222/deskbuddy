// DeskBuddy's menu: every category and item, in display order. See menu.h for what goes where.
#include <string.h>

#include "buddy_brain.h"
#include "menu.h"

static const menu_category_def_t k_categories[MENU_CATEGORY_COUNT] = {
    [MENU_HOME] = {"DESKBUDDY", MENU_LIST, MENU_HOME},
    [MENU_MOODS] = {"MOODS", MENU_GRID, MENU_HOME},
    [MENU_ACTIVITIES] = {"ACTIVITIES", MENU_GRID, MENU_HOME},
    [MENU_FOOD] = {"FOOD & DRINK", MENU_GRID, MENU_HOME},
    [MENU_GAMES] = {"GAMES", MENU_GRID, MENU_HOME},
    [MENU_MYDAY] = {"MY DAY", MENU_GRID, MENU_HOME},
    [MENU_PDC] = {"PDC", MENU_LIST, MENU_HOME},
    [MENU_CLOCKFACES] = {"CLOCK FACES", MENU_GRID, MENU_ACTIVITIES},
};

#define MOOD(id, label, mood) {"mood." id, label, MENU_MOODS, MENU_ICON_MOOD, MENU_DO_MOOD, mood, false}

// MY DAY: the ids after "myday." are the companion's activity types (Schedule/ActivityTypes.cs); never renamed.
#define MYDAY(id, label, icon, arg) {"myday." id, label, MENU_MYDAY, icon, MENU_DO_LOG, arg, false}

static const menu_item_t k_items[] = {
    {"moods", "MOODS", MENU_HOME, MENU_ICON_MOODS, MENU_DO_OPEN, MENU_MOODS, false},
    {"activities", "ACTIVITIES", MENU_HOME, MENU_ICON_ACTIVITIES, MENU_DO_OPEN, MENU_ACTIVITIES, false},
    {"food", "FOOD & DRINK", MENU_HOME, MENU_ICON_FOOD, MENU_DO_OPEN, MENU_FOOD, false},
    {"myday", "MY DAY", MENU_HOME, MENU_ICON_SCHEDULE, MENU_DO_OPEN, MENU_MYDAY, false},
    {"pdc", "PDC", MENU_HOME, MENU_ICON_ZDMW, MENU_DO_OPEN, MENU_PDC, false}, // only while the companion offers it
    {"games", "GAMES", MENU_HOME, MENU_ICON_GAMES, MENU_DO_OPEN, MENU_GAMES, true},

    MOOD("idle", "IDLE", BUDDY_MOOD_IDLE),
    MOOD("relaxed", "RELAXED", BUDDY_MOOD_RELAXED),
    MOOD("peukie", "PEUKIE", BUDDY_MOOD_PEUKIE),
    MOOD("angry", "ANGRY", BUDDY_MOOD_ANGRY),
    MOOD("sleepy", "SLEEPY", BUDDY_MOOD_SLEEPY),
    MOOD("working", "WORKING", BUDDY_MOOD_WORKING),
    MOOD("happy", "HAPPY", BUDDY_MOOD_HAPPY),

    {"activity.surprise", "SURPRISE!", MENU_ACTIVITIES, MENU_ICON_SURPRISE, MENU_DO_EVENT, BUDDY_EVENT_POKE, false},
    {"activity.clock", "CLOCK", MENU_ACTIVITIES, MENU_ICON_OFFICE, MENU_DO_OFFICE, 0, false}, // the selected face
    {"activity.clockfaces", "CLOCK FACES", MENU_ACTIVITIES, MENU_ICON_CLOCKFACES, MENU_DO_OPEN, MENU_CLOCKFACES,
     false},
    {"activity.sneeze", "SNEEZE", MENU_ACTIVITIES, MENU_ICON_SNEEZE, MENU_DO_EVENT, BUDDY_EVENT_SNEEZE, false},
    {"activity.stare", "STARE", MENU_ACTIVITIES, MENU_ICON_STARE, MENU_DO_EVENT, BUDDY_EVENT_STARE, false},
    {"activity.meditate", "MEDITATE", MENU_ACTIVITIES, MENU_ICON_MEDITATE, MENU_DO_EVENT, BUDDY_EVENT_MEDITATE, false},
    {"activity.focus", "FOCUS", MENU_ACTIVITIES, MENU_ICON_FOCUS, MENU_DO_EVENT, BUDDY_EVENT_FOCUS, false},
    {"activity.peukie", "PEUKIE?", MENU_ACTIVITIES, MENU_ICON_PEUKIE, MENU_DO_EVENT, BUDDY_EVENT_ASK_PEUKIE, false},

    {"pdc.zdmw", "ZE DOEN MAAR WAT!", MENU_PDC, MENU_ICON_ZDMW, MENU_DO_PDC, 0, false},

    // Clock faces: arg is the office_face_t; the ids after "face." are its wire names (office_face_name).
    {"face.classic", "CLASSIC", MENU_CLOCKFACES, MENU_ICON_OFFICE, MENU_DO_FACE, 0, false},
    {"face.skeleton", "SKELETON", MENU_CLOCKFACES, MENU_ICON_GEAR, MENU_DO_FACE, 1, false},

    {"food.monster", "MONSTER", MENU_FOOD, MENU_ICON_MONSTER, MENU_DO_EVENT, BUDDY_EVENT_DRINK_MONSTER, false},
    {"food.water", "WATER", MENU_FOOD, MENU_ICON_WATER, MENU_DO_EVENT, BUDDY_EVENT_DRINK_WATER, false},
    {"food.coffee", "COFFEE", MENU_FOOD, MENU_ICON_COFFEE, MENU_DO_NOTHING, 0, true},
    {"food.lunch", "LUNCH", MENU_FOOD, MENU_ICON_LUNCH, MENU_DO_NOTHING, 0, true},
    {"food.snacks", "SNACKS", MENU_FOOD, MENU_ICON_SNACKS, MENU_DO_NOTHING, 0, true},

    MYDAY("work", "WORK", MENU_ICON_MOOD, BUDDY_MOOD_WORKING),
    MYDAY("meeting", "MEETING", MENU_ICON_MEETING, 0),
    MYDAY("coffee", "COFFEE", MENU_ICON_COFFEE, 0),
    MYDAY("water", "WATER", MENU_ICON_WATER, 0),
    MYDAY("smoke", "SMOKE", MENU_ICON_PEUKIE, 0),
    MYDAY("stretch", "STRETCH", MENU_ICON_WALK, 0),
    MYDAY("lunch", "LUNCH", MENU_ICON_LUNCH, 0),
};

const menu_def_t menu_default = {k_categories, k_items, (int)(sizeof(k_items) / sizeof(k_items[0]))};

static const struct {
    const char *id;
    const char *note;
} k_notes[] = {
    {"myday.work", "BACK TO WORK!"},          {"myday.meeting", "GOOD LUCK IN THE MEETING!"},
    {"myday.coffee", "ENJOY YOUR COFFEE!"},   {"myday.water", "STAY HYDRATED!"},
    {"myday.smoke", "ENJOY YOUR BREAK!"},     {"myday.stretch", "GOOD IDEA: STRETCH YOUR LEGS!"},
    {"myday.lunch", "ENJOY YOUR LUNCH!"},
};

const char *menu_item_note(const menu_item_t *it)
{
    for (size_t i = 0; it && i < sizeof(k_notes) / sizeof(k_notes[0]); i++) {
        if (strcmp(k_notes[i].id, it->id) == 0) {
            return k_notes[i].note;
        }
    }
    return NULL;
}
