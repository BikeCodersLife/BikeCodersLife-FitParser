---
title: Json Output
---

# Json Output

Documentation coming soon.

## gearEvents

Optional top-level array, since v2.7.0, time ordered, omitted when the file has
no gear change events (FIT event 42 `front_gear_change`, 43 `rear_gear_change`,
written by Di2 / AXS drivetrains). It sits between `coordinates` and `summary`.

```json
"gearEvents": [
  {"timestamp": "2025-06-15T06:31:05Z", "front": 50, "rear": 17, "frontNum": 2, "rearNum": 5}
]
```

| Key | Meaning |
|-----|---------|
| `timestamp` | ISO 8601 UTC time of the gear change (always present) |
| `front` | Chainring teeth, absent when not reported |
| `rear` | Sprocket teeth, absent when not reported |
| `frontNum` | Front gear number, 1 = innermost, absent when not reported |
| `rearNum` | Rear gear number, 1 = innermost, absent when not reported |
