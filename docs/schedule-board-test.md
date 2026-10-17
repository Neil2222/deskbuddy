# P6 live scheduling: physical board test (pending)

The P6 board test that still has to run. Everything before it is done: P6 is committed on `feature/schedule`
(`729a8f9`, fixes `e28113c`), the Companion is **published** from it (`companion\publish`, 16 Oct 2026 00:27), the
board runs `29f5c80` (not flashed, not restarted), and Live is **off** in the real `schedule.json` (the demo schedule,
untouched).

Takes about 25 minutes and needs the user at the board twice (JA, then NEE). Scripts: `tools/schedule-board-test/`.

## Before

1. Back up `%APPDATA%\DeskBuddy`: `powershell -File tools\schedule-board-test\ui.ps1 -Backup` (writes
   `Documents\DeskBuddy-backups\appdata-<time>`; the last one before this test: `appdata-20261016-002754`).
2. The published Companion runs and shows `Connected · COM6`; no simulator runs (its history is separate anyway).
3. Pick S at least 3 minutes ahead (a whole minute, with S−3 and S+31 on the same day) and tell the user the times.

## Status (17 Oct 2026): done, except two skips on the board

Shown on the board (Companion `7904918`, firmware `29f5c80`), by the user's eyes and the files:
- Live starts off; switching it on applied the scheduled mood (relaxed); a scheduled transition (working) reached the
  board; a manual Happy stayed over a boundary with the same effective mood.
- Scheduled questions appear on the touchscreen: JA → `yes` with exactly one linked smoke activity, NEE → `no`;
  unanswered → `timeout` (not NEE, no activity).
- Clock Mode is never interrupted: the scheduled sneeze got `busy` six times (every 10 s), then `skipped:busy:office`.
- A Companion restart repeated nothing; 32 events, 32 keys; 10 activities, 10 ids; every occurrence decided once.

Not shown on the board (the run was stopped when the script could not leave Clock Mode): an action skipped while
paused or while the Companion is disconnected. Both are covered by unit tests and were shown end to end in the
simulator (`skipped:paused`, `skipped:offline`, never run afterwards). The script's "Buddy" mode tile click did not
work (use a touch on the clock instead); leaving Clock Mode by touch works.

## Quick run (the open part, about 7 minutes)

`python tools\schedule-board-test\quick.py HH:MM` (S). Same preparation and restoration as below; blocks via
`add_test_blocks.py HH:MM quick`. The user is needed from S−0:20 to about S+3:30 only:

| Time | Step | Expected | User |
|---|---|---|---|
| S−1 | Live on | | |
| S−0:20 | Clock tile | zoom into the clock | watch the zoom |
| S | `Test: sneeze in Clock Mode` | busy every 10 s, `skipped:busy:office` at S+1; the clock is never interrupted | watch |
| S+1:05 | Buddy mode tile | zoom out, wake up | watch the zoom |
| S+2 | `Test: question JA` | the question | **tap JA** |
| S+3 | `Test: question NEE` | the question | **tap NEE** |
| S+3:40 … S+4:10 | Pause for 1 hour, Resume; `Test: sneeze while paused` at S+4 | `skipped:paused`, never run | — |
| S+4:20 … S+6:10 | Disconnect, Connect; `Test: water while away` at S+5 | `skipped:offline`, never run after reconnecting | — |

Afterwards from the files: a request only for the two questions (ok) and the sneeze (busy, then the skip); the JA
question `Yes` with exactly one linked smoke activity, the NEE question `No`; no `evt act` for the paused sneeze or the
water.

## Run

`python tools\schedule-board-test\run.py HH:MM` (S). It closes the Companion, adds the test blocks below to today in the
existing schedule (`add_test_blocks.py`: blocks named `Test: …`, the demo blocks stay, Live stays off), restarts it,
then follows the timeline. Screenshots of the Companion go to `%TEMP%\board-*.png` (`off`, `live`, `work`,
`override`, `clock`, `paused`, `end`; the Device page shows the latest log lines).

| Time | Block / step | Expected | User |
|---|---|---|---|
| start | Companion restarted | Schedule header *Off*, status "Off: asks Buddy for nothing" | |
| S−1 | Live switched on (`Test: relaxed now`, S−3 … S+3) | Buddy becomes **relaxed** (enabling applies the current mood) | watch |
| S+3 | `Test: work`, S+3 … S+30, working | one `mood.set working` | watch |
| S+5 | `Test: question JA`, ask_peukie | "PEUKIE ROKEN?" on the touchscreen | **tap JA** |
| S+8 | `Test: question NEE`, ask_peukie | the question again | **tap NEE** |
| S+10 | Happy tile in the Companion (a manual mood) | Buddy happy | watch |
| S+11 … S+12 | `Test: same mood` (working, as below it) | the effective mood does not change: Buddy **stays happy** | watch |
| S+13 | Clock tile (Clock Mode) | the clock | |
| S+14 | `Test: sneeze in Clock Mode` | busy every 10 s, then `skipped:busy:office` at S+15; the clock is **never interrupted** | watch |
| S+15:40 | Buddy mode tile | back to Buddy | |
| S+16 … S+18:30 | Disconnect, then Connect; `Test: water while away` at S+17 | `skipped:offline`; nothing happens after reconnecting | watch |
| S+19:30 … S+21:30 | Pause for 1 hour, then Resume; `Test: sneeze while paused` at S+20 | `skipped:paused`; nothing after Resume; Resume applies working | watch |
| S+22:30 | Companion closed and started | reconnects; nothing repeated | |
| after | | the user checks Clock zoom (in and out), a few animations, touchscreen navigation | **look** |

## Check afterwards

- `companion-events.jsonl`: one `request` per question and the sneeze (with `occurrence`, `block`, `planned`,
  `planned_utc`, `origin: scheduled`), `request_result` ok / busy, `skip` for busy:office, offline, paused; no request or
  skip twice for an occurrence, nothing new after the restart.
- Derived (as in the P5 / P6 checks, `HistoryLedger` on the real files): the JA question `Yes`, origin `Scheduled`,
  exactly one `smoke` activity linked to it; the NEE question `No`, no activity; no activity from moods or the sneeze.
- `device-events.jsonl`: the questions' `evt act` shown / done, no duplicate keys.

## After

1. Close the Companion; copy `schedule.json` and `schedule.json.bak` back from the backup (Live off, demo schedule as
   it was); compare `schedule.json`, `schedule.json.bak` and `settings.json` with the backup byte for byte.
2. Keep the history (`device-events.jsonl`, `companion-events.jsonl`, checkpoints): the test events are real records;
   remove them only if the user asks.
3. Start the published Companion again.
