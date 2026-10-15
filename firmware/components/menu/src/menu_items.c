// DeskBuddy's menu: every category and item, in display order. See menu.h for what goes where.
#include "buddy_brain.h"
#include "menu.h"

static const menu_category_def_t k_categories[MENU_CATEGORY_COUNT] = {
    [MENU_HOME] = {"DESKBUDDY", MENU_LIST, MENU_HOME},
    [MENU_MOODS] = {"MOODS", MENU_GRID, MENU_HOME},
    [MENU_ACTIVITIES] = {"ACTIVITIES", MENU_GRID, MENU_HOME},
    [MENU_FOOD] = {"FOOD & DRINK", MENU_GRID, MENU_HOME},
    [MENU_GAMES] = {"GAMES", MENU_GRID, MENU_HOME},
    [MENU_SCHEDULE] = {"SCHEDULE", MENU_GRID, MENU_HOME},
};

#define MOOD(id, label, mood) {"mood." id, label, MENU_MOODS, MENU_ICON_MOOD, MENU_DO_MOOD, mood, false}

static const menu_item_t k_items[] = {
    {"moods", "MOODS", MENU_HOME, MENU_ICON_MOODS, MENU_DO_OPEN, MENU_MOODS, false},
    {"activities", "ACTIVITIES", MENU_HOME, MENU_ICON_ACTIVITIES, MENU_DO_OPEN, MENU_ACTIVITIES, false},
    {"food", "FOOD & DRINK", MENU_HOME, MENU_ICON_FOOD, MENU_DO_OPEN, MENU_FOOD, false},
    {"games", "GAMES", MENU_HOME, MENU_ICON_GAMES, MENU_DO_OPEN, MENU_GAMES, true},
    {"schedule", "SCHEDULE", MENU_HOME, MENU_ICON_SCHEDULE, MENU_DO_OPEN, MENU_SCHEDULE, true},

    MOOD("idle", "IDLE", BUDDY_MOOD_IDLE),
    MOOD("relaxed", "RELAXED", BUDDY_MOOD_RELAXED),
    MOOD("peukie", "PEUKIE", BUDDY_MOOD_PEUKIE),
    MOOD("angry", "ANGRY", BUDDY_MOOD_ANGRY),
    MOOD("sleepy", "SLEEPY", BUDDY_MOOD_SLEEPY),
    MOOD("working", "WORKING", BUDDY_MOOD_WORKING),
    MOOD("happy", "HAPPY", BUDDY_MOOD_HAPPY),

    {"activity.surprise", "SURPRISE!", MENU_ACTIVITIES, MENU_ICON_SURPRISE, MENU_DO_EVENT, BUDDY_EVENT_POKE, false},
    {"activity.clock", "CLOCK", MENU_ACTIVITIES, MENU_ICON_OFFICE, MENU_DO_OFFICE, 0, false},
    {"activity.sneeze", "SNEEZE", MENU_ACTIVITIES, MENU_ICON_SNEEZE, MENU_DO_EVENT, BUDDY_EVENT_SNEEZE, false},
    {"activity.stare", "STARE", MENU_ACTIVITIES, MENU_ICON_STARE, MENU_DO_EVENT, BUDDY_EVENT_STARE, false},
    {"activity.meditate", "MEDITATE", MENU_ACTIVITIES, MENU_ICON_MEDITATE, MENU_DO_EVENT, BUDDY_EVENT_MEDITATE, false},

    {"food.monster", "MONSTER", MENU_FOOD, MENU_ICON_MONSTER, MENU_DO_EVENT, BUDDY_EVENT_DRINK_MONSTER, false},
    {"food.water", "WATER", MENU_FOOD, MENU_ICON_WATER, MENU_DO_EVENT, BUDDY_EVENT_DRINK_WATER, false},
    {"food.coffee", "COFFEE", MENU_FOOD, MENU_ICON_COFFEE, MENU_DO_NOTHING, 0, true},
    {"food.lunch", "LUNCH", MENU_FOOD, MENU_ICON_LUNCH, MENU_DO_NOTHING, 0, true},
    {"food.snacks", "SNACKS", MENU_FOOD, MENU_ICON_SNACKS, MENU_DO_NOTHING, 0, true},
};

const menu_def_t menu_default = {k_categories, k_items, (int)(sizeof(k_items) / sizeof(k_items[0]))};
