# ShroudMap — Fab Store Description

---

## Title

ShroudMap - Fog of War with a Memory

## Short description (Fab "Description" field)

Fog of war that remembers. Three states, not two: visible, explored and unknown - so the ground your
team has walked stays drawn and the enemy standing on it does not. Two hundred revealers cost one
texture upload per team, and Blueprint asks IsVisible in O(1) with no render-target readback.

---

## Long description

### The problem

Ask a fog-of-war system "can this unit be seen", and most of them answer honestly for the units and
say nothing at all about the ground.

That gap is the whole feature. A strategy game is not made of "who can see whom right now" - it is
made of **what the team has learned**. The hill you walked over at minute two is still on your map at
minute twenty. The enemy who walked onto it since is not. Take that middle state away and you no
longer have fog of war; you have a flashlight.

The other half of the problem is arithmetic. Fifty units, twenty towers and a scout plane is seventy
things that reveal, updating several times a second, over a map that is a megabyte. Do it once per
revealer and it is a hundred and forty draw calls a second before anything has actually happened.

### Three states, not two

| | What it means | What is drawn |
|---|---|---|
| **Unknown** | never seen by this team | nothing - fully shrouded |
| **Explored** | seen before, nobody looking now | terrain stays, **movers do not** |
| **Visible** | a revealer is looking right now | everything |

One Blueprint node - `Apply Shroud Visibility To Actor` - hides a unit standing in remembered ground
while the ground under it stays drawn. Turn `bHideInExplored` off and buildings, wrecks and resource
nodes stay on the map where you last saw them. That is the model paying off in one node.

### One upload per team. Two hundred revealers or two.

Every revealer's footprint is worked out **once** and cached, then stamped into a per-team byte mask.
The whole team's map goes to the GPU as a single region update covering only the rectangle that
actually changed.

The number is on the built-in stats box, and it does not move when you add revealers. `Shroud.Test
200` puts two hundred orbiting revealers on the map in one console line; **Texture uploads** still
reads one per team. That is the claim, and it is checkable in about four seconds.

A revealer that stands still costs a stamp of cells that were already computed. Only movement costs a
rebuild, and rebuilds run under a hard per-update budget with a round-robin cursor: over budget, a
revealer stamps the shape it had last update. The fog edge lags under load. **Nothing blinks out**,
and nothing is starved.

### Blueprint asks in O(1). No readback, no stall.

The subsystem keeps a CPU-side mask at the same resolution as the texture, so `IsVisible`,
`IsExplored` and `GetVisibilityState` are a clamp, an index and a byte comparison - on the game
thread, this frame, with no render-target readback anywhere.

And because the mask **is** what gets packed into the texture, the answer a Blueprint gets and the
pixel on the screen are the same byte. There is no CPU approximation shadowing a GPU truth, which is
the usual place these systems quietly disagree with themselves.

### Terrain occlusion that is a ray count, not a trace count

Optional, and off by default - most top-down games want a plain disc and should not pay for anything
else.

Switched on, sight spreads by **radial horizon sampling** over a coarse height field: a ray leaves the
eye, steps outward, and keeps the steepest slope it has met; ground that does not clear it stays dark.
One pass per ray - not a line trace per cell.

The height field itself is gathered by downward traces **spread over updates**, so switching occlusion
on mid-game never hitches, and the stats box tells you how far along it is. Ray count scales with the
circumference, so a big circle gets the rays it needs at the rim and a small one is not paying for
overlap.

`EyeHeight` is why a watchtower clears a ridge a soldier does not - it falls straight out of comparing
both against the same field, with no special case anywhere. Buildings and rocks block sight through a
**Shroud Occluder** component, which does not add a code path at all: it raises the same height field.

### Numbers you can read, not claims you have to trust

`AShroudHUD` draws revealers, occluders, visible cells, explored cells, **texture uploads**, kilobytes
uploaded, and milliseconds broken down into footprints, composite and upload - plus resolution, cell
size, team, memory state and occlusion state.

On `UCanvas`, so it draws in a cooked **Shipping** build where DrawDebug is compiled out and a debug
widget would be stripped. It is a real overlay, not a development-only one. Deliberately no buttons on
it: anything that has to be clicked belongs in a widget, and this box has to work on machines that
have no mouse at all.

### Runtime, all of it

Resolution, world bounds, update rate, team, memory, terrain occlusion - every one of them moves at
runtime from Blueprint or from the console, not just in a config file. Changing the resolution
**resamples the memory across** rather than wiping it, so switching from 256 to 1024 sharpens the map
instead of erasing five minutes of scouting.

Eight console commands: `Shroud.Test`, `Shroud.Reveal`, `Shroud.Clear`, `Shroud.Stats`,
`Shroud.Resolution`, `Shroud.Memory`, `Shroud.Occlusion`, `Shroud.Team`.

### Materials

The texture is linear, bilinear, clamped: **R is current sight, G is memory**. Drop the shipped
`MF_ShroudSample` into your landscape or postprocess material, or sample it yourself with
`WorldToShroudUV`. A ready-made postprocess material is included for a running start.

Point the settings at a material parameter collection and the plugin publishes the map bounds and the
three state colours into it, so a landscape material follows a runtime bounds change with no Blueprint
pushing values around.

---

## ShroudMap and TurretMind — not alternatives

If you already own **TurretMind**, or are weighing the two: they answer different questions and do not
overlap.

- **TurretMind** decides **what a turret aims at** - a targeting service with a spatial index, a
  re-evaluation budget and lead prediction.
- **ShroudMap** decides **what the player sees** - a per-team map of visible, remembered and unknown
  ground.

Different data, different problem, no shared code. A game can use both, and they never talk to each
other. Please do not buy one instead of the other expecting it to cover the same ground.

---

## What ShroudMap does NOT do

Being clear about this up front saves everybody a refund:

- **No minimap.** ShroudMap hands you the texture; drawing a minimap is a separate product with its
  own competition, and pretending otherwise would be selling you half of one.
- **No AI perception.** This is what the *player* sees. Whether an NPC noticed you has different rules
  and belongs in a perception system.
- **No network replication of the fog texture.** A per-team megabyte is not something to push down a
  wire. Replicate the revealers - they are already just transforms - and every client computes the
  same map.
- **No units, no terrain tools, no terrain generation.** It works with what you already have.
- **Terrain occlusion reads a height field, not the collision world.** One height per sample, so a
  bridge you can see under or an archway will read as solid. That is the trade that buys radial
  sampling instead of a trace per cell, and it is stated here rather than discovered later.

---

## Technical details

**Features**

- Three-state fog: unknown, explored, visible - with the explored state as a first-class citizen
- One texture upload per team per update, whatever the revealer count
- O(1) `IsVisible` / `IsExplored` / `GetVisibilityState` from a CPU mask - no readback, no stall
- The mask and the texture are the same bytes, so queries and picture cannot drift apart
- Cached revealer footprints with a hard per-update rebuild budget and a round-robin cursor
- Optional terrain occlusion by radial horizon sampling over a coarse height field
- Height field gathered in slices across updates - switching occlusion on never hitches
- Occluder components that block sight by raising the same height field
- Per-team maps, up to eight teams, with a runtime view-team switch
- Runtime resolution change that resamples memory instead of wiping it
- Revealers that light without mapping, for radar sweeps and searchlights
- Canvas stats overlay that works in a cooked Shipping build
- Material function and ready-made postprocess material; optional parameter collection publishing
- Full Blueprint API, project settings with runtime setters, eight console commands

**Code Modules**

- `ShroudMap` (Runtime)

**Number of Blueprints:** demo content only (the plugin itself needs none)
**Number of C++ Classes:** 6 (subsystem, revealer component, occluder component, statics library,
settings, HUD)
**Network Replicated:** No
**Supported Development Platforms:** Win64 (built and verified). Mac and Linux are not in the `.uplugin`'s `PlatformAllowList` and were not built for this release.
**Supported Target Build Platforms:** Win64 (built and verified). Mac and Linux are not in the `.uplugin`'s `PlatformAllowList` and were not built for this release.

**Built and verified on Win64 with Unreal Engine 5.8. Mac and Linux are not in the .uplugin's PlatformAllowList and have not been built or tested.**

**Documentation:** included as `Docs/DOCUMENTATION.md`, covering the update passes, the height field,
the material contract and every project setting in full - including a section on how to check each
performance claim yourself.

---

## Tags

fog of war, fow, rts, strategy, real time strategy, moba, vision, line of sight, visibility, explored,
minimap data, reveal, scouting, stealth, team vision, render target, performance, optimization,
blueprint, gameplay
