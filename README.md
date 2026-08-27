# ShroudMap — Fog of War with a Memory

Fog of war for Unreal Engine 5.8 that keeps **three** states instead of two:

| State | What it means | What is drawn |
|---|---|---|
| **Unknown** | never seen by this team | nothing — fully shrouded |
| **Explored** | seen before, nobody is looking now | terrain stays, **units do not** |
| **Visible** | a revealer is looking at it this update | everything |

That middle state is the reason this exists. A line-of-sight system answers *"can this observer see
that spot right now"*. A strategy game needs the map to keep what the team has learned — the hill you
walked over at minute two is still drawn at minute twenty, and the enemy that walked onto it since is
not.

## What it costs

- **One texture upload per team per update.** Two revealers or two hundred, the number on the stats
  box does not move. Nothing here scales a draw call with the number of things that can see.
- **O(1) Blueprint queries.** `IsVisible`, `IsExplored` and `GetVisibilityState` read a CPU-side mask
  at the texture's own resolution. No render-target readback, no stall on the game thread — and the
  byte a query reads is the byte the material samples, so the answer and the picture cannot disagree.
- **Cached revealer footprints.** A revealer that stands still costs a stamp of cells that were
  already worked out. Only moving costs a rebuild, and rebuilds run under a hard per-update budget.
- **Optional terrain occlusion** as radial horizon sampling over a coarse height field gathered once,
  spread over updates. One pass per ray, not one trace per cell — and the cost is on the stats box.

## Quick start

1. Enable the plugin. Set **Project Settings → Plugins → ShroudMap → World Origin / World Size** to
   cover your level (the map is a square; size it generously).
2. Add a **Shroud Revealer** component to anything that gives sight. Set `SightRadius` and `Team`.
3. Sample the fog: put `MF_ShroudSample` into your landscape or postprocess material, or read
   `GetShroudTexture` and set it on your own material instance.
4. Hide units in remembered ground with `Apply Shroud Visibility To Actor`.

That is the whole setup. Everything else — memory on/off, terrain occlusion, resolution, team —
moves at runtime from Blueprint or from the console.

## Classes

| Class | What it is for |
|---|---|
| `UShroudRevealerComponent` | anything that gives sight. Radius, eye height, team, soft edge, on/off. Does not tick. |
| `UShroudOccluderComponent` | optional. A building or rock that stops sight, by raising the height field. |
| `UShroudSubsystem` | the world subsystem: masks, textures, height field, budget, stats. |
| `UShroudStatics` | the whole plugin from Blueprint. |
| `UShroudSettings` | project defaults, under Project Settings → Plugins → ShroudMap. |
| `AShroudHUD` | the counters box on `UCanvas`, so it survives a Shipping build. |

## Console

```
Shroud.Test [Count] [RingRadius] [SightRadius]   put orbiting revealers on the map (0 removes them)
Shroud.Reveal [Radius|all] [Team]                reveal a circle at the viewpoint, or everything
Shroud.Clear [Team]                              forget what a team has seen
Shroud.Stats                                     print the measured counters
Shroud.Resolution [N]                            print or set the map resolution
Shroud.Memory [0|1]                              print or set whether the map remembers
Shroud.Occlusion [0|1]                           print or set terrain occlusion
Shroud.Team [N]                                  print or set the local view team
```

`Shroud.Test 200` is the claim, checkable in one line: two hundred revealers on screen, and
**Texture uploads** on the stats box still reads one per team.

## What it is not

No minimap. No AI perception. No network replication of the fog texture. No units, no terrain tools.
ShroudMap decides **what the player sees**; what a turret aims at is a different question and a
different plugin (see TurretMind).

Full documentation, free and without an account: <https://github.com/SimulatedFlow/documentation>

The same manual ships with the plugin as [`Docs/DOCUMENTATION.md`](Docs/DOCUMENTATION.md).
---

© 2026 Silvan Teufel. All rights reserved.

<!-- SF-STORE-BLOCK:BEGIN -->
## 🛒 Source-available — see before you buy

This repository contains the **full source** of a commercial Unreal Engine plugin. It is **source-available, not open source**: read it, evaluate it, then buy a license to use it. See **the Fab Content License Agreement / Unreal Engine EULA (purchase required)**.

**Get it / Buy:**
- Fab store — all our UE5 plugins: https://www.fab.com/sellers/Silvan%20Teufel

_This plugin does not have its own Fab listing yet — the store link above is where everything we currently sell lives._

### 📬 **Free UE5 Snippet-Pack**

10 ready-to-use C++/Blueprint building blocks (subsystems, versioned saves, async nodes, editor tooling) — MIT licensed. Get it by joining the newsletter — plus a heads-up when something new ships. Double opt-in, unsubscribe in one click, no address sharing.

👉 **[Get the free pack](https://silvan.teufel-engineering.com/newsletter/plugins/?q=gh)**

_© 2026 Silvan Teufel. All rights reserved._
<!-- SF-STORE-BLOCK:END -->
