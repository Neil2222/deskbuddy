# The short P6 board test (docs/schedule-board-test.md): quick.py HH:MM, with HH:MM the start S (at least 2 minutes
# ahead). About 7 minutes; the user is needed from S-0:20 to about S+3:30 (Clock Mode, then JA and NEE). What must NOT
# happen (an action while paused or away) is proven from the journal and the board's history afterwards.
import datetime, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ['TEMP']
def ps(script, *args):
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', os.path.join(HERE, script), *args],
                       capture_output=True, text=True, encoding='utf-8', errors='replace')
    return (r.stdout + r.stderr).strip()
def log(msg): print(f'{datetime.datetime.now():%H:%M:%S} {msg}', flush=True)
def ui(*a):
    out = ps('ui.ps1', *a)
    if out.startswith(('missing', 'no main window', 'no window')):
        log(f'ABORTED: {out}. Nothing more is done; restore schedule.json from the backup.')
        sys.exit(1)
    return out
def shot(name): return ps('shot.ps1', os.path.join(OUT, f'board-{name}.png'))
def status(): return 'status: ' + ui('-Texts', '^Off: |^Now |^Paused')

S = datetime.datetime.combine(datetime.date.today(), datetime.datetime.strptime(sys.argv[1], '%H:%M').time())
def at(minutes, seconds=0):
    wait = (S + datetime.timedelta(minutes=minutes, seconds=seconds) - datetime.datetime.now()).total_seconds()
    if wait > 0:
        time.sleep(wait)

log(ui('-Close'))
log(subprocess.run([sys.executable, os.path.join(HERE, 'add_test_blocks.py'), sys.argv[1], 'quick'], capture_output=True, text=True).stdout.strip())
log(ui('-Start'))
time.sleep(6)
log(ui('-Texts', 'COM6'))
log(ui('-Invoke', 'Schedule')); time.sleep(1); log(status()); log(shot('off'))

at(-1, 0);   log(ui('-Invoke', 'Live schedule on or off')); time.sleep(2); log(status())
at(-1, 40);  log(ui('-Invoke', 'Buddy')); time.sleep(1); log(ui('-Invoke', 'Clock'))          # Clock Mode
at(1, 5);    log(ui('-Invoke', 'Buddy', '-Nth', '2'))                                         # back to Buddy
at(2, 0);    log('question 1: JA')
at(3, 0);    log('question 2: NEE')
at(3, 40);   log(ui('-Invoke', 'Schedule')); time.sleep(1)
log(ui('-Invoke', 'Pause or resume the schedule')); time.sleep(1); log(ps('keys.ps1', 'down,enter')); time.sleep(2)
log(status()); log(shot('paused'))
at(4, 10);   log(ui('-Invoke', 'Pause or resume the schedule')); time.sleep(2); log(status())   # Resume
at(4, 20);   log(ui('-Invoke', 'Device')); time.sleep(1); log(ui('-Invoke', 'Disconnect'))
at(6, 10);   log(ui('-Invoke', 'Connect')); time.sleep(8); log(ui('-Texts', 'COM6'))
at(7, 0);    log(ui('-Invoke', 'Device')); time.sleep(1); log(shot('end')); log('done')
