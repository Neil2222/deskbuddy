#include "link.h"

#include <stdio.h>
#include <string.h>

#define MAX_ARGS 8
#define NAME_MAX 24
#define VALUE_MAX 32                         // ordinary values
#define TEXT_VALUE_MAX (3 * LINK_TEXT_MAX_BYTES) // the percent-encoded text of msg.show

typedef struct {
    const char *key, *value;
    size_t key_len, value_len;
} arg_t;

static const struct {
    const char *name;
    link_cmd_t cmd;
} COMMANDS[] = {
    {"hello", LINK_CMD_HELLO},       {"ping", LINK_CMD_PING},       {"status", LINK_CMD_STATUS},
    {"poke", LINK_CMD_POKE},         {"monster", LINK_CMD_MONSTER}, {"mode.set", LINK_CMD_MODE_SET},
    {"time.set", LINK_CMD_TIME_SET}, {"msg.show", LINK_CMD_MSG_SHOW},
};

static const struct {
    const char *name;
    link_style_t style;
} STYLES[] = {
    {"marquee", LINK_STYLE_MARQUEE},
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

// '%' introduces a percent-encoded byte; only the text of msg.show uses it.
static bool value_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c) || c == '.' || c == '_' || c == ':' ||
           c == '+' || c == '-' || c == '%';
}

static bool all(const char *p, size_t len, bool (*ok)(char))
{
    for (size_t i = 0; i < len; i++) {
        if (!ok(p[i])) {
            return false;
        }
    }
    return true;
}

// Copies the token [p, p + len) into out (size n), NUL-terminated. False if empty or too long.
static bool copy(const char *p, size_t len, char *out, size_t n)
{
    if (len == 0 || len >= n) {
        return false;
    }
    memcpy(out, p, len);
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

static const arg_t *find_arg(const arg_t *args, int n, const char *key)
{
    for (int i = 0; i < n; i++) {
        if (args[i].key_len == strlen(key) && memcmp(args[i].key, key, args[i].key_len) == 0) {
            return &args[i];
        }
    }
    return NULL;
}

// An ordinary argument as a C string (NULL if absent, too long or containing '%').
static const char *get_arg(const arg_t *args, int n, const char *key, char out[VALUE_MAX])
{
    const arg_t *a = find_arg(args, n, key);
    if (!a || memchr(a->value, '%', a->value_len) || !copy(a->value, a->value_len, out, VALUE_MAX)) {
        return NULL;
    }
    return out;
}

// Length of the UTF-8 sequence at p (at most len bytes), or 0 if it is not valid UTF-8: overlong forms,
// surrogates and code points above U+10FFFF are invalid.
static size_t utf8_sequence(const uint8_t *p, size_t len)
{
    const uint8_t b = p[0];
    size_t n;
    uint32_t cp, min;
    if (b < 0x80) {
        return 1;
    } else if ((b & 0xE0) == 0xC0) {
        n = 2, cp = b & 0x1F, min = 0x80;
    } else if ((b & 0xF0) == 0xE0) {
        n = 3, cp = b & 0x0F, min = 0x800;
    } else if ((b & 0xF8) == 0xF0) {
        n = 4, cp = b & 0x07, min = 0x10000;
    } else {
        return 0;
    }
    if (n > len) {
        return 0;
    }
    for (size_t i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            return 0;
        }
        cp = cp << 6 | (p[i] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;
    }
    return n;
}

// Percent-decodes the text argument into out and checks it is valid UTF-8 within the length limits.
static bool decode_text(const arg_t *a, char out[LINK_TEXT_MAX_BYTES + 1])
{
    size_t n = 0;
    for (size_t i = 0; i < a->value_len; i++) {
        int c = (uint8_t)a->value[i];
        if (c == '%') {
            const int hi = i + 2 < a->value_len ? hex_value(a->value[i + 1]) : -1;
            const int lo = hi >= 0 ? hex_value(a->value[i + 2]) : -1;
            if (lo < 0) {
                return false;
            }
            c = hi << 4 | lo;
            i += 2;
        }
        if (c == 0 || n == LINK_TEXT_MAX_BYTES) {
            return false;
        }
        out[n++] = (char)c;
    }
    out[n] = '\0';
    size_t chars = 0;
    for (size_t i = 0; i < n; chars++) {
        const size_t len = utf8_sequence((const uint8_t *)out + i, n - i);
        if (len == 0) {
            return false;
        }
        i += len;
    }
    return chars >= 1 && chars <= LINK_TEXT_MAX_CHARS;
}

#define OFFSET_MIN_LIMIT (15 * 60) // UTC-12 .. UTC+14, with room
#define UTC_LIMIT 4102444800LL     // 2100-01-01

// Command-specific arguments. Unknown keys are ignored, so newer companions can send more.
static link_result_t decode_args(link_command_t *c, const arg_t *args, int n)
{
    char v[5][VALUE_MAX];
    if (c->cmd == LINK_CMD_MODE_SET) {
        const char *mode = get_arg(args, n, "mode", v[0]);
        if (!mode || (strcmp(mode, "office") != 0 && strcmp(mode, "normal") != 0)) {
            return LINK_ERR_ARGS;
        }
        c->u.office = strcmp(mode, "office") == 0;
    } else if (c->cmd == LINK_CMD_TIME_SET) {
        const char *utc = get_arg(args, n, "utc", v[0]), *ms = get_arg(args, n, "ms", v[1]);
        const char *off = get_arg(args, n, "off", v[2]), *next = get_arg(args, n, "next", v[3]);
        const char *next_off = get_arg(args, n, "next_off", v[4]);
        int64_t v_utc, v_ms = 0, v_off, v_next = 0, v_next_off = 0;
        if (!utc || !off || !parse_int(utc, 0, UTC_LIMIT, &v_utc) ||
            !parse_int(off, -OFFSET_MIN_LIMIT, OFFSET_MIN_LIMIT, &v_off) || (ms && !parse_int(ms, 0, 999, &v_ms)) ||
            (!next != !next_off) || (next && !parse_int(next, 1, UTC_LIMIT, &v_next)) ||
            (next_off && !parse_int(next_off, -OFFSET_MIN_LIMIT, OFFSET_MIN_LIMIT, &v_next_off))) {
            return LINK_ERR_ARGS;
        }
        c->u.time.utc = v_utc;
        c->u.time.ms = (uint16_t)v_ms;
        c->u.time.tz = (link_tz_t){(int32_t)v_off, v_next, (int32_t)v_next_off};
    } else if (c->cmd == LINK_CMD_MSG_SHOW) {
        const char *id = get_arg(args, n, "id", v[0]), *style = get_arg(args, n, "style", v[1]);
        const arg_t *text = find_arg(args, n, "text");
        int64_t v_id;
        if (!id || !parse_int(id, 1, UINT32_MAX, &v_id) || !text || !decode_text(text, c->u.msg.text) ||
            (find_arg(args, n, "style") && !style)) {
            return LINK_ERR_ARGS;
        }
        c->u.msg.id = (uint32_t)v_id;
        c->u.msg.style = LINK_STYLE_MARQUEE; // the default
        if (style) {
            c->u.msg.style = 0;
            for (size_t i = 0; i < sizeof(STYLES) / sizeof(STYLES[0]); i++) {
                if (strcmp(STYLES[i].name, style) == 0) {
                    c->u.msg.style = STYLES[i].style;
                }
            }
            if (!c->u.msg.style) {
                return LINK_ERR_UNSUPPORTED;
            }
        }
    }
    return LINK_OK;
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
    const char *tok[2 + MAX_ARGS + 1];
    size_t tok_len[2 + MAX_ARGS + 1];
    int ntok = 0;
    for (p++; p < end;) {
        const char *s = p;
        while (p < end && *p != ' ') {
            p++;
        }
        if (p == s || ntok == (int)(sizeof(tok) / sizeof(tok[0]))) {
            return ntok < 2 ? LINK_ERR_SYNTAX : LINK_ERR_ARGS;
        }
        tok[ntok] = s;
        tok_len[ntok++] = (size_t)(p - s);
        if (p < end) {
            p++; // the space
        }
    }
    if (ntok < 2) {
        return LINK_ERR_SYNTAX;
    }
    char num[12], name[NAME_MAX];
    int64_t seq;
    if (!all(tok[0], tok_len[0], is_digit) || !copy(tok[0], tok_len[0], num, sizeof(num)) ||
        !parse_int(num, 0, UINT32_MAX, &seq) || !all(tok[1], tok_len[1], name_char) ||
        !copy(tok[1], tok_len[1], name, sizeof(name))) {
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
        const char *eq = memchr(tok[i], '=', tok_len[i]);
        arg_t *a = &args[i - 2];
        if (!eq) {
            return LINK_ERR_ARGS;
        }
        *a = (arg_t){tok[i], eq + 1, (size_t)(eq - tok[i]), tok_len[i] - (size_t)(eq + 1 - tok[i])};
        if (a->key_len == 0 || a->key_len >= NAME_MAX || !all(a->key, a->key_len, name_char) ||
            a->value_len == 0 || a->value_len > TEXT_VALUE_MAX || !all(a->value, a->value_len, value_char)) {
            return LINK_ERR_ARGS;
        }
    }
    return decode_args(out, args, ntok - 2);
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
    case LINK_ERR_UNSUPPORTED:
        return "unsupported";
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
