"""Summarize the last active stream in a PS4 debug.log, excluding older sessions."""
import argparse
import json
import re
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("log",type=Path)
parser.add_argument("--output",type=Path)
args=parser.parse_args()
text=args.log.read_text(encoding="utf-8",errors="replace")
marker="[I] connection: streaming active"
text=text[text.rfind(marker):] if marker in text else text
perf=[dict(zip(("bitrate","fps","decodes","drops","seconds"),map(float,m))) for m in
      re.findall(r"perf bitrate=(\d+) fps=([\d.]+) decodes/s=([\d.]+) drops=(\d+) interval=([\d.]+)s",text)]
timing=[list(map(float,m)) for m in re.findall(r"decode=([\d.]+)ms convert=([\d.]+)ms \(bounce=([\d.]+) bgra=([\d.]+)\) present=([\d.]+)ms",text)]
pipe=[list(map(float,m)) for m in re.findall(r"pipeline submit=([\d.]+)ms flush=([\d.]+)ms finish=([\d.]+)ms assembly=([\d.]+)ms queue=([\d.]+)ms frame_gaps=(\d+)",text)]
presentation=[list(map(float,m)) for m in re.findall(r"present_queue samples=(\d+) waits=(\d+) wait_avg=([\d.]+)ms wait_max=([\d.]+)ms no_slot=(\d+) submit_fail=(\d+)",text)]
display=[list(map(float,m)) for m in re.findall(r"present_timing shown_samples=(\d+) observed_show_avg=([\d.]+)ms observed_show_max=([\d.]+)ms pending_max=(\d+) events=(\d+) timeouts=(\d+) event_errors=(\d+) status_errors=(\d+)",text)]
duration=sum(p["seconds"] for p in perf)
def weighted(key):return sum(p[key]*p["seconds"] for p in perf)/duration if duration else 0
summary={"intervals":len(perf),"duration_seconds_approx":round(duration,2),
         "fps_weighted":round(weighted("fps"),3),"decode_rate_weighted":round(weighted("decodes"),3),
         "fps_min":min((p["fps"] for p in perf),default=0),"fps_max":max((p["fps"] for p in perf),default=0),
         "local_drops":int(sum(p["drops"] for p in perf)),
         "source_frame_gaps":int(sum(p[5] for p in pipe))}
for index,name in enumerate(("decode","convert","bounce","bgra","present_submit")):
    values=[row[index] for row in timing]
    summary[name+"_ms_mean"]=round(sum(values)/len(values),3) if values else 0
    summary[name+"_ms_max"]=max(values,default=0)
for index,name in enumerate(("callback","flush","finish","assembly","queue")):
    values=[row[index] for row in pipe]
    summary[name+"_ms_mean"]=round(sum(values)/len(values),3) if values else 0
    summary[name+"_ms_max"]=max(values,default=0)
result={"summary":summary,"performance_intervals":perf,"timing_intervals":timing,"pipeline_intervals":pipe}
if presentation:
    waits=sum(row[1] for row in presentation)
    summary.update(present_queue_samples=int(sum(row[0] for row in presentation)),
                   present_slot_waits=int(waits),
                   present_wait_ms_mean=round(sum(row[1]*row[2] for row in presentation)/waits,3) if waits else 0,
                   present_wait_ms_max=max(row[3] for row in presentation),
                   present_no_slot_drops=int(sum(row[4] for row in presentation)),
                   present_submit_failures=int(sum(row[5] for row in presentation)))
    result["presentation_intervals"]=presentation
if display:
    shown=sum(row[0] for row in display)
    summary.update(observed_show_samples=int(shown),
                   observed_show_ms_mean=round(sum(row[0]*row[1] for row in display)/shown,3) if shown else 0,
                   observed_show_ms_max=max(row[2] for row in display),
                   flip_pending_max=int(max(row[3] for row in display)),
                   flip_wait_events=int(sum(row[4] for row in display)),
                   flip_wait_timeouts=int(sum(row[5] for row in display)),
                   flip_event_errors=int(sum(row[6] for row in display)),
                   flip_status_errors=int(sum(row[7] for row in display)))
    result["display_intervals"]=display
if args.output:args.output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
print(json.dumps(summary,indent=2))
