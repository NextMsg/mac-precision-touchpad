"""Summarize locally captured MT2 WPP contact traces; no driver changes."""
import collections
import datetime
import json
import re
import sys
from pathlib import Path

lines = Path(sys.argv[1]).read_text(errors="replace").splitlines()
frames, contacts = [], []
for line in lines:
    output_match = re.search(r"MT2 output (.*)", line)
    if output_match and frames:
        frames[-1].setdefault("output", []).append({
            k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", output_match[1])})
    match = re.search(r"MT2 contact (.*)", line)
    if match:
        contacts.append({k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", match[1])})
    match = re.search(r"MT2 frame count=(\d+) active=(\d+) rejected=(\d+) read=(.*)", line)
    if match:
        time = re.search(r"::(\d+/\d+/\d+-\d+:\d+:\d+\.\d+)", line)[1]
        frames.append(dict(time=time, count=int(match[1]), active=int(match[2]),
                           rejected=int(match[3]), read=match[4].strip(), contacts=contacts))
        contacts = []

all_contacts = [c for f in frames for c in f["contacts"]]
two = [f for f in frames if sum(c["tip"] for c in f["contacts"]) == 2]
bad = [f for f in two if any(c["tip"] and not c["confidence"] for c in f["contacts"])]
mismatch = [c for c in all_contacts if bool(c["tip"]) != (c["state"] == 128)]
delta = []
for a, b in zip(frames, frames[1:]):
    if sum(c["tip"] for c in a["contacts"]) == 2 and sum(c["tip"] for c in b["contacts"]) == 2:
        ta, tb = [datetime.datetime.strptime(f["time"], "%m/%d/%Y-%H:%M:%S.%f") for f in (a, b)]
        delta.append(round((tb-ta).total_seconds()*1000, 3))
summary = dict(frames=len(frames), contacts=len(all_contacts),
    reported_counts=dict(collections.Counter(f["count"] for f in frames)),
    read_status=dict(collections.Counter(f["read"] for f in frames)),
    raw_states=dict(collections.Counter(c["state"] for c in all_contacts)),
    two_down_frames=len(two), two_down_with_rejected=len(bad),
    area_vs_state_mismatches=len(mismatch), mismatch_examples=mismatch[:8],
    two_down_intervals_ms=dict(collections.Counter(delta)),
    max_minor=max((c["minor"] for c in all_contacts), default=None))
if any("output" in f for f in frames):
    summary["output_down_on_non_touch"] = sum(
        c["tip"] and not any(raw["id"] == c["id"] and raw["state"] == 128 for raw in f["contacts"])
        for f in frames for c in f.get("output", []))
    summary["output_two_down_frames"] = sum(sum(c["tip"] for c in f.get("output", [])) == 2 for f in frames)
print(json.dumps(summary, indent=2))
Path(sys.argv[1]).with_suffix(".json").write_text(json.dumps(frames, indent=2))
