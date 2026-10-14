// The serial protocol between DeskBuddy and the Windows companion (docs/protocol.md).
//
// Text lines on the console UART, mixed with the log output:
//   >DB1 <seq> <command> [key=value ...] #<crc>     companion -> device
//   <DB1 <seq> ok|err <...> #<crc>                  device -> companion: reply to command <seq>
//   <DB1 0 evt <name> [key=value ...] #<crc>        device -> companion: unsolicited event
// <crc> is CRC-16/CCITT-FALSE (4 upper-case hex digits) over everything before " #". A line that does not start
// with ">DB" is not a frame (log text, typing in a serial monitor) and is ignored.
//
// Pure logic: no I/O, no hardware. The firmware's serial task and the host tests use it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINK_PROTO 1
#define LINK_LINE_MAX 768  // longest received line, including the newline (a message with percent-encoded text)
#define LINK_REPLY_MAX 160 // longest line the device sends
#define LINK_TEXT_MAX_CHARS 100 // msg.show text: Unicode code points...
#define LINK_TEXT_MAX_BYTES 200 // ...and UTF-8 bytes

typedef enum {
    LINK_CMD_HELLO = 1, // -> ok proto= fw= caps= styles=
    LINK_CMD_PING,      // -> ok
    LINK_CMD_STATUS,    // -> ok mode= time= msg= uptime=
    LINK_CMD_POKE,      // the surprise reaction, as if the robot had been tapped
    LINK_CMD_MONSTER,   // the Monster Energy sequence
    LINK_CMD_MODE_SET,  // mode=office|normal
    LINK_CMD_TIME_SET,  // utc= [ms=] off= [next= next_off=]
    LINK_CMD_MSG_SHOW,  // id= [style=] text=
} link_cmd_t;

// How a message is presented. Only the marquee exists so far; the wire names are listed in link.c.
typedef enum {
    LINK_STYLE_MARQUEE = 1,
} link_style_t;

// Local time = UTC + offset; the offset changes at a known moment (the next daylight-saving switch), so the clock
// stays right across it even without the companion.
typedef struct {
    int32_t offset_min;
    int64_t next_utc; // UTC seconds of the next change; 0: none known
    int32_t next_offset_min;
} link_tz_t;

typedef struct {
    link_cmd_t cmd;
    uint32_t seq;
    union {
        bool office; // MODE_SET
        struct {
            int64_t utc; // seconds since 1970
            uint16_t ms;
            link_tz_t tz;
        } time; // TIME_SET
        struct {
            uint32_t id; // chosen by the companion; a repeated id is the same message (not shown twice)
            link_style_t style;
            char text[LINK_TEXT_MAX_BYTES + 1]; // valid UTF-8, NUL-terminated, 1..LINK_TEXT_MAX_CHARS code points
        } msg; // MSG_SHOW
    } u;
} link_command_t;

typedef enum {
    LINK_NOT_A_FRAME, // not for us: ignore silently
    LINK_OK,
    LINK_ERR_CRC,
    LINK_ERR_VERSION,
    LINK_ERR_SYNTAX,
    LINK_ERR_UNKNOWN_CMD,
    LINK_ERR_ARGS,
    LINK_ERR_UNSUPPORTED, // well-formed, but asks for something this firmware does not have (e.g. a message style)
} link_result_t;

// Parses one received line (without its newline; a trailing '\r' is ignored). On LINK_OK *out holds the command.
// On LINK_ERR_UNKNOWN_CMD, LINK_ERR_ARGS and LINK_ERR_UNSUPPORTED out->seq holds the sequence number to reply to;
// otherwise it is 0.
link_result_t link_parse_command(const char *line, size_t len, link_command_t *out);

// The error code sent in "err <code>" replies.
const char *link_error_code(link_result_t r);

// Writes "<DB1 <seq> <body> #XXXX\n" into buf (NUL-terminated). Returns the length without the NUL, or 0 if it
// does not fit in size.
size_t link_format(char *buf, size_t size, uint32_t seq, const char *body);

// CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no reflection ("123456789" -> 0x29B1).
uint16_t link_crc16(const void *data, size_t len);

// Offset in minutes from UTC at the given UTC time.
int32_t link_tz_offset_min(const link_tz_t *tz, int64_t utc);

#ifdef __cplusplus
}
#endif
