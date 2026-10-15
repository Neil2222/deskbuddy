# DeskBuddy serial protocol, version 1

DeskBuddy and the Windows companion (`companion/`) talk over the board's **UART** Type-C port (CH343, 115200 8N1). The same line carries the firmware's log output; protocol frames are whole lines that start with `>DB` or `<DB`, and everything else is log text.

- **Firmware:** `firmware/components/link` (parsing and formatting, host-tested), `firmware/deskbuddy/main/serial_link.c` (UART task), `run_command()` in `main.c`.
- **Companion:** `companion/DeskBuddy.Companion/Protocol/Frame.cs`, `Device/DeviceLink.cs`.

## Frames

```
>DB1 <seq> <command> [key=value ...] #<crc>      companion -> device
<DB1 <seq> ok [key=value ...] #<crc>             reply to command <seq>
<DB1 <seq> err <code> [key=value ...] #<crc>     error reply
<DB1 0 evt <name> [key=value ...] #<crc>         unsolicited event
```

- `DB1`: protocol version 1. A device that receives another version answers `err bad_version proto=1`.
- `<seq>`: 1 to 4294967295, chosen by the companion; the reply carries the same number. Events and errors without a trustworthy sequence number (bad checksum) use 0.
- `<crc>`: CRC-16/CCITT-FALSE (polynomial 0x1021, initial 0xFFFF, no reflection; `123456789` gives `29B1`) over everything before ` #`, as 4 hex digits.
- Single spaces between tokens. Names: lower-case letters, digits, `.` and `_`. Values: letters, digits and `. _ : + -`; `%XX` escapes only in a message `text`. At most 8 arguments; received lines at most 767 bytes, sent lines at most 159.
- Lines end with `\n` (the device sends `\r\n`).

**Safety:** only a complete, well-formed command line with a matching checksum does anything. Plain text (typing in a serial monitor), the device's own output echoed back, esptool traffic and corrupted lines are ignored or rejected. During flashing the firmware is not even running. The companion does not retry actions after a timeout, so a lost reply cannot trigger an action twice.

## Commands

| Command | Arguments | Reply |
|---|---|---|
| `hello` | | `ok proto=1 fw=<version> caps=<list> styles=<list> moods=<list>` |
| `ping` | | `ok` |
| `status` | | `ok mode=<normal\|office\|monster> mood=<mood> time=<set\|unset> msg=<showing\|idle> queued=<0\|1> uptime=<s>` (`office`: Clock Mode, see below) |
| `poke` | | `ok`, or `err busy mode=<mode>` (Clock Mode, Monster running, a zoom under way, screen not up yet) |
| `monster` | | `ok`, or `err busy mode=<mode>` |
| `mode.set` | `mode=office\|clock\|normal` | `ok` (switches like *Clock* in the touchscreen menu / a tap on the clock: Buddy zooms into the clock or out of it, about 1.5 s; `evt mode` follows when the new scene is on screen; a request during a zoom is carried out after it), or `err unavailable` |
| `time.set` | `utc=<s> [ms=<0-999>] off=<min> [next=<utc s> next_off=<min>]` | `ok` |
| `msg.show` | `id=<1-4294967295> [style=marquee] text=<percent-encoded UTF-8>` | `ok` (on the sign now), `ok queued=1` (waits its turn), `ok dup=1` (this id was already taken), `err busy mode=<mode>` (one is already waiting), `err unsupported` (unknown style), `err bad_args` |
| `mood.set` | `name=<mood> [secs=<1-86400>]` | `ok`, `err unsupported` (a mood this firmware does not have), `err bad_args` |

**Capabilities:** `hello` lists what the firmware supports (`poke,monster,mode,time,status,msg,mood`), which message styles it draws (`styles=marquee`) and which moods it has (`moods=idle,relaxed,peukie,angry,sleepy,working,happy`, in display order). The companion only enables the matching buttons, so new commands, styles and moods can be added without breaking older companions or firmware.

**Clock Mode is `office` on the wire.** It was first called Office Mode; `status`, `evt mode` and `mode.set` keep that name, so existing companions and scripts work unchanged. `mode.set mode=clock` is accepted as an alias (firmware from 2026-10-15 on; older firmware answers `err bad_args`, so clients that must work with any firmware send `office`).

**Unknown arguments are ignored**, so newer companions can send more. Unknown commands get `err unknown_cmd`; missing or invalid arguments get `err bad_args`.

**Time:** `utc` is the current time; `off` is the local offset from UTC in minutes; `next`/`next_off` announce the next offset change (daylight saving) so the clock switches correctly even while the companion is disconnected. The board has no RTC, so after power loss the clock is unset until the companion sends the time; it shows *CLOCK NOT SET* meanwhile.

**Messages (`msg.show`):**
- **Text:** UTF-8, 1 to 100 characters (Unicode code points) and at most 200 bytes, percent-encoded: letters, digits and `. _ : + -` as they are, every other byte as `%XX` (`Hallo%2C%20Buddy%21`). Invalid UTF-8 (truncated, overlong, surrogates, above U+10FFFF), `%00`, broken escapes and over-long text are rejected with `err bad_args`.
- **What the LED font shows:** printable ASCII; accented Latin letters without their accents (`café` shows as `cafe`); typographic quotes, dashes and the ellipsis as `' " - .`; € ° and hearts as their own glyphs; control characters as spaces; emoji variation selectors and zero-width characters dropped; everything else (emoji, other scripts) as a hollow box. The companion sends the text as typed (normalised to NFC, line breaks and tabs as spaces).
- **`id`:** chosen by the companion (random start per run, then counting up). The same id again is acknowledged with `ok dup=1` and not shown twice, so a resent command cannot show a message twice. The companion never resends on its own.
- **Queue:** one message can wait. It waits while another message is on the sign, or while a Monster sequence runs (it never interrupts one). A second waiting message gets `err busy`.
- **Clock Mode** stays on: the sign appears below the clock, without the robot's reaction.
- **`style`:** how to present it; only `marquee` exists. Later styles (speech bubble, notification) get their own names here and in `hello`'s `styles` list; the command stays the same.

**Moods (`mood.set`)** say how Buddy is; activities (`poke`, `monster`) are things he does, on top of his mood:
- **The moods:** `idle` (the default, after every start), `relaxed` (calm and content), `peukie` (a smoke break), `angry`, `sleepy`, `working`, `happy`. Names are lower-case letters and `_`, at most 15; a well-formed name the firmware does not know gets `err unsupported`, so integrations can ask for newer moods safely.
- **Exactly one at a time:** a new mood replaces the current one and starts its animation from the beginning; the same mood again keeps running (only its duration changes).
- **`secs`:** back to `idle` after that many seconds (`evt mood` reports it); without it the mood stays until changed. Useful for events: `mood.set name=happy secs=30` after a successful build.
- **Always accepted,** also in Clock Mode and during a Monster sequence or a tap reaction: those play to their end, then the new mood shows. Messages, taps and Monster still interrupt any mood, which then resumes.
- **One source of truth:** the device owns the mood. Moods picked on the touchscreen menu go the same way as `mood.set`, and every change, from either side, is announced with `evt mood`; the companion shows what the device reports.
- **For integrations:** an AI agent working, a build or a download in progress → `working`; done → `happy secs=…`; failed → `angry secs=…`; a coffee or smoke break → `peukie`; quiet, nothing going on → `relaxed`; an idle computer → `sleepy`; back → `idle`.

## Events

| Event | When |
|---|---|
| `evt boot proto=1 fw=<version>` | the firmware started (the companion re-sends the time) |
| `evt mode mode=<normal\|office\|monster>` | the mode changed, by touch or by command |
| `evt mood mood=<mood>` | the mood changed: by `mood.set`, on the touchscreen menu, or a timed mood ran out |
| `evt msg id=<id> state=<showing\|done>` | a message went up on the sign / finished |

## Errors

`bad_crc`, `bad_version`, `bad_syntax`, `unknown_cmd`, `bad_args`, `unsupported`, `busy`, `unavailable`.

## Example

```
>DB1 6 monster #3F7C
<DB1 6 ok #3425
<DB1 0 evt mode mode=monster #04EC
>DB1 7 monster #D45F
<DB1 7 err busy mode=monster #7C4C
```

## Adding a command

1. Firmware: add it to `link_cmd_t` and `COMMANDS` in `components/link`, decode its arguments in `decode_args()`, run it in `run_command()` (`main.c`), add its capability to `CAPS` in `serial_link.c`, and add host tests (`firmware/tests/host/test_link.c`).
2. Companion: add a button (`MainForm.cs`) that sends it when the capability is present.
3. Document it here.

## Adding a mood

1. Firmware: add it to `buddy_mood_t` and its name in `components/buddy/src/buddy_mood.c` (the name list feeds `hello`'s `moods=` and `mood.set`), its face in `k_faces` there, and write its loop from the shared pieces (expressions, the hand, particles in `buddy_pose.h`); add its tile to the touchscreen menu (`MOOD(...)` in `components/menu/src/menu_items.c`); extend the host tests (`test_mood.c`).
2. Companion: add an entry to `Activities.cs` with its face (`tools/host-test.ps1 -Icons` exports `face-<mood>.png` from the firmware's art, using the same `k_faces`).
3. Document it here.
