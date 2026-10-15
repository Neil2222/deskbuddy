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
- Single spaces between tokens. Names: lower-case letters, digits, `.` and `_`. Values: letters, digits and `. _ : + -`; `%XX` escapes only in a message `text`. At most 8 arguments; received lines at most 767 bytes, sent lines at most 255.
- Lines end with `\n` (the device sends `\r\n`).

**Safety:** only a complete, well-formed command line with a matching checksum does anything. Plain text (typing in a serial monitor), the device's own output echoed back, esptool traffic and corrupted lines are ignored or rejected. During flashing the firmware is not even running. The companion does not retry actions after a timeout, so a lost reply cannot trigger an action twice.

## Commands

| Command | Arguments | Reply |
|---|---|---|
| `hello` | | `ok proto=1 fw=<version> caps=<list> styles=<list> moods=<list> acts=<list>` |
| `ping` | | `ok` |
| `status` | | `ok mode=<normal\|office\|monster> mood=<mood> time=<set\|unset> msg=<showing\|idle> queued=<0\|1> uptime=<s>` (`office`: Clock Mode, see below) |
| `poke` | | `ok`, or `err busy mode=<mode>` (Clock Mode, an activity running (Monster, water, sneeze, stare, meditate, focus, the peukie question), a zoom under way, screen not up yet) |
| `monster` | | `ok`, or `err busy mode=<mode>` (as `poke`) |
| `act` | `name=<activity>` | `ok` (it starts), `err busy mode=<mode>` (as `poke`), `err unsupported` (an activity this firmware does not have), `err bad_args` |
| `mode.set` | `mode=office\|clock\|normal` | `ok` (switches like *Clock* in the touchscreen menu / a tap on the clock: Buddy zooms into the clock or out of it, about 1.5 s; `evt mode` follows when the new scene is on screen; a request during a zoom is carried out after it), or `err unavailable` |
| `time.set` | `utc=<s> [ms=<0-999>] off=<min> [next=<utc s> next_off=<min>]` | `ok` |
| `msg.show` | `id=<1-4294967295> [style=marquee] text=<percent-encoded UTF-8>` | `ok` (on the sign now), `ok queued=1` (waits its turn), `ok dup=1` (this id was already taken), `err busy mode=<mode>` (one is already waiting), `err unsupported` (unknown style), `err bad_args` |
| `mood.set` | `name=<mood> [secs=<1-86400>]` | `ok`, `err unsupported` (a mood this firmware does not have), `err bad_args` |
| `pdc.set` | `state=on\|off [count=<0-2147483647>]` | `ok` (the PDC Manager lease, see *PDC Manager (ZDMW)*) |
| `pdc.result` | `id=<1-65535> result=<ok\|auth\|denied\|offline\|tls\|timeout\|unknown\|cooldown\|busy\|unavailable> [count=<n>] [teams=1\|0]` | `ok` (how the click `evt pdc.click id=` ended) |

**Capabilities:** `hello` lists what the firmware supports (`poke,monster,mode,time,status,msg,mood,act,pdc`), which message styles it draws (`styles=marquee`), which moods it has (`moods=idle,relaxed,peukie,angry,sleepy,working,happy`) and which activities `act` starts (`acts=water,sneeze,stare,meditate,focus`), each in display order. The companion only enables the matching buttons, so new commands, styles, moods and activities can be added without breaking older companions or firmware. Without `pdc` the companion sends no `pdc.*` command at all.

**Activities (`act`)** are short shows on top of Buddy's mood, the same ones the touchscreen menu starts: `water` (a glass of water short-circuits him), `sneeze` (his head flies off), `stare` (a staring contest, the camera close on his face), `meditate` (he floats until a notification drops him), `focus` (he builds a plan in his head until someone asks for "1 sec": POOF; older firmware answers `err unsupported`). Each plays to its end (2.7 to 9 s); meanwhile `poke`, `monster` and `act` get `err busy`, and the mood resumes afterwards. Names are lower-case letters and `_`, at most 15; `monster` and `poke` keep their own commands.

**The peukie question** (*Peukie?* in the touchscreen menu) is answered on the touchscreen only, so it has no command. While Buddy asks (up to a minute) and while he is sad after a *no*, he is busy as during an activity. A *yes* starts a smoke break: `evt mood mood=peukie`, and two minutes later `evt mood` with the mood he had before.

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
| `evt mood mood=<mood>` | the mood changed: by `mood.set`, on the touchscreen menu, a timed mood ran out, or a smoke break began or ended |
| `evt msg id=<id> state=<showing\|done>` | a message went up on the sign / finished |
| `evt pdc.click id=<1-65535>` | the user confirmed *JA* for Ze Doen Maar Wat on the touchscreen (sent once per click; see below) |

## PDC Manager (ZDMW)

The optional "Ze Doen Maar Wat" button of PDC Manager (`https://pdcmanager`): a counter that also posts a Teams message. DeskBuddy never talks to the network and holds no credentials; the companion does the HTTPS, the login and the session (`companion/DeskBuddy.Companion/Pdc/`), and only offers it while the user switched the integration on (off by default).

- **The lease:** `pdc.set state=on` means: integration on, logged in, PDC Manager reachable, and ZDMW write permission; `count=` is the current ZDMW count (with `state=on`, when known). The companion sends it right after `hello`, on every change of state or count, and repeats it every 10 s while connected; switched off, logged out or closing, it sends `state=off`. The device drops the lease after 30 s without a refresh and on `hello`, so a companion that went away cannot leave the button on.
- **A click:** after *JA*, the device sends `evt pdc.click id=<n>` once (ids start at a random value on every boot). The companion sends one `POST /zdmw/api/click` and answers with `pdc.result id=<n> result=…`: `count` with `ok` (and whenever it is known), `teams=1|0` only with `ok` (whether the Teams message went out).
- **Results:** `ok` the server confirmed it. Not sent: `auth` (login required; the companion deletes the session), `denied` (403, no write permission), `offline` (no connection at all), `tls` (certificate refused), `cooldown` (within 5 s of the previous click, from the window or the device), `busy` (another click on its way), `unavailable` (off, not logged in, not reachable or no permission). **Maybe sent:** `timeout` (no answer within 10 s) and `unknown` (connection lost after sending, a server error, an unexpected answer): the server may have counted it and posted to Teams, so these are never shown as "not sent".
- **Never twice:** the companion never retries a click, and keeps the ids of this device session: the same id again sends no second click (the known result is sent again; still on its way: no answer). The ids are forgotten on `evt boot` and on a new connection.
- **On the device:** the *PDC* category shows only while the lease holds; a tap asks *ZE DOEN MAAR WAT? / TEAMS-BERICHT VERSTUREN?* (NEE/JA, 15 s, then as if NEE), and only *JA* sends `evt pdc.click`. One click waits for its result at a time, then 5 s from the click before the next; a `pdc.result` for any other id (late, repeated, never sent) is acknowledged and ignored. Without a result within 20 s the device gives up and shows "maybe sent". On `ok` Buddy fumes for about 2.6 s (unless Clock Mode, a zoom or another activity is on) and the sign shows *ZE DOEN MAAR WAT!!! #<count>*; other results show a short line on the sign.

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

## Adding an activity

1. Firmware: its state, event and timeline in `components/buddy` (`buddy_activity.h`), its name and face in `k_acts` in `buddy_activity.c` (the list feeds `hello`'s `acts=` and `act`), and its tile in the touchscreen menu (`menu_items.c`); extend the host tests (`test_activity.c`).
2. Companion: an `ActTile(...)` entry in `Activities.cs` (`tools/host-test.ps1 -Icons` exports `face-<activity>.png` from the same face).
3. Document it here.

## Adding a mood

1. Firmware: add it to `buddy_mood_t` and its name in `components/buddy/src/buddy_mood.c` (the name list feeds `hello`'s `moods=` and `mood.set`), its face in `k_faces` there, and write its loop from the shared pieces (expressions, the hand, particles in `buddy_pose.h`); add its tile to the touchscreen menu (`MOOD(...)` in `components/menu/src/menu_items.c`); extend the host tests (`test_mood.c`).
2. Companion: add an entry to `Activities.cs` with its face (`tools/host-test.ps1 -Icons` exports `face-<mood>.png` from the firmware's art, using the same `k_faces`).
3. Document it here.
