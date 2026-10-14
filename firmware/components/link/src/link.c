#include "link.h"

#include <stdio.h>
#include <string.h>

#define MAX_ARGS 8
#define NAME_MAX 24
#define KEY_MAX 16
#define VALUE_MAX 32

typedef struct {
    char key[KEY_MAX];
    char value[VALUE_MAX];
} arg_t;

static const struct {
    const char *name;
    link_cmd_t cmd;
} COMMANDS[] = {
    {"hello", LINK_CMD_HELLO},     {"ping", LINK_CMD_PING},       {"status", LINK_CMD_STATUS},
    {"poke", LINK_CMD_POKE},       {"monster", LINK_CMD_MONSTER}, {"mode.set", LINK_CMD_MODE_SET},
    {"time.set", LINK_CMD_TIME_SET},
};

uint16_t link_crc16(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int hex_value(char c)
{
    if (is_digit(c)) {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static bool name_char(char c)
{
    return (c >= 'a' && c <= 'z') || is_digit(c) || c == '.' || c == '_';
}

static bool value_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c) || c == '.' || c == '_' || c == ':' ||
           c == '+' || c == '-';
}

// Copies the token [p, end) into out (size n) if every character passes ok(). False if empty, too long or invalid.
static bool take(const char *p, const char *end, char *out, size_t n, bool (*ok)(char))
{
    const size_t len = (size_t)(end - p);
    if (len == 0 || len >= n) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!ok(p[i])) {
            return false;
        }
        out[i] = p[i];
    }
    out[len] = '\0';
    return true;
}

// Decimal integer with an optional minus sign, within [lo, hi].
static bool parse_int(const char *s, int64_t lo, int64_t hi, int64_t *out)
{
    const bool neg = *s == '-';
    s += neg;
    if (!*s) {
        return false;
    }
    int64_t v = 0;
    for (; *s; s++) {
        if (!is_digit(*s) || v > (INT64_MAX - 9) / 10) {
            return false;
        }
        v = v * 10 + (*s - '0');
    }
    v = neg ? -v : v;
    if (v < lo || v > hi) {
        return false;
    }
    *out = v;
    return true;
}

static const char *find_arg(const arg_t *args, int n, const char *key)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(args[i].key, key) == 0) {
            return args[i].value;
        }
    }
    return NULL;
}

#define OFFSET_MIN_LIMIT (15 * 60) // UTC-12 .. UTC+14, with room
#define UTC_LIMIT 4102444800LL     // 2100-01-01

// Command-specific arguments. Unknown keys are ignored, so newer companions can send more.
static bool decode_args(link_command_t *c, const arg_t *args, int n)
{
    if (c->cmd == LINK_CMD_MODE_SET) {
        const char *mode = find_arg(args, n, "mode");
        if (!mode || (strcmp(mode, "office") != 0 && strcmp(mode, "normal") != 0)) {
            return false;
        }
        c->u.office = strcmp(mode, "office") == 0;
    } else if (c->cmd == LINK_CMD_TIME_SET) {
        const char *utc = find_arg(args, n, "utc"), *ms = find_arg(args, n, "ms"), *off = find_arg(args, n, "off");
        const char *next = find_arg(args, n, "next"), *next_off = find_arg(args, n, "next_off");
        int64_t v_utc, v_ms = 0, v_off, v_next = 0, v_next_off = 0;
        if (!utc || !off || !parse_int(utc, 0, UTC_LIMIT, &v_utc) ||
            !parse_int(off, -OFFSET_MIN_LIMIT, OFFSET_MIN_LIMIT, &v_off) || (ms && !parse_int(ms, 0, 999, &v_ms)) ||
            (!next != !next_off) || (next && !parse_int(next, 1, UTC_LIMIT, &v_next)) ||
            (next_off && !parse_int(next_off, -OFFSET_MIN_LIMIT, OFFSET_MIN_LIMIT, &v_next_off))) {
            return false;
        }
        c->u.time.utc = v_utc;
        c->u.time.ms = (uint16_t)v_ms;
        c->u.time.tz = (link_tz_t){(int32_t)v_off, v_next, (int32_t)v_next_off};
    }
    return true;
}

link_result_t link_parse_command(const char *line, size_t len, link_command_t *out)
{
    memset(out, 0, sizeof(*out));
    if (len > 0 && line[len - 1] == '\r') {
        len--;
    }
    if (len < 3 || memcmp(line, ">DB", 3) != 0) {
        return LINK_NOT_A_FRAME;
    }
    // Version: digits right after ">DB". A different version may use a different layout, so check nothing else.
    const char *p = line + 3, *end = line + len;
    int64_t version = 0;
    while (p < end && is_digit(*p) && version < 1000) {
        version = version * 10 + (*p++ - '0');
    }
    if (p == line + 3 || p == end || *p != ' ') {
        return LINK_ERR_SYNTAX;
    }
    if (version != LINK_PROTO) {
        return LINK_ERR_VERSION;
    }
    // Checksum: " #XXXX" at the very end, over everything before it.
    if (len < 6 || end[-6] != ' ' || end[-5] != '#') {
        return LINK_ERR_SYNTAX;
    }
    uint16_t sent = 0;
    for (int i = 4; i > 0; i--) {
        const int h = hex_value(end[-i]);
        if (h < 0) {
            return LINK_ERR_SYNTAX;
        }
        sent = (uint16_t)(sent << 4 | h);
    }
    end -= 6;
    if (link_crc16(line, (size_t)(end - line)) != sent) {
        return LINK_ERR_CRC;
    }

    // Tokens: <seq> <name> [key=value ...], single spaces.
    const char *tok[2 + MAX_ARGS + 1][2];
    int ntok = 0;
    for (p++; p < end;) {
        const char *s = p;
        while (p < end && *p != ' ') {
            p++;
        }
        if (p == s || ntok == (int)(sizeof(tok) / sizeof(tok[0]))) {
            return ntok < 2 ? LINK_ERR_SYNTAX : LINK_ERR_ARGS;
        }
        tok[ntok][0] = s;
        tok[ntok][1] = p;
        ntok++;
        if (p < end) {
            p++; // the space
        }
    }
    if (ntok < 2) {
        return LINK_ERR_SYNTAX;
    }
    char num[12], name[NAME_MAX];
    int64_t seq;
    if (!take(tok[0][0], tok[0][1], num, sizeof(num), is_digit) || !parse_int(num, 0, UINT32_MAX, &seq) ||
        !take(tok[1][0], tok[1][1], name, sizeof(name), name_char)) {
        return LINK_ERR_SYNTAX;
    }
    out->seq = (uint32_t)seq;

    for (size_t i = 0; i < sizeof(COMMANDS) / sizeof(COMMANDS[0]); i++) {
        if (strcmp(COMMANDS[i].name, name) == 0) {
            out->cmd = COMMANDS[i].cmd;
        }
    }
    if (!out->cmd) {
        return LINK_ERR_UNKNOWN_CMD;
    }
    if (ntok - 2 > MAX_ARGS) {
        return LINK_ERR_ARGS;
    }
    arg_t args[MAX_ARGS];
    for (int i = 2; i < ntok; i++) {
        const char *eq = memchr(tok[i][0], '=', (size_t)(tok[i][1] - tok[i][0]));
        if (!eq || !take(tok[i][0], eq, args[i - 2].key, KEY_MAX, name_char) ||
            !take(eq + 1, tok[i][1], args[i - 2].value, VALUE_MAX, value_char)) {
            return LINK_ERR_ARGS;
        }
    }
    return decode_args(out, args, ntok - 2) ? LINK_OK : LINK_ERR_ARGS;
}

const char *link_error_code(link_result_t r)
{
    switch (r) {
    case LINK_ERR_CRC:
        return "bad_crc";
    case LINK_ERR_VERSION:
        return "bad_version";
    case LINK_ERR_SYNTAX:
        return "bad_syntax";
    case LINK_ERR_UNKNOWN_CMD:
        return "unknown_cmd";
    case LINK_ERR_ARGS:
        return "bad_args";
    default:
        return "internal";
    }
}

size_t link_format(char *buf, size_t size, uint32_t seq, const char *body)
{
    const int n = snprintf(buf, size, "<DB%d %lu %s", LINK_PROTO, (unsigned long)seq, body);
    if (n < 0 || (size_t)n + 7 >= size) { // " #XXXX\n" and the NUL
        return 0;
    }
    snprintf(buf + n, size - (size_t)n, " #%04X\n", link_crc16(buf, (size_t)n));
    return (size_t)n + 7;
}

int32_t link_tz_offset_min(const link_tz_t *tz, int64_t utc)
{
    return tz->next_utc != 0 && utc >= tz->next_utc ? tz->next_offset_min : tz->offset_min;
}
