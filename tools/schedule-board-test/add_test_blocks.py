# Adds the board test's blocks (named "Test: …") to today in the real schedule.json; the other blocks stay, Live stays
# as it is. add_test_blocks.py HH:MM [full|quick] (S; see docs/schedule-board-test.md).
import datetime, json, os, sys, uuid

s = datetime.datetime.strptime(sys.argv[1], '%H:%M')
profile = sys.argv[2] if len(sys.argv) > 2 else 'full'
today = datetime.datetime.now()
s = today.replace(hour=s.hour, minute=s.minute, second=0, microsecond=0)
assert 3 <= s.hour * 60 + s.minute and s.hour * 60 + s.minute + 31 <= 24 * 60
path = os.path.join(os.environ['APPDATA'], 'DeskBuddy', 'schedule.json')
doc = json.load(open(path, encoding='utf-8'))
day = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun'][s.weekday()]

def hm(d):
    t = s + datetime.timedelta(minutes=d)
    return f'{t.hour:02d}:{t.minute:02d}'

def block(name, start, end, mood=None, action=None):
    return {'id': str(uuid.uuid4()), 'name': 'Test: ' + name, 'start': hm(start), 'end': hm(end), 'mood': mood,
            'plans': None, 'action': action, 'reminder': False, 'enabled': True}

tests = [
    block('relaxed now', -3, 3, mood='relaxed'),
    block('work', 3, 30, mood='working'),
    block('question JA', 5, 6, action='ask_peukie'),
    block('question NEE', 8, 9, action='ask_peukie'),
    block('same mood', 11, 12, mood='working'),
    block('sneeze in Clock Mode', 14, 15, action='sneeze'),
    block('water while away', 17, 18, action='water'),
    block('sneeze while paused', 20, 21, action='sneeze'),
] if profile == 'full' else [
    block('sneeze in Clock Mode', 0, 1, action='sneeze'),
    block('question JA', 2, 3, action='ask_peukie'),
    block('question NEE', 3, 4, action='ask_peukie'),
    block('sneeze while paused', 4, 5, action='sneeze'),
    block('water while away', 5, 6, action='water'),
]
doc['days'][day] = [b for b in doc['days'][day] if not b.get('name', '').startswith('Test: ')] + tests
json.dump(doc, open(path, 'w', encoding='utf-8'), indent=2)
print('S =', s.strftime('%H:%M'), day, '| live in file:', doc.get('live'), '| demo blocks kept:',
      len(doc['days'][day]) - len(tests))
for b in tests:
    print(f"  {b['start']}-{b['end']} {b['name']} mood={b['mood']} action={b['action']}")
