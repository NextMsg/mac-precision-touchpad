"""Inspect MT2 diagnostics without assuming that the user's finger was stationary."""
import collections
import csv
import datetime as dt
import json
import math
import sys
from pathlib import Path

folder = Path(sys.argv[1])
frames = json.loads((folder / 'contacts.json').read_text())
cursor = list(csv.DictReader((folder / 'cursor.csv').open(encoding='utf-8-sig')))
previous = {}
up_changes, transition_moves, blocks = [], [], collections.defaultdict(list)
for f in frames:
    stamp = dt.datetime.strptime(f['time'], '%m/%d/%Y-%H:%M:%S.%f')
    eligible = [c for c in f['contacts'] if c['tip'] and c['confidence']]
    if len(eligible) == 1:
        c = eligible[0]
        blocks[stamp.strftime('%H:%M:%S')].append(c)
    current = {}
    for c in f['contacts']:
        p = previous.get(c['id'])
        if p and p['tip'] and p['confidence'] and c['confidence']:
            distance = math.hypot(c['x'] - p['x'], c['y'] - p['y'])
            record = dict(time=f['time'], id=c['id'], state=c['state'],
                          dx=c['x']-p['x'], dy=c['y']-p['y'], units=round(distance,2))
            if not c['tip'] and distance:
                up_changes.append(record)
            if c['tip'] and c['state'] != 128 and distance:
                transition_moves.append(record)
        current[c['id']] = c
    previous = current

windows = []
for sec, cs in blocks.items():
    if len(cs) < 40 or len({c['id'] for c in cs}) != 1:
        continue
    screen = [r for r in cursor if r['Time'][11:19] == sec]
    row = dict(second=sec, frames=len(cs), id=cs[0]['id'],
               raw_span_x=max(c['x'] for c in cs)-min(c['x'] for c in cs),
               raw_span_y=max(c['y'] for c in cs)-min(c['y'] for c in cs),
               transition_states=sum(c['state'] != 128 for c in cs))
    if screen:
        row.update(cursor_span_x=max(int(r['X']) for r in screen)-min(int(r['X']) for r in screen),
                   cursor_span_y=max(int(r['Y']) for r in screen)-min(int(r['Y']) for r in screen))
    windows.append(row)
summary = dict(frames=len(frames), cursor_samples=len(cursor),
               changed_position_on_up=len(up_changes),
               largest_up_changes=sorted(up_changes,key=lambda r:r['units'],reverse=True)[:8],
               reported_motion_outside_confirmed_touch=len(transition_moves),
               largest_transition_moves=sorted(transition_moves,key=lambda r:r['units'],reverse=True)[:8],
               one_eligible_contact_windows=windows)
(folder/'jitter-summary.json').write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
