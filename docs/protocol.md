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
- Single spaces between tokens. Names: lower-case letters, digits, `.` and `_`. Values: letters, digits and `. _ : + -`. At most 8 arguments; lines at most 255 bytes.
- Lines end with `\n` (the device sends `\r\n`).

**Safety:** only a complete, well-formed command line with a matching checksum does anything. Plain text (typing in a serial monitor), the device's own output echoed back, esptool traffic and corrupted lines are ignored or rejected. During flashing the firmware is not even running. The companion does not retry actions after a timeout, so a lost reply cannot trigger an action twice.

## Commands

| Command | Arguments | Reply |
|---|---|---|
| `hello` | | `ok proto=1 fw=<version> caps=<list>` |
| `ping` | | `ok` |
| `status` | | `ok mode=<normal\|office\|monster> time=<set\|unset> uptime=<s>` |
| `poke` | | `ok`, or `err busy mode=<mode>` (Office Mode, Monster running, screen not up yet) |
| `monster` | | `ok`, or `err busy mode=<mode>` |
| `mode.set` | `mode=office\|normal` | `ok` (switches like the moon button / a tap), or `err unavailable` |
| `time.set` | `utc=<s> [ms=<0-999>] off=<min> [next=<utc s> next_off=<min>]` | `ok` |

**Capabilities:** `hello` lists what the firmware supports (`poke,monster,mode,time,status`). The companion only enables the matching buttons, so new commands can be added without breaking older companions or firmware.

**Unknown arguments are ignored**, so newer companions can send more. Unknown commands get `err unknown_cmd`; missing or invalid arguments get `err bad_args`.

**Time:** `utc` is the current time; `off` is the local offset from UTC in minutes; `next`/`next_off` announce the next offset change (daylight saving) so the clock switches correctly even while the companion is disconnected. The board has no RTC, so after power loss the clock is unset until the companion sends the time; it shows *CLOCK NOT SET* meanwhile.

## Events

| Event | When |
|---|---|
| `evt boot proto=1 fw=<version>` | the firmware started (the companion re-sends the time) |
| `evt mode mode=<normal\|office\|monster>` | the mode changed, by touch or by command |

## Errors

`bad_crc`, `bad_version`, `bad_syntax`, `unknown_cmd`, `bad_args`, `busy`, `unavailable`.

## Example

```
>DB1 7 monster #3F7C
<DB1 7 ok #3425
<DB1 0 evt mode mode=monster #04EC
>DB1 8 monster #D45F
<DB1 8 err busy mode=monster #...
```

## Adding a command

1. Firmware: add it to `link_cmd_t` and `COMMANDS` in `components/link`, decode its arguments in `decode_args()`, run it in `run_command()` (`main.c`), add its capability to `CAPS` in `serial_link.c`, and add host tests (`firmware/tests/host/test_link.c`).
2. Companion: add a button (`MainForm.cs`) that sends it when the capability is present.
3. Document it here.
