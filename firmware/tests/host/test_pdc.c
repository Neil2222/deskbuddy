// The PDC Manager integration ("Ze Doen Maar Wat") on the device: the protocol lines, the availability lease, one
// request per confirmed tap, the cooldown, the menu, the confirmation and Buddy's reaction. No network anywhere: the
// companion's side is simulated by calling what its lines would do.
#include <stdio.h>
#include <string.h>

#include "buddy_activity.h"
#include "buddy_brain.h"
#include "link.h"
#include "menu.h"
#include "pdc.h"
#include "test.h"

static const char *frame(const char *content)
{
    static char buf[LINK_LINE_MAX];
    snprintf(buf, sizeof(buf), "%s #%04X", content, link_crc16(content, strlen(content)));
    return buf;
}

static link_result_t parse(const char *content, link_command_t *c)
{
    const char *line = frame(content);
    return link_parse_command(line, strlen(line), c);
}

void test_pdc_protocol_lines(void)
{
    link_command_t c;
    CHECK_EQ(parse(">DB1 3 pdc.set state=on count=123", &c), LINK_OK);
    CHECK(c.cmd == LINK_CMD_PDC_SET && c.u.pdc_set.on && c.u.pdc_set.count == 123);
    CHECK_EQ(parse(">DB1 3 pdc.set state=off", &c), LINK_OK);
    CHECK(!c.u.pdc_set.on && c.u.pdc_set.count == -1);
    CHECK_EQ(parse(">DB1 3 pdc.set state=on count=0 later=1", &c), LINK_OK); // unknown keys ignored
    CHECK_EQ(c.u.pdc_set.count, 0);
    CHECK_EQ(parse(">DB1 3 pdc.set", &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(">DB1 3 pdc.set state=maybe", &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(">DB1 3 pdc.set state=on count=-1", &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(">DB1 3 pdc.set state=on count=2147483648", &c), LINK_ERR_ARGS);

    CHECK_EQ(parse(">DB1 4 pdc.result id=7 result=ok count=124 teams=1", &c), LINK_OK);
    CHECK(c.cmd == LINK_CMD_PDC_RESULT && c.u.pdc_result.id == 7 && c.u.pdc_result.result == LINK_PDC_OK);
    CHECK(c.u.pdc_result.count == 124 && c.u.pdc_result.teams == 1);
    CHECK_EQ(parse(">DB1 4 pdc.result id=65535 result=timeout", &c), LINK_OK);
    CHECK(c.u.pdc_result.result == LINK_PDC_TIMEOUT && c.u.pdc_result.count == -1 && c.u.pdc_result.teams == -1);
    static const struct {
        const char *word;
        link_pdc_result_t result;
    } k_words[] = {{"auth", LINK_PDC_AUTH},         {"denied", LINK_PDC_DENIED},   {"offline", LINK_PDC_OFFLINE},
                   {"tls", LINK_PDC_TLS},           {"unknown", LINK_PDC_UNKNOWN}, {"cooldown", LINK_PDC_COOLDOWN},
                   {"busy", LINK_PDC_BUSY},         {"unavailable", LINK_PDC_UNAVAILABLE},
                   {"sharks", LINK_PDC_UNKNOWN}}; // a newer companion's word: maybe sent
    for (unsigned i = 0; i < sizeof(k_words) / sizeof(k_words[0]); i++) {
        char line[64];
        snprintf(line, sizeof(line), ">DB1 5 pdc.result id=1 result=%s", k_words[i].word);
        CHECK_EQ(parse(line, &c), LINK_OK);
        CHECK_EQ(c.u.pdc_result.result, k_words[i].result);
    }
    CHECK_EQ(parse(">DB1 6 pdc.result id=0 result=ok", &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(">DB1 6 pdc.result id=65536 result=ok", &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(">DB1 6 pdc.result result=ok", &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(">DB1 6 pdc.result id=1", &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(">DB1 6 pdc.result id=1 result=OK", &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(">DB1 6 pdc.result id=1 result=ok teams=2", &c), LINK_ERR_ARGS);
}

void test_pdc_lease_and_one_request_per_tap(void)
{
    pdc_t p;
    pdc_init(&p, 65534);
    CHECK(!pdc_available(&p, 0) && !pdc_ready(&p, 0) && p.count == -1);
    CHECK_EQ(pdc_send(&p, 0), 0); // not offered: nothing to send
    CHECK_EQ(pdc_next(&p, 0), UINT32_MAX);

    // Offered; the lease runs out without a refresh, and a hello forgets it at once.
    pdc_set(&p, true, 41, 1000);
    CHECK(pdc_available(&p, 1000) && pdc_ready(&p, 1000) && p.count == 41);
    CHECK_EQ(pdc_next(&p, 1000), PDC_LEASE_MS);
    CHECK(pdc_available(&p, 1000 + PDC_LEASE_MS - 1) && !pdc_available(&p, 1000 + PDC_LEASE_MS));
    pdc_set(&p, true, -1, 20000); // a refresh without a count keeps the one we know
    CHECK(pdc_available(&p, 1000 + PDC_LEASE_MS) && p.count == 41);
    pdc_reset(&p);
    CHECK(!pdc_available(&p, 20001));
    pdc_set(&p, true, 41, 30000);

    // JA: one id; until its result nothing else goes out, and a result for another id changes nothing.
    const uint16_t id = pdc_send(&p, 30000);
    CHECK_EQ(id, 65534);
    CHECK(!pdc_ready(&p, 30001) && pdc_available(&p, 30001));
    CHECK_EQ(pdc_send(&p, 30001), 0);
    CHECK(!pdc_result(&p, 65533, LINK_PDC_OK, 99, 30100));
    CHECK(!pdc_result(&p, 0, LINK_PDC_OK, 99, 30100));
    CHECK_EQ(p.count, 41);
    CHECK(pdc_result(&p, id, LINK_PDC_OK, 42, 30500));
    CHECK_EQ(p.count, 42);
    CHECK(!pdc_result(&p, id, LINK_PDC_OK, 43, 30600)); // the same result again (repeated line): ignored
    CHECK_EQ(p.count, 42);

    // Five seconds from the confirmed click before the next one.
    CHECK(!pdc_ready(&p, 30000 + PDC_COOLDOWN_MS - 1));
    CHECK_EQ(pdc_next(&p, 30500), PDC_COOLDOWN_MS - 500);
    pdc_set(&p, true, -1, 34000);
    CHECK(pdc_ready(&p, 30000 + PDC_COOLDOWN_MS));
    const uint16_t id2 = pdc_send(&p, 36000);
    CHECK_EQ(id2, 65535);

    // No result at all: given up once after PDC_REPLY_MS (the caller says "maybe sent"); a late result is ignored.
    CHECK(!pdc_expired(&p, 36000 + PDC_REPLY_MS - 1));
    CHECK(pdc_expired(&p, 36000 + PDC_REPLY_MS));
    CHECK(!pdc_expired(&p, 36000 + PDC_REPLY_MS + 1));
    CHECK(!pdc_result(&p, id2, LINK_PDC_OK, 50, 36000 + PDC_REPLY_MS + 5));
    pdc_set(&p, true, -1, 60000);
    CHECK_EQ(pdc_send(&p, 60000), 1); // ids wrap past 65535, skipping 0
}

void test_pdc_sign_lines(void)
{
    char s[PDC_TEXT_MAX];
    pdc_text(s, sizeof(s), LINK_PDC_OK, 123, 1);
    CHECK(strcmp(s, "ZE DOEN MAAR WAT!!! #123") == 0);
    pdc_text(s, sizeof(s), LINK_PDC_OK, 2147483647, 0);
    CHECK(strstr(s, "#2147483647") && strstr(s, "TEAMS"));
    pdc_text(s, sizeof(s), LINK_PDC_OK, -1, -1);
    CHECK(strcmp(s, "ZE DOEN MAAR WAT!!!") == 0);
    for (int r = 0; r < LINK_PDC_RESULT_COUNT; r++) {
        pdc_text(s, sizeof(s), (link_pdc_result_t)r, 5, 1);
        CHECK(strlen(s) > 0 && strlen(s) < PDC_TEXT_MAX - 1);
        // A request that may have reached the server is never reported as not sent.
        const bool maybe = r == LINK_PDC_TIMEOUT || r == LINK_PDC_UNKNOWN;
        CHECK(maybe == (strstr(s, "MISSCHIEN") != NULL));
        if (maybe) {
            CHECK(strstr(s, "NIET VERSTUURD") == NULL);
        }
    }
}

static const menu_item_t *find(const char *id)
{
    for (int i = 0; i < menu_default.count; i++) {
        if (strcmp(menu_default.items[i].id, id) == 0) {
            return &menu_default.items[i];
        }
    }
    return NULL;
}

void test_pdc_menu_only_while_offered(void)
{
    const menu_item_t *row = find("pdc"), *zdmw = find("pdc.zdmw");
    CHECK(row && zdmw && row->category == MENU_HOME && zdmw->category == MENU_PDC);
    menu_status_t st = {.mood = BUDDY_MOOD_IDLE, .office = true, .pdc_count = -1};
    CHECK_EQ(menu_item_avail(row, &st), MENU_ITEM_HIDDEN);
    CHECK_EQ(menu_item_avail(zdmw, &st), MENU_ITEM_HIDDEN);

    menu_t m;
    menu_init(&m, &menu_default, (gfx_rect_t){672, 0, 352, 600}, 0);
    const menu_item_t *items[MENU_SLOTS];
    int pages;
    CHECK_EQ(menu_page_items(&m, &st, items, &pages), 5); // Home as it always was
    CHECK_EQ(pages, 1);

    st.pdc = st.pdc_ready = true;
    st.pdc_count = 7;
    CHECK_EQ(menu_item_avail(row, &st), MENU_ITEM_READY);
    CHECK_EQ(menu_item_avail(zdmw, &st), MENU_ITEM_READY);
    CHECK_EQ(menu_page_items(&m, &st, items, &pages), 5);
    CHECK_EQ(pages, 2);           // Schedule (still SOON) moves to page 2...
    CHECK(items[3] == row);       // ...PDC is on the first page, after Food & Drink
    st.pdc_ready = false;         // a click under way or cooling down
    CHECK_EQ(menu_item_avail(zdmw, &st), MENU_ITEM_BUSY);
    st.pdc_ready = true;
    st.busy = true;               // another activity: not now either
    CHECK_EQ(menu_item_avail(zdmw, &st), MENU_ITEM_BUSY);
    st.busy = false;

    // A tap on the tile asks for the confirmation (it sends nothing by itself).
    menu_open(&m, MENU_PDC, 1000);
    CHECK_EQ(menu_page_items(&m, &st, items, NULL), 1);
    const gfx_rect_t r = menu_slot_rect(MENU_LIST, 0);
    const int x = m.area.x + (r.x + r.w / 2) * MENU_SCALE, y = m.area.y + (r.y + r.h / 2) * MENU_SCALE;
    menu_touch(&m, MENU_TOUCH_DOWN, x, y, &st, 3000);
    const menu_result_t res = menu_touch(&m, MENU_TOUCH_UP, x, y, &st, 3040);
    CHECK_EQ(res.action, MENU_DO_PDC);
    st.pdc_ready = false;
    menu_touch(&m, MENU_TOUCH_DOWN, x, y, &st, 4000);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x, y, &st, 4040).action, MENU_DO_NOTHING); // dimmed: nothing

    // The companion goes away: the page has nothing left to press.
    st.pdc = false;
    CHECK_EQ(menu_page_items(&m, &st, items, NULL), 0);
}

void test_pdc_confirmation_and_reaction(void)
{
    buddy_brain_t b;
    buddy_pose_t pose;
    buddy_brain_init(&b, 0, 3);
    buddy_brain_set_mood(&b, BUDDY_MOOD_WORKING, 0, 0);

    // NEE: back to work, nothing changed.
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_ASK_ZDMW, 1000));
    CHECK(buddy_brain_busy(&b, 1500) && !buddy_brain_event(&b, BUDDY_EVENT_POKE, 1500));
    CHECK_EQ(buddy_brain_asking(&b, 1000), BUDDY_CONFIRM_MS);
    buddy_brain_tick(&b, 1500, &pose);
    CHECK(pose.state == BUDDY_STATE_CONFIRM && !pose.props.cig);
    CHECK(buddy_brain_answer(&b, false, 2000));
    buddy_brain_tick(&b, 2010, &pose);
    CHECK_EQ(pose.state, BUDDY_STATE_IDLE);
    CHECK_EQ(buddy_brain_mood(&b, 2010), BUDDY_MOOD_WORKING);
    CHECK(!buddy_brain_answer(&b, true, 2020)); // answered already

    // JA: also straight back (the click goes out through the companion); no answer: the same, after 15 s.
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_ASK_ZDMW, 3000));
    CHECK(buddy_brain_answer(&b, true, 3100));
    CHECK(!buddy_brain_busy(&b, 3100));
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_ASK_ZDMW, 4000));
    buddy_brain_tick(&b, 4000 + BUDDY_CONFIRM_MS, &pose);
    CHECK_EQ(pose.state, BUDDY_STATE_IDLE); // no sadness, nothing sent
    CHECK(!buddy_brain_answer(&b, true, 4000 + BUDDY_CONFIRM_MS));

    // The reaction: exasperated, briefly; nothing interrupts it; then his mood as it was.
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_ZDMW, 30000));
    bool angry = false, waving = false, marks = false;
    for (uint32_t t = 30000; t < 30000 + BUDDY_ZDMW_MS; t += 20) {
        buddy_brain_tick(&b, t, &pose);
        CHECK_EQ(pose.state, BUDDY_STATE_ZDMW);
        CHECK(!buddy_brain_event(&b, BUDDY_EVENT_ZDMW, t) && !buddy_brain_event(&b, BUDDY_EVENT_ASK_ZDMW, t));
        angry |= pose.eyes == BUDDY_EYES_ANGRY && pose.tint == BUDDY_TINT_FURY;
        waving |= pose.props.hand_to == BUDDY_HAND_REST && pose.props.hand_mix > 0;
        marks |= pose.props.particle[0].kind == BUDDY_PART_ANGER;
    }
    CHECK(angry && waving && marks);
    CHECK(BUDDY_ZDMW_MS <= 3000); // short enough not to annoy
    buddy_brain_tick(&b, 30000 + BUDDY_ZDMW_MS, &pose);
    CHECK_EQ(pose.state, BUDDY_STATE_IDLE);
    CHECK_EQ(buddy_brain_mood(&b, 30000 + BUDDY_ZDMW_MS), BUDDY_MOOD_WORKING);
    CHECK(strcmp(buddy_activity_doing(BUDDY_STATE_ZDMW), "fuming") == 0);

    // During another activity the reaction does not barge in (the sign line still tells the outcome).
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_SNEEZE, 40000));
    CHECK(!buddy_brain_event(&b, BUDDY_EVENT_ZDMW, 40100));
}
