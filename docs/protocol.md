# DeskBuddy serial protocol, version 1

DeskBuddy and the Windows companion (`companion/`) talk over the board's **UART** Type-C port (CH343, 115200 8N1). The same line carries the firmware's log output; protocol frames are whole lines that start with `>DB` or `<DB`, and everything else is log text.

The desktop simulator (`tools/simulator.ps1`) carries the same byte stream, frames and log lines, over **TCP on 127.0.0.1:47801** instead of the UART. It accepts one companion at a time; the companion lists it as *Simulator*.

- **Firmware:** `firmware/components/link` (parsing and formatting, host-tested), `firmware/components/app` (`app_link.c`: lines, `hello`/`ping`, `CAPS`; `run_command()` in `app.c`), and the transport underneath: `firmware/deskbuddy/main/serial_link.c` (UART task) or `firmware/simulator/sim_port.c` (TCP thread).
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
| `hello` | | `ok proto=1 fw=<version> caps=<list> styles=<list> moods=<list> acts=<list> faces=<list> dev=<device id> boot=<16 hex>` (`dev`, `boot`: cap `log`, see *Recorded events*; `faces=`: cap `face`) |
| `ping` | | `ok` |
| `status` | | `ok mode=<normal\|office\|monster\|game> mood=<mood> time=<set\|unset> msg=<showing\|idle> queued=<0\|1> uptime=<s> act=<none\|iid:state[:result]> face=<face>` (`office`: Clock Mode, see below; `game`: Tic-Tac-Toe on the touchscreen; `act=`: the last interaction, see *Interactions*; `face=`: cap `face`) |
| `poke` | | `ok`, or `err busy mode=<mode>` (Clock Mode, an activity running (Monster, water, sneeze, stare, meditate, focus, the peukie question), a game (`mode=game`), a zoom under way, screen not up yet) |
| `monster` | | `ok`, or `err busy mode=<mode>` (as `poke`) |
| `act` | `name=<activity> [iid=<1-4294967295>]` | `ok` (it starts), `ok dup=1 state=<started\|shown\|done>` (this iid already started: a retry never starts it twice), `err busy mode=<mode>` (as `poke`), `err unsupported` (an activity this firmware does not have), `err bad_args` |
| `mode.set` | `mode=office\|clock\|normal` | `ok` (switches like *Clock* in the touchscreen menu / a tap on the clock: Buddy zooms into the clock or out of it, about 1.5 s; `evt mode` follows when the new scene is on screen; a request during a zoom is carried out after it), or `err unavailable` |
| `time.set` | `utc=<s> [ms=<0-999>] off=<min> [next=<utc s> next_off=<min>]` | `ok` |
| `msg.show` | `id=<1-4294967295> [style=marquee] text=<percent-encoded UTF-8>` | `ok` (on the sign now), `ok queued=1` (waits its turn), `ok dup=1` (this id was already taken), `err busy mode=<mode>` (one is already waiting), `err unsupported` (unknown style), `err bad_args` |
| `mood.set` | `name=<mood> [secs=<1-86400>]` | `ok`, `err unsupported` (a mood this firmware does not have), `err bad_args` |
| `pdc.set` | `state=on\|off [count=<0-2147483647>]` | `ok` (the PDC Manager lease, see *PDC Manager (ZDMW)*) |
| `pdc.result` | `id=<1-65535> result=<ok\|auth\|denied\|offline\|tls\|timeout\|unknown\|cooldown\|busy\|unavailable> [count=<n>] [teams=1\|0]` | `ok` (how the click `evt pdc.click id=` ended) |
| `log.since` | `[boot=<16 hex>] seq=<0-4294967295>` | the recorded events after `seq` again (`replay=1`), then `ok boot=<16 hex> last=<seq> lost=<n>`; `err busy` while a replay is under way (see *Recorded events*) |
| `clock.face` | `name=<face>` | `ok` (also when it already is that face), `err unsupported` (a face this firmware does not have), `err unavailable` (no Clock Mode), `err bad_args` (see *Clock faces*) |

**Capabilities:** `hello` lists what the firmware supports (`poke,monster,mode,time,status,msg,mood,act,pdc,track,did,log,face`), which message styles it draws (`styles=marquee`), which moods it has (`moods=idle,relaxed,peukie,angry,sleepy,working,happy`), which activities `act` starts (`acts=water,sneeze,stare,meditate,focus,ask_peukie,kitt`) and which Clock Mode faces it has (`faces=classic,skeleton`), each in display order. The companion only enables the matching buttons, so new commands, styles, moods and activities can be added without breaking older companions or firmware. Without `pdc` the companion sends no `pdc.*` command at all. `track`: `act iid=`, `evt act`, `status act=` and `evt mood src=`; `did`: `evt did` (MY DAY); `log`: `dev=`/`boot=`, `seq=`/`t=`/`up=` and `log.since` (*Recorded events*); `face`: `clock.face`, `faces=`, `status face=`, `evt face`.

**Clock faces (`clock.face`, cap `face`).** Clock Mode shows one of the faces in `faces=`: `classic` (the desk clock) and `skeleton` (Mechanical Skeleton: a steampunk movement around a timeplate). The face is chosen here or on the touchscreen (the *Clock faces* page: a long press on Buddy's head in the menu's header); either way `evt face` reports it, and `status face=` says which it is. It works in any mode: in Clock Mode the face changes at once (the screen is redrawn, as when the clock appears), otherwise the next time Clock Mode starts. The device keeps the face in RAM only: after a restart it is `classic` again, so a companion that wants to keep a face sends it again after `evt boot` (or a `hello` with a new `boot=`), never on an ordinary reconnect, which would undo a choice made on the touchscreen meanwhile. Names are lower-case letters and `_`, at most 15.

**Activities (`act`)** are short shows on top of Buddy's mood, the same ones the touchscreen menu starts: `water` (a glass of water short-circuits him), `sneeze` (his head flies off), `stare` (a staring contest, the camera close on his face), `meditate` (he floats until a notification drops him), `focus` (he builds a plan in his head until someone asks for "1 sec": POOF; older firmware answers `err unsupported`). Each plays to its end (2.7 to 9 s); meanwhile `poke`, `monster` and `act` get `err busy`, and the mood resumes afterwards. Names are lower-case letters and `_`, at most 15; `monster` and `poke` keep their own commands.

**KITT** (`kitt`, the eighth button on the touchscreen's Moods page; older firmware answers `err unsupported`): Buddy turns into a Knight Rider scanner (about 2 s) and stays one, with no end of its own, until a tap on him (once he has finished turning) or any mood choice (`mood.set`, the Moods page; also the mood he already has) turns him back (about 1.8 s); then his mood carries on: the one he had, or the one just chosen. Meanwhile he is busy: `poke`, `monster` and `act` get `err busy`. It reports like any activity: one `evt act … state=shown`, then exactly one `state=done result=done` when he has turned back, or `result=interrupted` when Clock Mode cut it (he comes back from the clock as himself). Nothing else is sent while he sweeps.

**The peukie question** (`ask_peukie`, *Peukie?* in the touchscreen menu): `act name=ask_peukie` asks exactly the question the menu asks, through the same code; it is answered on the touchscreen only (NEE / JA). While Buddy asks (up to a minute) and while he is sad after a *no* or no answer, he is busy as during an activity. A *yes* starts a smoke break: `evt mood mood=peukie src=break`, and two minutes later `evt mood … src=break` with the mood he had before.

**Interactions (`act iid=`, `evt act`, cap `track`).** Every activity started by `act` or on the touchscreen is reported, so the companion can tell what really happened:
- `iid`: the companion's id (it uses 1–2147483647). Without one, and for touches, the device picks one with the top bit set (2147483648 and up), so the two never collide. The same `iid` again gets `ok dup=1 state=…` and starts nothing.
- `evt act iid=<n> name=<activity> state=shown src=<cmd|touch>`: sent once a frame showing it has been presented: for the question, the question itself is on screen. A command's `ok` only means it started.
- `evt act iid=<n> state=done result=<r>`: exactly once. `yes` / `no`: JA / NEE; `timeout`: the minute ran out (on screen the same as *no*, but a different fact); `done`: a show played to its end; `interrupted`: cut off (Clock Mode, a restart of his mood): never an answer. Started but never shown and then cut off is `interrupted` without a `shown` before it.
- `status … act=<iid>:<started|shown|done>[:<result>]`: the last interaction, so a companion that missed an event (a reconnect) learns how it ended; `act=none` after a restart of the device. Missed events themselves come back with `log.since` (cap `log`).

**Why the mood changed (`evt mood … src=`, cap `track`):** `cmd` (`mood.set`), `touch` (the Moods page), `break` (a smoke break starting or ending), `timer` (a timed mood ran out).

**MY DAY (`evt did`, cap `did`):** the touchscreen page where you tell Buddy what you are doing: `evt did item=<id>` per tap (plus `seq=` and the time, see *Recorded events*), `id` one of `myday.work`, `myday.meeting`, `myday.coffee`, `myday.water`, `myday.smoke`, `myday.stretch`, `myday.lunch` (after `myday.`: the companion's activity types). Every tap is a new event (a new `seq`); a replayed one has the same `seq`. Sent for touches only; Buddy's shows (Activities, Food & Drink), moods and the question's answer never send it. (P4 firmware sent `n=<counter>` instead of `seq=`.)

**Recorded events (cap `log`).** The events the companion keeps as history, `evt act` and `evt did`, survive a companion that was away (closed, the cable unplugged while the board stays powered, a reconnect):
- **Identity:** `(dev, boot, seq)`. `dev` (in `hello` and `evt boot`) is stable: the board's factory MAC (`441bf68d1178`), `sim-<12 hex>` for the simulator on a machine. `boot` is a random 64-bit number chosen at every start of the firmware (16 hex digits). `seq` counts up from 1 within a boot, one per recorded event. Together they name an event uniquely and for good; neither `boot` nor `seq` alone does.
- **Time:** each recorded event ends in ` seq=<n> t=<UTC ms>` once the clock has been set (`time.set`; the board keeps time across companion absences, not across restarts), or ` seq=<n> up=<ms since start>` before that. An `up=` event has no date: the device never derives one, and a companion must keep it as "time unknown, n ms after the start of boot X" rather than guess.
- **Kept:** the last 64 recorded events, in RAM (PSRAM on the board). Nothing is written to flash, so a restart or power cut loses what was not yet fetched; a new `boot` shows that it happened.
- **Replay:** `log.since boot=<b> seq=<n>` sends the kept events with seq > n, exactly the lines that went out (same seq, same time) with ` replay=1` added, then `ok boot=<current> last=<seq> lost=<k>`. The reply comes after the last replayed line, so a companion that has the reply has the lot. `lost`: events after n that are no longer kept (overflow): their number is known, their content is not. For another `boot` (or none), the replay starts at the beginning of the current boot (`lost` = events of this boot no longer kept); events of the earlier boot that were never fetched are gone, and how many is unknown. The device sends two replayed lines per frame, so the screen keeps its pace (64 events: about half a second).
- **Duplicates:** an event can arrive twice (live and replayed, or two replays); the companion stores by `(dev, boot, seq)`, so the second copy changes nothing. Live events go out as before during a replay.
- **Gaps:** a jump in the live `seq` means events were missed (a line lost): ask `log.since` for the ones after the last one stored.

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
| `evt boot proto=1 fw=<version> dev=<device id> boot=<16 hex>` | the firmware started (the companion re-sends the time; `dev`, `boot`: cap `log`) |
| `evt mode mode=<normal\|office\|monster\|game>` | the mode changed, by touch or by command (`game`: a Tic-Tac-Toe round is being played; Clock Mode pauses it (`office`, then `game` again when it carries on), Back or two minutes without a touch end it) |
| `evt mood mood=<mood> src=<cmd\|touch\|break\|timer>` | the mood changed: by `mood.set`, on the touchscreen menu, a smoke break began or ended, or a timed mood ran out (`src`: firmware with `track`) |
| `evt msg id=<id> state=<showing\|done>` | a message went up on the sign / finished |
| `evt face name=<face> src=<cmd\|touch>` | the Clock Mode face changed: by `clock.face` or on the touchscreen (cap `face`) |
| `evt pdc.click id=<1-65535>` | the user confirmed *JA* for Ze Doen Maar Wat on the touchscreen (sent once per click; see below) |
| `evt act iid=<n> name=<activity> state=shown src=<cmd\|touch> seq=<n> t=<ms>\|up=<ms>` | an activity (a show or the question) is on screen (see *Interactions*; `seq`, `t`, `up`: *Recorded events*) |
| `evt act iid=<n> state=done result=<yes\|no\|timeout\|done\|interrupted> seq=<n> t=<ms>\|up=<ms>` | how it ended, exactly once (a replay repeats it with `replay=1`) |
| `evt did item=<myday.…> seq=<n> t=<ms>\|up=<ms>` | a MY DAY tap: what you are doing (see *MY DAY*) |

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

1. Firmware: add it to `link_cmd_t` and `COMMANDS` in `components/link`, decode its arguments in `decode_args()`, run it in `run_command()` (`components/app/src/app.c`), add its capability to `APP_LINK_CAPS` in `app_link.h`, and add host tests (`firmware/tests/host/test_link.c`).
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
