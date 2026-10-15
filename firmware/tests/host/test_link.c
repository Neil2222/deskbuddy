// Serial protocol to the Windows companion: what is accepted, what is rejected, and how.
#include <stdio.h>
#include <string.h>

#include "link.h"
#include "test.h"

// Appends " #XXXX" to a command line, like the companion does.
static const char *frame(const char *content)
{
    static char buf[LINK_LINE_MAX];
    snprintf(buf, sizeof(buf), "%s #%04X", content, link_crc16(content, strlen(content)));
    return buf;
}

static link_result_t parse(const char *line, link_command_t *c)
{
    return link_parse_command(line, strlen(line), c);
}

void test_link_crc_matches_ccitt_false(void)
{
    CHECK_EQ(link_crc16("123456789", 9), 0x29B1);
}

void test_link_parses_commands(void)
{
    link_command_t c;
    CHECK_EQ(parse(frame(">DB1 7 monster"), &c), LINK_OK);
    CHECK_EQ(c.cmd, LINK_CMD_MONSTER);
    CHECK_EQ(c.seq, 7);

    CHECK_EQ(parse(frame(">DB1 4294967295 mode.set mode=office"), &c), LINK_OK);
    CHECK(c.cmd == LINK_CMD_MODE_SET && c.u.office && c.seq == 4294967295u);
    CHECK_EQ(parse(frame(">DB1 1 mode.set mode=normal"), &c), LINK_OK);
    CHECK(!c.u.office);
    CHECK_EQ(parse(frame(">DB1 2 mode.set mode=clock"), &c), LINK_OK); // Clock Mode's new name, an alias of office
    CHECK(c.cmd == LINK_CMD_MODE_SET && c.u.office);

    // Arguments in any order; unknown keys are ignored (newer companions may send more).
    CHECK_EQ(parse(frame(">DB1 9 time.set off=120 utc=1792000000 ms=250 next=1792800000 next_off=60 x=1"), &c),
             LINK_OK);
    CHECK_EQ(c.cmd, LINK_CMD_TIME_SET);
    CHECK_EQ(c.u.time.utc, 1792000000LL);
    CHECK_EQ(c.u.time.ms, 250);
    CHECK_EQ(c.u.time.tz.offset_min, 120);
    CHECK_EQ(c.u.time.tz.next_utc, 1792800000LL);
    CHECK_EQ(c.u.time.tz.next_offset_min, 60);
    CHECK_EQ(parse(frame(">DB1 10 time.set utc=1792000000 off=-300"), &c), LINK_OK); // no DST, no ms
    CHECK(c.u.time.tz.next_utc == 0 && c.u.time.tz.offset_min == -300 && c.u.time.ms == 0);

    // A Windows line ending, and lower-case checksum digits.
    char line[LINK_LINE_MAX];
    snprintf(line, sizeof(line), "%s\r", frame(">DB1 3 poke"));
    CHECK_EQ(parse(line, &c), LINK_OK);
    CHECK_EQ(c.cmd, LINK_CMD_POKE);
    snprintf(line, sizeof(line), ">DB1 2 ping #%04x", link_crc16(">DB1 2 ping", 11));
    CHECK_EQ(parse(line, &c), LINK_OK);
}

// Nothing but a well-formed command with a matching checksum may do anything: log output, typing in a serial
// monitor and corrupted lines must not trigger actions.
void test_link_rejects_noise_and_corruption(void)
{
    link_command_t c;
    CHECK_EQ(parse("I (1234) deskbuddy: Office Mode on", &c), LINK_NOT_A_FRAME);
    CHECK_EQ(parse("monster", &c), LINK_NOT_A_FRAME);
    CHECK_EQ(parse("", &c), LINK_NOT_A_FRAME);
    CHECK_EQ(parse(frame("<DB1 7 monster"), &c), LINK_NOT_A_FRAME); // the device's own output echoed back

    char line[LINK_LINE_MAX];
    strcpy(line, frame(">DB1 7 monster"));
    line[8] = 'n'; // "monster" -> "nonster"
    CHECK_EQ(parse(line, &c), LINK_ERR_CRC);
    CHECK_EQ(c.seq, 0);
    strcpy(line, frame(">DB1 7 monster"));
    char *last = &line[strlen(line) - 1];
    *last = *last == '0' ? '1' : '0'; // a different, still valid, checksum digit
    CHECK_EQ(parse(line, &c), LINK_ERR_CRC);

    CHECK_EQ(parse(">DB1 7 monster", &c), LINK_ERR_SYNTAX);        // no checksum
    CHECK_EQ(parse(">DB1 7 monster #12G4", &c), LINK_ERR_SYNTAX);  // not hex
    CHECK_EQ(parse(frame(">DB1 7"), &c), LINK_ERR_SYNTAX);         // no command
    CHECK_EQ(parse(frame(">DB1  7 poke"), &c), LINK_ERR_SYNTAX);   // empty token
    CHECK_EQ(parse(frame(">DB1 x7 poke"), &c), LINK_ERR_SYNTAX);   // sequence not a number
    CHECK_EQ(parse(frame(">DB1 4294967296 poke"), &c), LINK_ERR_SYNTAX); // beyond 32 bits
    CHECK_EQ(parse(frame(">DB1 7 Poke"), &c), LINK_ERR_SYNTAX);    // names are lower case
    CHECK_EQ(parse(frame(">DB 7 poke"), &c), LINK_ERR_SYNTAX);     // no version
    CHECK_EQ(parse(frame(">DB2 7 poke"), &c), LINK_ERR_VERSION);
    CHECK_EQ(parse(">DB2 7 a different layout", &c), LINK_ERR_VERSION); // judged before the checksum
}

void test_link_rejects_unknown_commands_and_bad_arguments(void)
{
    link_command_t c;
    CHECK_EQ(parse(frame(">DB1 12 dance"), &c), LINK_ERR_UNKNOWN_CMD);
    CHECK_EQ(c.seq, 12); // the reply goes to this command
    CHECK_EQ(parse(frame(">DB1 13 mode.set"), &c), LINK_ERR_ARGS);
    CHECK_EQ(c.seq, 13);
    CHECK_EQ(parse(frame(">DB1 13 mode.set mode=sideways"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 13 mode.set mode"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 13 mode.set mode=office!"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 14 time.set utc=1792000000"), &c), LINK_ERR_ARGS);            // no offset
    CHECK_EQ(parse(frame(">DB1 14 time.set utc=1792000000 off=120 ms=1000"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 14 time.set utc=-5 off=120"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 14 time.set utc=1792000000 off=901"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 14 time.set utc=1792000000 off=120 next=1792800000"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 14 time.set utc=99999999999999999999 off=120"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 15 ping a=1 b=2 c=3 d=4 e=5 f=6 g=7 h=8 i=9"), &c), LINK_ERR_ARGS);
}

void test_link_formats_replies_the_companion_can_check(void)
{
    char buf[LINK_LINE_MAX];
    const size_t n = link_format(buf, sizeof(buf), 42, "ok mode=office");
    CHECK_EQ(n, strlen(buf));
    const char *expect = "<DB1 42 ok mode=office";
    CHECK(strncmp(buf, expect, strlen(expect)) == 0);
    CHECK(buf[n - 1] == '\n');
    unsigned crc = 0;
    CHECK(sscanf(buf + strlen(expect), " #%4X", &crc) == 1);
    CHECK_EQ(crc, link_crc16(buf, strlen(expect)));

    char small[24];
    CHECK_EQ(link_format(small, sizeof(small), 42, "ok mode=office"), 0); // never truncated
}

void test_link_time_zone_switches_at_the_announced_moment(void)
{
    const link_tz_t cest = {.offset_min = 120, .next_utc = 1792803600, .next_offset_min = 60};
    CHECK_EQ(link_tz_offset_min(&cest, 1792803599), 120);
    CHECK_EQ(link_tz_offset_min(&cest, 1792803600), 60);
    CHECK_EQ(link_tz_offset_min(&cest, 1800000000), 60);
    const link_tz_t fixed = {.offset_min = -300};
    CHECK_EQ(link_tz_offset_min(&fixed, 1800000000), -300);
}

// msg.show: percent-encoded UTF-8 text, an id against repeats, and a style for later presentations.
void test_link_parses_messages(void)
{
    link_command_t c;
    CHECK_EQ(parse(frame(">DB1 5 msg.show id=7 style=marquee text=Hello%2C%20world%21"), &c), LINK_OK);
    CHECK(c.cmd == LINK_CMD_MSG_SHOW && c.u.msg.id == 7 && c.u.msg.style == LINK_STYLE_MARQUEE);
    CHECK(strcmp(c.u.msg.text, "Hello, world!") == 0);
    CHECK_EQ(parse(frame(">DB1 6 msg.show text=caf%C3%a9%20%E2%99%A5 id=4294967295"), &c), LINK_OK); // default style
    CHECK(c.u.msg.style == LINK_STYLE_MARQUEE && c.u.msg.id == 4294967295u);
    CHECK(strcmp(c.u.msg.text, "caf\xC3\xA9 \xE2\x99\xA5") == 0);
    CHECK_EQ(parse(frame(">DB1 6 msg.show id=1 text=a.b_c:d+e-f"), &c), LINK_OK); // these need no encoding
    CHECK(strcmp(c.u.msg.text, "a.b_c:d+e-f") == 0);

    // The limits: 100 characters, and 200 bytes of UTF-8 (100 two-byte letters fit exactly).
    static char line[LINK_LINE_MAX];
    char text[3 * LINK_TEXT_MAX_BYTES + 1] = "";
    for (int i = 0; i < LINK_TEXT_MAX_CHARS; i++) {
        strcat(text, "%C3%A9");
    }
    snprintf(line, sizeof(line), ">DB1 8 msg.show id=2 text=%s", text);
    CHECK_EQ(parse(frame(line), &c), LINK_OK);
    CHECK_EQ(strlen(c.u.msg.text), LINK_TEXT_MAX_BYTES);
    snprintf(line, sizeof(line), ">DB1 8 msg.show id=2 text=%sx", text); // one character too many
    CHECK_EQ(parse(frame(line), &c), LINK_ERR_ARGS);
    CHECK_EQ(c.seq, 8);
    memset(text, 'x', LINK_TEXT_MAX_CHARS + 1);
    text[LINK_TEXT_MAX_CHARS + 1] = '\0';
    snprintf(line, sizeof(line), ">DB1 8 msg.show id=2 text=%s", text);
    CHECK_EQ(parse(frame(line), &c), LINK_ERR_ARGS);
    text[LINK_TEXT_MAX_CHARS] = '\0';
    snprintf(line, sizeof(line), ">DB1 8 msg.show id=2 text=%s", text);
    CHECK_EQ(parse(frame(line), &c), LINK_OK);
    text[0] = '\0';
    for (int i = 0; i < 49; i++) { // 49 four-byte emoji (196 bytes) and 4 letters: 53 characters, 200 bytes
        strcat(text, "%F0%9F%98%80");
    }
    snprintf(line, sizeof(line), ">DB1 8 msg.show id=2 text=%sxxxx", text);
    CHECK_EQ(parse(frame(line), &c), LINK_OK);
    snprintf(line, sizeof(line), ">DB1 8 msg.show id=2 text=%sxxxxx", text); // 201 bytes
    CHECK_EQ(parse(frame(line), &c), LINK_ERR_ARGS);
}

void test_link_rejects_bad_messages(void)
{
    link_command_t c;
    CHECK_EQ(parse(frame(">DB1 9 msg.show text=hi"), &c), LINK_ERR_ARGS); // no id
    CHECK_EQ(c.seq, 9);
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=0 text=hi"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=%31 text=hi"), &c), LINK_ERR_ARGS); // only the text is encoded
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=1"), &c), LINK_ERR_ARGS);         // no text
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=1 text="), &c), LINK_ERR_ARGS);   // empty text
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=1 text=hi,there"), &c), LINK_ERR_ARGS);  // ',' must be encoded
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=1 text=%G1"), &c), LINK_ERR_ARGS);       // not hex
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=1 text=ab%2"), &c), LINK_ERR_ARGS);      // cut short
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=1 text=a%00b"), &c), LINK_ERR_ARGS);     // NUL
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=1 text=%C3"), &c), LINK_ERR_ARGS);       // truncated UTF-8
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=1 text=%C0%AF"), &c), LINK_ERR_ARGS);    // overlong
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=1 text=%ED%A0%80"), &c), LINK_ERR_ARGS); // surrogate
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=1 text=%F4%90%80%80"), &c), LINK_ERR_ARGS); // above U+10FFFF
    CHECK_EQ(parse(frame(">DB1 9 msg.show id=1 style= text=hi"), &c), LINK_ERR_ARGS);
    // A style this firmware does not have yet: well-formed, so a clear "unsupported" for this command.
    CHECK_EQ(parse(frame(">DB1 10 msg.show id=1 style=speech text=hi"), &c), LINK_ERR_UNSUPPORTED);
    CHECK_EQ(c.seq, 10);
    CHECK(strcmp(link_error_code(LINK_ERR_UNSUPPORTED), "unsupported") == 0);
}

// mood.set: the parser checks the shape of the name and the duration; whether the mood exists is the device's
// answer (err unsupported), so newer companions can ask for moods an older firmware does not have.
void test_link_parses_moods(void)
{
    link_command_t c;
    CHECK_EQ(parse(frame(">DB1 5 mood.set name=working"), &c), LINK_OK);
    CHECK(c.cmd == LINK_CMD_MOOD_SET && c.seq == 5 && strcmp(c.u.mood.name, "working") == 0 && c.u.mood.secs == 0);
    CHECK_EQ(parse(frame(">DB1 6 mood.set secs=90 name=relaxed"), &c), LINK_OK);
    CHECK(strcmp(c.u.mood.name, "relaxed") == 0 && c.u.mood.secs == 90);
    CHECK_EQ(parse(frame(">DB1 7 mood.set name=build_failed secs=86400"), &c), LINK_OK); // shape only
    CHECK_EQ(c.u.mood.secs, LINK_MOOD_SECS_MAX);
    CHECK_EQ(parse(frame(">DB1 8 mood.set name=abcdefghijklmno"), &c), LINK_OK); // 15 letters: the longest

    CHECK_EQ(parse(frame(">DB1 9 mood.set"), &c), LINK_ERR_ARGS);
    CHECK_EQ(c.seq, 9); // still answerable
    CHECK_EQ(parse(frame(">DB1 9 mood.set name="), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 9 mood.set name=Working"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 9 mood.set name=work1ng"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 9 mood.set name=work%69ng"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 9 mood.set name=abcdefghijklmnop"), &c), LINK_ERR_ARGS); // 16
    CHECK_EQ(parse(frame(">DB1 9 mood.set name=happy secs=0"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 9 mood.set name=happy secs=86401"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 9 mood.set name=happy secs=-5"), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 9 mood.set name=happy secs="), &c), LINK_ERR_ARGS);
    CHECK_EQ(parse(frame(">DB1 9 mood.set name=happy secs=ten"), &c), LINK_ERR_ARGS);
}
