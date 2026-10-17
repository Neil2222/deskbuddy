# Daily use: laptop setup, data, backup and restore

How to run DeskBuddy every day from a work laptop: installing the Companion, what lives in `%APPDATA%\DeskBuddy`,
moving it between PCs, backing it up and restoring it. Building firmware is not needed for any of this.

## One PC drives the board

The Companion talks to the board over the UART cable, and Windows lets one program open a COM port at a time. Run
the Companion on the PC the board is plugged into, and only there. Each PC keeps its own copy of everything in
`%APPDATA%\DeskBuddy`; nothing is synchronised. The board itself keeps no settings: its mood, Clock face and clock are
lost at every power-up, and the Companion restores them when it connects. It also keeps its last 64 recorded events
(questions, shows, MY DAY taps) in RAM until a Companion collects them, so the History survives a closed Companion
as long as the board keeps power.

## Setting up the laptop

```powershell
git clone https://github.com/neil2222/deskbuddy.git     # or, in an existing checkout: git switch master; git pull
cd deskbuddy
tools\setup-dotnet.ps1                                   # once: .NET 10 SDK for this user (no administrator rights)
tools\companion.ps1 -Publish                             # companion\publish\DeskBuddy.Companion.exe
```

- **Check the version:** the exe's *Properties → Details → Product version* is `1.0.0+<commit>`; it should start with
  the commit of `git rev-parse HEAD`. The board reports its firmware in *Device → Connection* (`57eb475` today:
  `master`'s firmware source is identical, only that string differs).
- **Start it:** double-click the exe, or pin it to the taskbar. Close the Companion on any other PC first.
- **Republishing later:** quit the Companion first (in tray mode: *Quit DeskBuddy* in the icon's menu), then
  `tools\companion.ps1 -Publish` again. Publishing to the same folder keeps *Start with Windows* valid.

## Closing, tray and Start with Windows

Both are optional and off until you switch them on in *Device → Windows*.

| Setting | Closing the window (the X) | At sign-in |
|---|---|---|
| Both off (default) | Quits the Companion: no connection, no schedule until you start it again | Nothing: start it yourself |
| Tray mode on | Hides the window; Buddy's icon stays in the notification area and everything keeps running. A notice says so the first time | — |
| Start with Windows on | — | Windows starts the published exe minimized: in the notification area with tray mode, else on the taskbar |

- **Quitting in tray mode:** right-click Buddy's icon → *Quit DeskBuddy*. A left click on the icon opens the window.
  Its tooltip shows the connection. Signing out or shutting down always quits.
- **Only one Companion:** starting it again while it runs brings the running window up (also from the notification
  area). Started by Windows while one already runs, it just leaves.
- **Start with Windows** writes `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\DeskBuddy Companion`, with this
  exe's path. The Companion checks that entry at every start and whenever its window is activated, and says so in
  amber (and in *Diagnostics*) when it starts **another copy** (the exe was moved or published elsewhere), points to a
  **file that no longer exists**, or was **switched off in Task Manager** (*Startup apps*). *Use this copy* fixes all
  three; unticking removes the entry. Only the published exe can be registered (the development build needs the
  per-user .NET that `tools\companion.ps1` sets up).

## What is in `%APPDATA%\DeskBuddy`

| File | What | Copy to another PC? |
|---|---|---|
| `schedule.json`, `schedule.json.bak` | The week (blocks, *Live*, a pause) and its previous version | Yes, if you want the same week |
| `settings.json` | PDC on/off and user name, the last Clock face, the mood the schedule last set, tray mode | Yes (tray mode comes along; *Start with Windows* does not: it is in the registry) |
| `device-events.jsonl` | The board's recorded events: the History (append-only) | Yes, to keep one History (see below) |
| `device-checkpoints.json` | How far each board boot has been collected | Only together with `device-events.jsonl` from the same PC |
| `companion-events.jsonl` | What the Companion asked the board to do (requests, scheduled actions, skips) | Together with `device-events.jsonl` |
| `history-annotations.jsonl` | Entries hidden as test data (only after hiding something) | Together with the History |
| `installation.json` | This PC's own id | **No**: each PC keeps its own |
| `pdc-session.bin` | The PDC Manager login, encrypted for this Windows user on this PC | **No**: it cannot be read elsewhere; log in again |
| `*.sim.*` | The same for the desktop simulator | Not needed |
| `schedule.json.damaged-*` | A schedule file that could not be read, set aside | No |

Development and simulator tests use another folder: set `DESKBUDDY_DATA_DIR` before starting the Companion (it says
so in *Diagnostics*). Never point it at this folder.

## Back up

Quit the Companion first (so nothing is half-written), then:

```powershell
$src = Join-Path $env:APPDATA 'DeskBuddy'
$dst = Join-Path $env:USERPROFILE ('Documents\DeskBuddy-backups\appdata-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $dst | Out-Null
Copy-Item (Join-Path $src '*') $dst
Get-ChildItem $dst -File | Get-FileHash -Algorithm SHA256 |
    ForEach-Object { '{0}  {1}' -f $_.Hash.ToLower(), (Split-Path $_.Path -Leaf) } | Set-Content (Join-Path $dst 'SHA256SUMS.txt')
```

Back up before every test that touches the real folder, before moving data, and before changing the schedule.
Never delete old backups or recorded history without a reason.

## Restore

1. Quit the Companion.
2. Keep the current folder: rename `%APPDATA%\DeskBuddy` to `DeskBuddy.before-restore-<date>` (never delete it).
3. Copy the backup folder's files back to `%APPDATA%\DeskBuddy` (not `SHA256SUMS.txt`). Check them against
   `SHA256SUMS.txt` with `Get-FileHash` if in doubt.
4. Restoring another PC's backup: put this PC's own `installation.json` (and `pdc-session.bin`, if any) back from
   the renamed folder.
5. Start the Companion and check *Schedule* (Off or Live as expected), *History* and *PDC Manager*.

## Moving to the laptop

1. Quit the Companion on both PCs and back up both folders (above).
2. Copy what you want from the desktop's folder to the laptop's `%APPDATA%\DeskBuddy`, following the table:
   - the week: `schedule.json` (and `.bak`);
   - one History: `device-events.jsonl`, `device-checkpoints.json`, `companion-events.jsonl` and, if present,
     `history-annotations.jsonl`, all four together, into a laptop folder that has no History of its own yet;
   - never `installation.json` or `pdc-session.bin`.
3. Plug the board into the laptop, start the Companion there, and check: *Connected*, the clock synced, the
   schedule's state (a copied schedule keeps its *Live* or *Off*), the History.
4. Do not start the Companion on the desktop again while the board is on the laptop.

**Merging two Histories** (both PCs already recorded the same board): there is no tool, and it is untested. The
History de-duplicates by `(dev, boot, seq)`, so appending one PC's `device-events.jsonl` and `companion-events.jsonl`
lines to the other's should not double anything; then delete `device-checkpoints.json` (it is rebuilt). Back up
first. Keeping each PC's History separate is the safe choice.

## A week with the schedule

1. **Back up** (above), with the schedule still *Off*.
2. **Build the week** on the *Schedule* page with *Live* off: blocks with a mood, optionally an action. Saving
   writes `schedule.json` version 2 and keeps the previous one as `.bak`. Start with moods only; add actions once the
   moods behave.
3. **Turn it Live** with the board connected (the page's switch, or the sidebar pill → *Turn Live on*). The current
   scheduled mood is sent at once.
4. **What happens:** a scheduled mood is sent only when the scheduled mood changes; a gap between blocks keeps the
   mood. A mood you pick on the touchscreen or a tile wins until the next scheduled change. An action runs once per
   block per day, within its grace period (3 min for the question, 1 min for shows); if it cannot (Buddy busy for too
   long, the board offline, the Companion closed, the laptop asleep, a pause) it is skipped with the reason, never
   late or twice. *Pause* (1 hour, the rest of today, days away, until resumed) and *Turn Off* are in the sidebar
   pill.
5. **After sleep, a reconnect or a Companion restart:** if Buddy still shows the mood the schedule itself set, he gets
   the mood that applies now (a change missed meanwhile is applied when the Companion is back). A mood you chose
   yourself stays. One exception: a choice made on the touchscreen while the Companion was closed or disconnected is
   only recognised if it differs from the schedule's last mood.
6. **Every day:** the Companion must be running for the schedule to work (tray mode and *Start with Windows* help).
   The *History* page shows what happened: questions and answers, shows, MY DAY taps, skipped actions with their
   reason; restarts under *Technical*.

Buddy does not learn from the History yet: nothing adapts the schedule or his behaviour. It only records.

## Firmware

Not needed for daily use. Full flash backups and rollback: `tools\flash-backup.ps1 -Action backup|verify|restore
-Image <file> -Port COM6` (the images and their checksums are listed in [HANDOVER.md](../HANDOVER.md)). Flashing stops
the Companion's use of the port automatically only through `tools\idf.ps1`; other serial tools need *Disconnect*
first, and some (pyserial with its defaults) reset the board, which loses events not yet collected.
