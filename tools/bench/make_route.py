#!/usr/bin/env python3
# tools/bench/make_route.py — writes route.json, the benchmark's scripted run (--smoke-script): settle, look round,
# sprint-walk, then a fast fly-over (teleports along +x high over the land, so new land streams in all the way).
import json, sys
steps = [{"wait": 600, "do": "anchor"}]
for i in range(24):   # look round: 24 mouse turns, 15 frames apart
    steps.append({"wait": 15, "do": "move", "dx": 120, "dy": (-20 if i % 6 < 3 else 20)})
steps.append({"wait": 10, "do": "look", "yaw": 90, "pitch": 0})
steps.append({"wait": 2, "do": "key", "key": "Left Shift", "hold": 900})
steps.append({"wait": 0, "do": "key", "key": "W", "hold": 900})
steps.append({"wait": 900, "do": "cmd", "text": "/gamemode spectator"})
for i in range(1, 41):   # fly-over: 6 blocks every 30 frames, 40 up, turning the view a little
    steps.append({"wait": 30, "do": "tp", "rel": True, "x": i * 6, "y": 40, "z": i * 2})
    if i % 5 == 0:
        steps.append({"wait": 0, "do": "move", "dx": 200 if i % 10 else -200, "dy": 0})
steps.append({"wait": 120, "do": "quit"})
json.dump({"steps": steps}, open(sys.argv[1] if len(sys.argv) > 1 else "route.json", "w"), indent=1)
