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
#define LINK_REPLY_MAX 320 // longest line the device sends (hello: capabilities, styles, moods, activities, dev, boot)
#define LINK_TEXT_MAX_CHARS 100 // msg.show text: Unicode code points...
#define LINK_TEXT_MAX_BYTES 200 // ...and UTF-8 bytes
#define LINK_MOOD_NAME_MAX 15    // mood.set name and act name: lower-case letters and '_' (the device decides
                                 // which exist)
#define LINK_MOOD_SECS_MAX 86400 // mood.set secs: at most a day

typedef enum {
    LINK_CMD_HELLO = 1, // -> ok proto= fw= caps= styles= moods= acts=
    LINK_CMD_PING,      // -> ok
    LINK_CMD_STATUS,    // -> ok mode= mood= time= msg= uptime=
    LINK_CMD_POKE,      // the surprise reaction, as if the robot had been tapped
    LINK_CMD_MONSTER,   // the Monster Energy sequence
    LINK_CMD_MODE_SET,  // mode=office|normal
    LINK_CMD_TIME_SET,  // utc= [ms=] off= [next= next_off=]
    LINK_CMD_MSG_SHOW,  // id= [style=] text=
    LINK_CMD_MOOD_SET,  // name= [secs=]
    LINK_CMD_ACT,       // name= [iid=]: an activity (hello acts=); iid identifies it in its evt act events
    LINK_CMD_PDC_SET,   // state=on|off [count=]: the PDC Manager integration, as the companion sees it (a lease)
    LINK_CMD_PDC_RESULT, // id= result= [count=] [teams=]: how a ZDMW click (evt pdc.click) ended
    LINK_CMD_LOG_SINCE, // [boot=] seq=: replays the recorded events after seq (see app_events.h)
    LINK_CMD_CLOCK_FACE, // name=: Clock Mode's face (hello faces=)
} link_cmd_t;

#define LINK_PDC_COUNT_MAX 2147483647

// How a ZDMW click ended, as the companion reports it (pdc.result result=). Names are listed in link.c; a
// well-formed name this firmware does not know counts as UNKNOWN (a newer companion: maybe sent).
typedef enum {
    LINK_PDC_OK,          // PDC Manager confirmed it (count, teams)
    LINK_PDC_AUTH,        // not sent: log in again (in the companion)
    LINK_PDC_DENIED,      // not sent: no ZDMW write permission
    LINK_PDC_OFFLINE,     // not sent: PDC Manager could not be reached
    LINK_PDC_TLS,         // not sent: the server's certificate was rejected
    LINK_PDC_TIMEOUT,     // no answer in time: maybe sent
    LINK_PDC_UNKNOWN,     // unexpected answer or connection lost after sending: maybe sent
    LINK_PDC_COOLDOWN,    // not sent: too soon after the previous click
    LINK_PDC_BUSY,        // not sent: another click is under way
    LINK_PDC_UNAVAILABLE, // not sent: the integration is off (or not logged in, or no permission)
    LINK_PDC_RESULT_COUNT,
} link_pdc_result_t;

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
        struct {
            char name[LINK_MOOD_NAME_MAX + 1];
            uint32_t secs; // 0: until changed
        } mood; // MOOD_SET
        struct {
            char name[LINK_MOOD_NAME_MAX + 1];
            uint32_t iid; // chosen by the companion (1..2^31-1); 0: not given (the device picks one)
        } act; // ACT
        struct {
            char name[LINK_MOOD_NAME_MAX + 1];
        } face; // CLOCK_FACE
        struct {
            bool on;
            int32_t count; // -1: not given
        } pdc_set; // PDC_SET
        struct {
            uint16_t id; // 1..65535, from evt pdc.click
            link_pdc_result_t result;
            int32_t count; // -1: not given
            int8_t teams;  // the Teams message went out: 1, 0; -1: not given
        } pdc_result; // PDC_RESULT
        struct {
            bool has_boot;
            uint64_t boot; // the boot (hello boot=) seq counts in; another one means: everything of this boot
            uint32_t seq;  // the last event the companion has stored (0: none)
        } log_since; // LOG_SINCE
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
