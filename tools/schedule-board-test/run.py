# The P6 board test (docs/schedule-board-test.md): run.py HH:MM, with HH:MM the start S (at least 2 minutes ahead).
# Drives the published Companion through UI Automation on a fixed timeline relative to S; screenshots go to %TEMP%.
import datetime, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ['TEMP']
def ps(script, *args):
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', os.path.join(HERE, script), *args],
                       capture_output=True, text=True, encoding='utf-8', errors='replace')
    return (r.stdout + r.stderr).strip()
def ui(*a):
    out = ps('ui.ps1', *a)
    if out.startswith(('missing', 'no main window', 'no window')):
        # Never carry on blindly (later steps send keys): stop, so the user can restore (docs/schedule-board-test.md, After).
        log(f'ABORTED: {out}. Nothing more is done; restore schedule.json from the backup.')
        sys.exit(1)
    return out
def shot(name): return ps('shot.ps1', os.path.join(OUT, f'board-{name}.png'))
def log(msg): print(f'{datetime.datetime.now():%H:%M:%S} {msg}', flush=True)

S = datetime.datetime.combine(datetime.date.today(), datetime.datetime.strptime(sys.argv[1], '%H:%M').time())
def at(minutes, seconds=0):
    wait = (S + datetime.timedelta(minutes=minutes, seconds=seconds) - datetime.datetime.now()).total_seconds()
    if wait > 0:
        time.sleep(wait)
def device_page(name): log(ui('-Invoke', 'Device')); time.sleep(1); log(shot(name))

log(ui('-Close'))
log(subprocess.run([sys.executable, os.path.join(HERE, 'add_test_blocks.py'), sys.argv[1]], capture_output=True, text=True).stdout.strip())
log(ui('-Start'))
time.sleep(6)
log(ui('-Texts', 'Connected'))
log(ui('-Invoke', 'Schedule')); time.sleep(1); log('status: ' + ui('-Texts', '^Off: |^Now |^Paused'))
log(shot('off'))

at(-1);      log(ui('-Invoke', 'Live schedule on or off')); time.sleep(3); log('status: ' + ui('-Texts', '^Off: |^Now |^Paused')); device_page('live')
at(3, 20);   device_page('work')
at(5, 30);   log('question 1 (JA) on the board')
at(8, 30);   log('question 2 (NEE) on the board')
at(10, 0);   log(ui('-Invoke', 'Buddy')); time.sleep(1); log(ui('-Invoke', 'Happy'))
at(12, 20);  device_page('override')
at(13, 0);   log(ui('-Invoke', 'Buddy')); time.sleep(1); log(ui('-Invoke', 'Clock'))
at(15, 20);  device_page('clock')
at(15, 40);  log(ui('-Invoke', 'Buddy')); time.sleep(1); log(ui('-Invoke', 'Buddy', '-Nth', '2'))
at(16, 0);   log(ui('-Invoke', 'Device')); log(ui('-Invoke', 'Disconnect'))
at(18, 30);  log(ui('-Invoke', 'Connect'))
at(19, 30);  log(ui('-Invoke', 'Schedule')); time.sleep(1); log(ui('-Invoke', 'Pause or resume the schedule')); time.sleep(1); log(ps('keys.ps1', 'down,enter'))
time.sleep(2); log('status: ' + ui('-Texts', '^Off: |^Now |^Paused')); log(shot('paused'))
at(21, 30);  log(ui('-Invoke', 'Pause or resume the schedule')); time.sleep(2); log('status: ' + ui('-Texts', '^Off: |^Now |^Paused'))
at(22, 30);  log(ui('-Close')); log(ui('-Start')); time.sleep(8); log(ui('-Texts', 'Connected'))
at(24, 0);   device_page('end'); log('done')
