# ShroudMap — Documentation

**Fog of war with a memory, for Unreal Engine 5.8.**

One runtime module · `LoadingPhase: PreDefault` · Win64 built and verified; Mac and Linux
allow-listed but not built · No third-party code, no other plugin dependencies.

---

## Table of contents

1. [What this is](#1-what-this-is)
2. [Engine and platform support](#2-engine-and-platform-support)
3. [Installation](#3-installation)
4. [Quick start (5 minutes)](#4-quick-start-5-minutes)
5. [Code examples](#5-code-examples)
6. [How it is computed](#6-how-it-is-computed)
7. [Class reference](#7-class-reference)
8. [Project settings](#8-project-settings)
9. [Materials](#9-materials)
10. [Console commands](#10-console-commands)
11. [Demo content](#11-demo-content)
12. [Checking the claims](#12-checking-the-claims)
13. [Notes and limits](#13-notes-and-limits)
14. [ShroudMap and TurretMind](#14-shroudmap-and-turretmind)
15. [Troubleshooting](#15-troubleshooting)

---

## 1. What this is

ShroudMap keeps, per team, a picture of what that team can see and what it has ever seen. It is a
**map**, not a visibility test: it answers questions about places, not about pairs of actors.

Three states, and the middle one is the product:

| `EShroudVisibility` | Meaning | Typical use |
|---|---|---|
| `Unknown` | never seen by this team | fully shrouded — no terrain, no props, no units |
| `Explored` | seen before, nobody looking now | terrain drawn dimmed, **movers hidden** |
| `Visible` | a revealer is lighting it this update | everything drawn |

A line-of-sight product tells you whether observer A can see actor B right now. It has nothing to
say about a hill nobody is currently looking at — and "the terrain you remember, without the enemy
that walked onto it" is exactly what a strategy game is made of.

### What it does not do

- **No minimap.** Drawing the map is a separate product with its own competition. ShroudMap gives you
  the texture; how you present it is yours.
- **No AI perception.** This is what the *player* sees. Whether an NPC noticed you is a different
  question with different rules (partial credit, memory decay, hearing) and a different plugin.
- **No replication of the fog texture.** A per-team megabyte is not something to push down a wire.
  Replicate the revealers — they are already just transforms — and every client computes the same map.
- **No units, no terrain tools.** Nothing here spawns or moves anything except the `Shroud.Test`
  revealers, which exist so the performance claim can be checked in one console line.

---

## 2. Engine and platform support

| | |
|---|---|
| **Engine version** | Unreal Engine **5.8** (`"EngineVersion": "5.8.0"`) |
| **Module** | `ShroudMap`, `Type: Runtime`, `LoadingPhase: PreDefault` |
| **Platform allow-list** | `Win64`, `Mac`, `Linux` |
| **Built and verified on** | **Win64** — Development Editor and a packaged plugin build |
| **Mac / Linux** | Allow-listed in the `.uplugin`, **not built and not tested for this release** |
| **Project type** | C++ **and** Blueprint-only projects (the plugin ships precompiled binaries; a Blueprint-only project needs no compiler to use it) |
| **Engine dependencies** | `Core`, `CoreUObject`, `Engine`, `DeveloperSettings` (public); `RenderCore`, `RHI` (private) |
| **Marketplace dependencies** | none |
| **Third-party code** | none |
| **Network replication** | none — see §13 |
| **Editor module** | none; every line here ships in a cooked build |

There is no reason ShroudMap should not build on Mac or Linux — it uses no platform-specific API and
no shader of its own — but "should" is not "did", so this is stated as it is rather than claimed.

---

## 3. Installation

### 3.1 From Fab (Epic Games Launcher)

1. In the Epic Games Launcher, open **Library → Fab Library**, find **ShroudMap** and click
   **Install to Engine**, choosing your 5.8 installation.
2. Open your project. **Edit → Plugins**, search for `ShroudMap`, tick **Enabled**.
3. Restart the editor when prompted.

### 3.2 As a project plugin (source)

1. Copy the `ShroudMap` folder into `<YourProject>/Plugins/ShroudMap`, so that
   `<YourProject>/Plugins/ShroudMap/ShroudMap.uplugin` exists.
2. If your project is C++: right-click the `.uproject` → **Generate Visual Studio project files**,
   then build. If your project is Blueprint-only, the shipped binaries are used as they are.
3. Launch the project, then **Edit → Plugins → ShroudMap → Enabled**, and restart.

### 3.3 Verifying the install

Open the console (`` ` ``) in Play-in-Editor and type:

```
Shroud.Stats
```

If the plugin is live you get a block of counters in the log. If you get *"no ShroudMap subsystem in
this world"*, the world has no shroud yet — see §15.

### 3.4 Using it from C++

Add the module to your own `Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine",
    "ShroudMap",          // <- this
});
```

Then include what you need — the public headers are
`ShroudTypes.h`, `ShroudRevealerComponent.h`, `ShroudOccluderComponent.h`, `ShroudSubsystem.h`,
`ShroudStatics.h`, `ShroudSettings.h`, `ShroudHUD.h`.

---

## 4. Quick start (5 minutes)

### Step 1 — cover your level

**Project Settings → Plugins → ShroudMap → Map**:

- **World Origin** — the XY centre of your playable area, in world cm.
- **World Size** — the edge length of the square that gets a map, in cm. The map is always square;
  **size it generously**. Anything outside the square answers `Unknown` and the texture clamps to its
  rim pixel, which reads on screen as a smear.
- **Resolution** — 512 is the default. Over a 1 km map that is just under 2 m per cell.
- **Num Teams** — 2 by default.

### Step 2 — give something sight

Add a **Shroud Revealer** component to any actor that should reveal — a unit, a watchtower, a
vehicle, a camera, a flare on the ground.

Set `Sight Radius` (cm) and `Team`. That is the whole setup: the component **does not tick**, does not
trace, and does not get a draw call of its own.

### Step 3 — draw the fog

Pick one:

- **Postprocess** — drop `M_ShroudPostProcess` (in `/ShroudMap/ShroudMap/Materials/`) into a Post
  Process Volume's **Post Process Materials** array, and set its texture parameter from
  `Get Shroud Texture`. Fastest way to see something.
- **Landscape / ground material** — put the `MF_ShroudSample` material function into your existing
  material and multiply your base colour by its tint output.
- **Your own** — read `Get Shroud Texture` (R = current sight, G = memory) and sample it with
  `World To Shroud UV`.

### Step 4 — hide units in remembered ground

On each unit that should disappear when nobody is looking, on Tick or on a timer:

**`Apply Shroud Visibility To Actor`** (Target = self, `Team` = -1, `bHideInExplored` = true).

Turn `bHideInExplored` **off** for anything that does not move — a building, a wreck, a resource node.
It then stays drawn on remembered ground, which is what a player expects.

### Step 5 — see the numbers

Set your level's HUD class to `AShroudHUD` (or a Blueprint child of it) in the Game Mode. You get a
counters box on `UCanvas`: revealers, visible cells, explored cells, **texture uploads**, kilobytes
uploaded, and milliseconds split into footprints / composite / upload.

That is the whole setup. Everything else — memory on and off, terrain occlusion, resolution, view
team — moves at runtime from Blueprint or the console.

---

## 5. Code examples

Every Blueprint node below is also a C++ call, and vice versa. The Blueprint library is
`UShroudStatics`; the same functions live on `UShroudSubsystem` without the world-context argument.

### 5.1 Put a revealer on an actor (C++)

```cpp
#include "ShroudRevealerComponent.h"

AScoutUnit::AScoutUnit()
{
    Revealer = CreateDefaultSubobject<UShroudRevealerComponent>(TEXT("Revealer"));
    Revealer->SetupAttachment(RootComponent);

    Revealer->SightRadius = 2400.0f;   // cm, on the XY plane
    Revealer->EyeHeight   = 180.0f;    // only terrain occlusion reads this
    Revealer->Team        = 0;
    Revealer->SoftEdge    = -1.0f;     // negative = take the project default
}
```

The component registers itself with the world subsystem on `OnRegister` and unregisters on
`OnUnregister`. There is nothing to call, nothing to tick and nothing to clean up.

### 5.2 Change a revealer at runtime

```cpp
// Blind a unit that got hit by a flashbang - it stays registered and costs nothing.
Revealer->SetRevealEnabled(false);

// A scout deploying its sensor mast.
Revealer->SetSightRadius(5000.0f);

// A captured unit changing sides. The old team keeps what it had already remembered.
Revealer->SetTeam(1);

// A wounded scout: drawn, but below VisibilityThreshold, so queries still call it unexplored.
Revealer->SetStrength(0.25f);
```

All four mark the cached footprint stale for you. `MarkFootprintDirty()` is only for changes the
component cannot see for itself — a piece of level geometry moving, say.

### 5.3 A radar sweep that lights without mapping

```cpp
Revealer->bLeavesMemory = false;   // lights the current update only
```

A searchlight, a radar pulse, a flash of lightning: it tells the player where the enemy is without
mapping the ground for them. The composite pass stamps these *after* folding sight into memory, which
is the entire cost of the feature.

### 5.4 Query a position (C++)

```cpp
#include "ShroudSubsystem.h"

if (const UShroudSubsystem* Shroud = UShroudSubsystem::Get(this))
{
    const EShroudVisibility State = Shroud->GetVisibilityState(TargetLocation, /*Team=*/0);

    switch (State)
    {
    case EShroudVisibility::Visible:  /* draw it, shoot at it */            break;
    case EShroudVisibility::Explored: /* draw the ground, not the unit */   break;
    case EShroudVisibility::Unknown:  /* draw nothing */                    break;
    }
}
```

`Get()` returns null in a world without a shroud, so a system written against ShroudMap still runs in
a test map where the plugin was never set up. The `UShroudStatics` versions go one better and answer
`Unknown` instead of needing the null check at all.

This is O(1) — a clamp, an index and two byte comparisons against a CPU-side mask. **No render target
readback, no game-thread stall.**

### 5.5 Hide a unit the player should not see

```cpp
#include "ShroudStatics.h"

void AEnemyUnit::UpdateShroudVisibility()
{
    // true = hidden in remembered ground; false = stays drawn there (buildings, wrecks, nodes)
    UShroudStatics::ApplyShroudVisibilityToActor(this, this, /*Team=*/-1, /*bHideInExplored=*/true);
}
```

`Team = -1` always means *"the team the local player sees the world as"*, which is what a
single-player game and a hotseat game both want with no branching.

Call it on a timer rather than every frame — the map only changes at `UpdatesPerSecond` (20 Hz by
default), so a 10 Hz timer is already finer than the data.

### 5.6 Scripted reveals

```cpp
// A scouting report hands the player a piece of the map they never walked.
UShroudStatics::RevealCircle(this, ObjectiveLocation, /*Radius=*/6000.0f, /*Team=*/0);

// A captured radar station: everything, for one team.
UShroudStatics::RevealAll(this, /*Team=*/0);

// New mission, same level: forget it all. Team < 0 clears every team.
UShroudStatics::ClearMemory(this, /*Team=*/-1);
```

`RevealCircle` writes into **memory** by default and leaves it there. Pass
`bAffectCurrentSight = true` to also light it as currently visible for this update.

### 5.7 Feed your own material

```cpp
if (UTexture* ShroudTex = UShroudStatics::GetShroudTexture(this))
{
    GroundMID->SetTextureParameterValue(TEXT("ShroudTexture"), ShroudTex);
}

FVector2D Origin; float WorldSize = 0.f; float CellSize = 0.f;
UShroudStatics::GetShroudBounds(this, Origin, WorldSize, CellSize);

GroundMID->SetVectorParameterValue(
    TEXT("ShroudBounds"),
    FLinearColor(Origin.X, Origin.Y, WorldSize, 1.0f / WorldSize));
```

A texture cannot travel in a material parameter collection, so the texture is always set on the
instance. The bounds and the three state colours *can* — see §9.

### 5.8 Switch the view team (hotseat, spectator, replay)

```cpp
UShroudStatics::SetTeam(this, 1);   // the whole screen now shows what team 1 knows
```

Every query defaulting to `Team = -1` follows immediately, and `GetShroudTexture` starts handing out
team 1's texture. Nothing is recomputed: each team's map was already being kept.

### 5.9 Terrain occlusion, switched on mid-game

```cpp
UShroudStatics::SetTerrainOcclusionEnabled(this, true);

// The height field is gathered in slices across updates, so this never hitches.
// Occlusion applies from the update the field is complete:
if (UShroudStatics::IsHeightFieldReady(this))
{
    // ridges are casting real shadows now
}

// Terrain changed under you - a crater, a raised bridge:
UShroudStatics::RebuildHeightField(this);
```

### 5.10 Read the counters yourself

```cpp
const FShroudStats Stats = UShroudStatics::GetShroudStats(this);

UE_LOG(LogTemp, Display,
    TEXT("%d revealers -> %d texture uploads, %.2f ms (%.2f footprints / %.2f composite / %.2f upload)"),
    Stats.ActiveRevealers, Stats.TextureUploads, Stats.UpdateMilliseconds,
    Stats.FootprintMilliseconds, Stats.CompositeMilliseconds, Stats.UploadMilliseconds);
```

`Stats.TextureUploads` is the number the whole design stands on: it is one per team that changed,
whether the world holds two revealers or two hundred.

### 5.11 An occluder (a wall that stops a scout but not a tower)

```cpp
#include "ShroudOccluderComponent.h"

Occluder = CreateDefaultSubobject<UShroudOccluderComponent>(TEXT("Occluder"));
Occluder->SetupAttachment(RootComponent);
Occluder->Shape       = EShroudOccluderShape::Box;
Occluder->BoxExtent   = FVector2D(1200.0f, 200.0f);
Occluder->BlockHeight = 400.0f;   // a one-storey wall
```

`BlockHeight` is a height, not a radius. A soldier with `EyeHeight = 180` is stopped by it; a
watchtower with `EyeHeight = 900` looks straight over. That falls out of comparing both against the
same height field — there is no special case for it anywhere in the code.

Occluders only have an effect while terrain occlusion is switched on.

### 5.12 Blueprint, in words

The typical Blueprint graph is three nodes and no variables:

| Where | Node | Arguments |
|---|---|---|
| Enemy pawn, on a 10 Hz timer | `Apply Shroud Visibility To Actor` | Target = self, Team = -1, Hide In Explored = true |
| UI button "MEMORY" | `Set Memory Enabled` | the inverse of `Is Memory Enabled` |
| UI button "TEAM" | `Set Team` | `(Get Team + 1) % Get Num Teams` |

The shipped demo panel (`WBP_ShroudDemoPanel`) is exactly this and nothing more — it is worth opening
as the shortest possible reference.

---

## 6. How it is computed

One update, four bounded passes. Everything is driven by rectangles in cell space, because a map is a
megabyte and a battle touches a corner of it.

### 6.1 Grid

The world is covered by a square, `WorldSize` cm on a side, centred on `WorldOrigin`, cut into
`Resolution × Resolution` cells. Per team there are three arrays at that resolution:

- `Vision` — one byte per cell, how brightly a revealer of this team is lighting it **now**.
  Cleared every update.
- `Memory` — one byte per cell, the brightest this team has **ever** seen it. `max(old, new)`.
  Never decays; only `ClearMemory` takes it away.
- `Pixels` — the BGRA staging buffer. `R = Vision`, `G = Memory`, `B = 0`, `A = 255`.

`Vision` and `Memory` are what `IsVisible` and `IsExplored` read, and they are the same bytes the
texture carries. That is deliberate: there is one number, not a CPU estimate that tracks a GPU truth.

### 6.2 Footprints

The set of cells one revealer lights is worked out once and cached, packed one cell per `uint32`
(cell index in the top 24 bits, brightness in the bottom 8).

A footprint is rebuilt when the revealer moved more than `RebuildMoveTolerance` cells, or its radius,
soft edge, strength, team or occlusion mode changed. A revealer standing still costs nothing beyond
stamping cells that were already computed.

Rebuilds run under a hard per-update budget with a round-robin cursor. **Over budget, a revealer
stamps the shape it had last update** — the fog edge lags, nothing blinks out, and the cursor
guarantees the one that missed its turn is served before the ones behind it.

Two budgets, because the two paths are two orders of magnitude apart:

| Setting | Applies when | Default |
|---|---|---|
| `MaxFootprintRebuildsPerUpdate` | occlusion off — a bounding-box walk per revealer | 64 |
| `MaxOccludedFootprintRebuildsPerUpdate` | occlusion on — hundreds of rays per revealer | 8 |

0 lifts either cap.

### 6.3 Compositing

1. Clear each team's `Vision` — only over **last update's** rectangle. Outside it the mask is known
   to be zero without looking.
2. Stamp the cached footprints of every revealer with `bLeavesMemory`.
3. Fold `Vision` into `Memory` over the changed rectangle, counting cells that cross the threshold.
4. Stamp the footprints of revealers **without** `bLeavesMemory` — a radar sweep, a searchlight, a
   flash of lightning. Going in after the fold is the whole of what "lights without mapping" costs.
5. Pack the changed rectangle to BGRA, count visible cells, and hand the region to the render thread.

### 6.4 The upload

**This is the number the design stands on.** One `UpdateTextureRegions` call per team per update,
covering only the rectangle that changed, whether the world holds two revealers or two hundred. A
revealer never gets a GPU write of its own.

The staging copy is tightly packed and owned by the render command, because the buffer it came from
is overwritten on the next update and the upload has not necessarily happened by then.

Before every region update the upload checks three things, and skips rather than clamps if any fails:
the texture's actual size against the current resolution, the rectangle against `[0, Resolution-1]`,
and the pixel buffer's length against `Resolution² × BytesPerPixel`. A runtime resolution change
rebuilds the grids *and* creates new textures, and a rectangle from the new grid must never reach the
old texture — clamping would paint half a picture, and the RHI's own bounds assert is fatal.

> **Deviation worth stating plainly.** The design brief called for the revealers to be drawn into a
> `UTextureRenderTarget2D` as instanced canvas quads in a single draw call. What ships instead is a
> CPU rasterisation into a `UTexture2D` pushed up as one region update. Two reasons, both in your
> favour: it is *one* GPU operation per team rather than a clear plus a batch, and — more
> importantly — it means the CPU mask a Blueprint queries and the pixels a material samples are
> **literally the same bytes**, computed once. A GPU draw path would need the mask computed a second
> time on the CPU anyway (that is what makes `IsVisible` O(1) with no readback), and two computations
> of the same thing drift. The public surface is unchanged: `GetShroudTexture` hands you a `UTexture*`
> with sight in R and memory in G.

### 6.5 Terrain occlusion

Off by default. A revealer lights a plain disc, which is what most top-down games want and costs
almost nothing.

On, sight is stopped by a **coarse height field** — `HeightFieldResolution²` downward line traces,
gathered in slices of `MaxHeightFieldTracesPerUpdate` per update so switching it on never hitches.
Occlusion applies from the update the field is complete; `IsHeightFieldReady` and the stats box both
say where it is.

Sight then spreads by **radial horizon sampling**. A ray leaves the eye, steps outward half a cell at
a time, and keeps the steepest slope it has met. Ground that does not clear that slope is behind
something and stays dark. That is one pass per ray — **not** a trace per cell.

- Ray count scales with the circumference (`RaysPerRimCell`, clamped by `MinRaysPerRevealer` and
  `MaxRaysPerRevealer`), so a big circle gets the rays it needs at the rim and a small one is not
  paying for overlap.
- `HorizonSlack` is tolerance in the test — ground has noise in it and the field is coarse, so
  without a little slack a revealer on a gentle rise shadows its own feet.
- `NearFieldCells` is lit whatever the field says. You always see your own feet.
- `EyeHeight` is why a watchtower clears a ridge a soldier does not. It falls straight out of
  comparing the two against the same field; there is no special case for it anywhere.

`UShroudOccluderComponent` does not add a code path. It **raises the height field**, so a wall and a
ridge stop a scout the same way and cost the same nothing per update. Moving an occluder is caught by
comparison each update and costs a restamp — a `memcpy` and a handful of samples, not a re-trace.

---

## 7. Class reference

### `UShroudRevealerComponent`

`USceneComponent`. Hang it on anything that gives sight. **It does not tick.**

| Property | Default | Notes |
|---|---|---|
| `SightRadius` | 1500 | cm, on the XY plane |
| `EyeHeight` | 180 | cm above the component; only terrain occlusion reads it |
| `Team` | 0 | which team's map this writes into |
| `SoftEdge` | -1 | fraction of the radius that fades; negative takes the project default |
| `Strength` | 1.0 | below `VisibilityThreshold` the cells are drawn but count as unexplored |
| `bRevealEnabled` | true | off costs nothing and keeps the registration |
| `bLeavesMemory` | true | off = lights the current update only (radar sweep, searchlight) |
| `bIgnoreTerrainOcclusion` | false | for anything that looks *down* on the map: aircraft, satellites |

| Function | Notes |
|---|---|
| `SetTeam` / `SetSightRadius` / `SetRevealEnabled` / `SetStrength` | mark the footprint stale for you |
| `MarkFootprintDirty` | only for changes the component cannot see |
| `GetEffectiveSoftEdge` | with the project default already resolved |
| `GetEyeLocation` | component location raised by `EyeHeight` |
| `IsRevealing` | true when it would contribute right now |

### `UShroudOccluderComponent`

`USceneComponent`, optional. `Shape` (`Circle` or axis-aligned `Box`), `Radius` / `BoxExtent`,
`BlockHeight`, `bOccluderEnabled`, plus `SetOccluderEnabled`, `SetBlockHeight`, `SetRadius`,
`MarkOccluderDirty`.

Rotation is ignored: the height field is axis-aligned. Only has any effect while terrain occlusion is
on. Does not tick.

### `UShroudSubsystem`

`UTickableWorldSubsystem`. Owns the per-team masks, the visibility textures, the coarse height field,
the budget and the counters.

`DoesSupportWorldType` covers **Game, PIE and Editor** — a plain editor world only gets one when
`bTickInEditorWorlds` is set, because the fog allocates and creates textures and that is not something
to do behind a level designer's back.

`UShroudSubsystem::Get(WorldContextObject)` is the static accessor; it returns null outside a
supported world. Everything on the subsystem is also on `UShroudStatics` — reach for the subsystem
directly from C++, and additionally for `GetWorldOrigin`, `GetWorldSize`, `GetCellSize`,
`WorldToCell`, `CellToWorld`, `SpawnTestRevealers` and `LogStats`.

### `UShroudStatics`

The whole plugin from Blueprint, `UBlueprintFunctionLibrary`. Every call is safe in a world with no
shroud: queries answer `Unknown`, setters do nothing, `ApplyShroudVisibilityToActor` leaves the actor
alone. That matters — a Blueprint written against ShroudMap still runs in a test map where the plugin
was never set up.

A `Team` argument **below zero always means "the local view team"**.

| Function | Notes |
|---|---|
| `IsVisible` / `IsExplored` / `GetVisibilityState` | O(1) against the CPU mask |
| `GetVisibilityValue` / `GetMemoryValue` | raw 0..1, the same byte the material samples |
| `GetActorVisibilityState` | the actor's location as a state |
| `ApplyShroudVisibilityToActor` | hide/show by state; `bHideInExplored` off keeps buildings drawn |
| `GetShroudTexture` | R = sight, G = memory |
| `WorldToShroudUV` / `GetShroudBounds` | for a material you drive yourself |
| `GetShroudStateColors` | the three colours from the project settings |
| `SetTeam` / `GetTeam` / `GetNumTeams` | the local view team |
| `RevealCircle` / `RevealAll` / `ClearMemory` | scripted reveals; Team < 0 does every team |
| `SetMemoryEnabled` / `IsMemoryEnabled` | the middle state on and off |
| `SetTerrainOcclusionEnabled` / `IsTerrainOcclusionEnabled` | |
| `IsHeightFieldReady` / `RebuildHeightField` | |
| `SetShroudResolution` / `GetShroudResolution` | resolution, memory carried across |
| `SetShroudWorldBounds` / `SetShroudUpdatesPerSecond` | |
| `GetShroudStats` / `HasShroud` | |

### `UShroudSettings`

`UDeveloperSettings`, `Config = Game`. See §8.

### `FShroudStats`

A `BlueprintType` struct returned by `GetShroudStats`. Everything in it is **counted where the work
happens**, not estimated: `Teams`, `ViewTeam`, `Resolution`, `TotalCells`, `CellSize`, `Revealers`,
`ActiveRevealers`, `Occluders`, `VisibleCells`, `ExploredCells`, `TextureUploads`, `UploadedBytes`,
`FootprintRebuilds`, `FootprintRebuildsDeferred`, `FootprintCells`, `OcclusionSamples`,
`UpdateMilliseconds`, `FootprintMilliseconds`, `CompositeMilliseconds`, `UploadMilliseconds`,
`bMemoryEnabled`, `bTerrainOcclusionEnabled`, `HeightFieldResolution`, `HeightFieldProgress`,
`HeightFieldTraces`, `UpdatesPerSecond`, `UpdateCount`.

### `AShroudHUD`

The counters box, drawn on `UCanvas` from `AHUD`. `bShowStats`, `StatsBoxOrigin`, `StatsBoxWidth`,
`ToggleStats()`. Blueprintable — subclass it and keep your own HUD work alongside.

**There are deliberately no buttons on it**, for two reasons that pull the same way:

- The box has to survive a cooked Shipping build. `DrawDebug` is compiled out there and a debug widget
  is usually stripped; a Canvas overlay is not.
- Anything that has to be *clicked* belongs in UMG. An `AHUD` hit box is tested against
  `UGameViewportClient::GetMousePosition()`, which reports nothing on a machine with no mouse attached
  — a capture rig, a build agent, a headless test. Widgets are unaffected.

So the numbers live here, where they always draw and cost nothing, and the controls live in a widget,
where they always receive the click.

---

## 8. Project settings

`Project Settings → Plugins → ShroudMap`, stored in `DefaultGame.ini`. Read once when a world's
subsystem comes up; everything important can be moved afterwards at runtime, so a demo button and a
shipped game drive the same code.

### Map

| Setting | Default | Notes |
|---|---|---|
| `Resolution` | 512 | 32..2048, square. 512 over 1 km ≈ 2 m per cell |
| `WorldOrigin` | (0,0) | centre of the covered square |
| `WorldSize` | 100000 | cm. **Size it generously** — outside the square queries answer `Unknown` and the texture clamps to its rim |
| `NumTeams` | 2 | 1..8, one texture and one mask each |
| `DefaultViewTeam` | 0 | |

### Update

| Setting | Default | Notes |
|---|---|---|
| `UpdatesPerSecond` | 20 | 0 = every frame. Fog does not need frame rate |
| `bTickInEditorWorlds` | true | see the fog without pressing Play |
| `MaxFootprintRebuildsPerUpdate` | 64 | 0 lifts the cap |
| `RebuildMoveTolerance` | 0.5 | cells; below this a drift cannot move a pixel |

### Appearance

| Setting | Default | Notes |
|---|---|---|
| `bMemoryEnabled` | true | the middle state |
| `DefaultSoftEdge` | 0.25 | fraction of the radius that fades |
| `VisibilityThreshold` | 0.35 | **the one number that ties the picture to the answers** |
| `UnknownColor` / `ExploredColor` / `VisibleColor` | black / dim slate / white | published to the collection |
| `ParameterCollection` | none | optional; see §9 |

### Terrain occlusion

| Setting | Default | Notes |
|---|---|---|
| `bTerrainOccludes` | false | |
| `HeightFieldResolution` | 128 | 128 over a km is a sample every 8 m — coarse on purpose |
| `MaxHeightFieldTracesPerUpdate` | 1024 | so switching it on never hitches |
| `HeightTraceChannel` | WorldStatic | |
| `HeightTraceStartZ` / `HeightTraceEndZ` | ±100000 | the downward trace, and the height of a miss |
| `RaysPerRimCell` | 2.0 | 2 leaves no gap at the rim; 1 halves the cost |
| `MinRaysPerRevealer` / `MaxRaysPerRevealer` | 32 / 512 | floor and ceiling on one revealer's cost |
| `HorizonSlack` | 0.03 | tolerance as a slope |
| `NearFieldCells` | 1.5 | always lit |
| `MaxOccludedFootprintRebuildsPerUpdate` | 8 | the separate, much smaller budget |

### Debug

| Setting | Default | Notes |
|---|---|---|
| `bShowStatsByDefault` | true | show the counters box as soon as a world with a `ShroudHUD` comes up |

---

## 9. Materials

The texture is linear (not sRGB), bilinear, clamped, `B8G8R8A8`, never streamed:

- **R** — current sight, 0..1
- **G** — memory, 0..1 (equals R while memory is switched off)
- **B** — free
- **A** — 1

UV from world position:

```
UV = (WorldXY - GridMin) / WorldSize        GridMin = WorldOrigin - WorldSize/2
```

`MF_ShroudSample` (shipped in `/ShroudMap/ShroudMap/Materials/`) does this and returns the three-state
tint, and `M_ShroudPostProcess` is there for a running start. Set the texture parameter from
`GetShroudTexture` — a texture cannot travel in a parameter collection.

### Optional: material parameter collection

Point `ParameterCollection` at a collection (`MPC_ShroudMap` ships as a ready-made one) and the
subsystem publishes, whenever any of it moves:

| Parameter | Type | Value |
|---|---|---|
| `ShroudBounds` | Vector | X,Y = `GridMin`; Z = `WorldSize`; W = `1/WorldSize` |
| `ShroudUnknownColor` / `ShroudExploredColor` / `ShroudVisibleColor` | Vector | from the settings |
| `ShroudResolution` | Scalar | |
| `ShroudMemoryEnabled` | Scalar | 1 or 0 |

Parameters the collection does not have are simply skipped. This is what lets a landscape material
follow a runtime `SetShroudWorldBounds` with no Blueprint pushing values around.

---

## 10. Console commands

```
Shroud.Test [Count] [RingRadius] [SightRadius]   orbiting revealers (0 removes them)
Shroud.Reveal [Radius|all] [Team]                reveal a circle at the viewpoint, or everything
Shroud.Clear [Team]                              forget what a team has seen
Shroud.Stats                                     print the measured counters
Shroud.Resolution [N]                            print or set the resolution
Shroud.Memory [0|1]                              print or set whether the map remembers
Shroud.Occlusion [0|1]                           print or set terrain occlusion
Shroud.Team [N]                                  print or set the local view team
```

`Shroud.Test` spawns transient actors and keeps them orbiting, because a still frame proves nothing.
It only runs in a game world.

---

## 11. Demo content

Mounted at `/ShroudMap/ShroudMap/` (24 assets):

| Path | What |
|---|---|
| `Maps/L_ShroudMapDemo` | top-down demo: 2 teams, 12 revealers, 2 occluders, 512×512 shroud over 24 000 cm |
| `Blueprints/BP_ShroudScout` · `BP_ShroudEnemy` | patrolling units that reveal, and units that get hidden |
| `Blueprints/BP_ShroudTower` | carries a revealer **and** an occluder — the watchtower-over-the-wall case |
| `Blueprints/BP_ShroudWall` · `BP_ShroudDepot` | occluder and static objective |
| `Blueprints/BP_ShroudDemoGameMode` · `…Controller` · `…HUD` · `…Pawn` · `…Director` | the harness |
| `UI/WBP_ShroudDemoPanel` | six buttons: memory, terrain occlusion, switch view team, clear memory, reveal all, stats box |
| `Materials/` | `M_ShroudGround` + 6 instances, `MF_ShroudSample`, `MPC_ShroudMap`, `M_ShroudPostProcess`, `T_ShroudDefault` |

Open `L_ShroudMapDemo`, press Play, and use the panel. Every button is one node from `UShroudStatics`
— the widget graph is the shortest reference for the Blueprint API there is.

The demo is a sample, not a dependency. Nothing in the plugin's C++ references it, and you can delete
`Content/` entirely without touching the module.

---

## 12. Checking the claims

Every one of these is visible on the stats box; none of them has to be taken on trust.

| Claim | How to check |
|---|---|
| 200 revealers = 1 upload per team | `Shroud.Test 200`. **Texture uploads** stays at the team count. |
| Memory really is the middle state | `Shroud.Memory 0` — explored terrain falls back to black; `Shroud.Memory 1` — it comes straight back. Nothing was destroyed. |
| Resolution costs what it says | `Shroud.Resolution 256` then `1024`. **Update** rises measurably, and the map you had uncovered is resampled rather than wiped. |
| Queries match the picture | Put a marker at a test position and drive it from `IsVisible`. It flips exactly when that spot lights up. |
| Terrain really occludes | `Shroud.Occlusion 1`, wait for the height field, walk a revealer up to a hill: a shadow stays behind it. |
| Deferred rebuilds are real, not hidden | Drop `MaxFootprintRebuildsPerUpdate` to 1 with many movers: **deferred** climbs on the stats box and the fog edge visibly lags. Nothing blinks out. |

---

## 13. Notes and limits

- **The map is square.** A rectangular level wastes a few cells at the sides, which is far cheaper
  than the bookkeeping two independent axes would cost in every loop.
- **Outside the square is `Unknown`,** while the texture clamps to its rim pixel. A level that spills
  over the boundary is a setup mistake, and answering `Unknown` makes it show up as one.
- **The map is 2D.** One value per XY cell. A multi-storey building, a tunnel network or a stack of
  decks all resolve to a single state per column.
- **`SetShroudWorldBounds` wipes memory** — the cells no longer mean what they meant.
  `SetShroudResolution` does **not**: memory is resampled across.
- **Nothing is replicated.** Replicate the revealers; each client computes the same map. Note that
  this means the fog is client-authoritative — an unmodified client can compute another team's map.
  For a competitive title, gate what the server sends, and treat the fog as presentation.
- **Terrain occlusion reads a height field, not the collision world.** A bridge you can see under, an
  archway, an overhang: a height field has one height per sample and will treat these as solid. That
  is the trade that buys radial sampling instead of a trace per cell.
- **Memory does not decay.** `max(old, new)`, forever, until `ClearMemory`. There is no
  half-remembered state and no timed forgetting.
- **`Shroud.Test` needs a game world.** It spawns actors; an editor world does not get them.

---

## 14. ShroudMap and TurretMind

They are not alternatives and should not be weighed against each other.

- **TurretMind** decides what a turret *aims at* — a targeting service, a spatial index of targets, a
  budget for re-evaluation, lead prediction.
- **ShroudMap** decides what the *player sees* — a map of visible, remembered and unknown ground.

Different questions, different data, no overlap. A game can use both, and they do not talk to each
other.

---

## 15. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `Shroud.Stats` says *"no ShroudMap subsystem in this world"* | The world type is not supported, or you are in a plain editor world with `bTickInEditorWorlds` off. Press Play, or switch the setting on. |
| Everything is shrouded and nothing reveals | No revealer registered, or every revealer's `Team` differs from the view team. Check **Revealers** and **Active revealers** on the stats box — they tell the two apart. |
| The fog is a smear at the level's edge | `WorldSize` does not cover the level. The texture clamps to its rim pixel outside the square. Grow `WorldSize`. |
| The fog edge looks like stair steps | `Resolution` too low for the map size. Cell size is on the stats box; 512 over 1 km is ~2 m. |
| `IsVisible` disagrees with what looks lit | `VisibilityThreshold` is the line between the two. The soft rim is drawn but is below it — lower the threshold to count the rim as sight. |
| Terrain occlusion is on but nothing is shadowed | The height field is still being gathered. `IsHeightFieldReady`, or **Height field** on the stats box, says how far along it is. |
| A revealer on a gentle slope shadows its own feet | Raise `HorizonSlack`, or `NearFieldCells`. |
| The fog edge lags behind fast movers | The rebuild budget is being hit. **Deferred** on the stats box confirms it; raise `MaxFootprintRebuildsPerUpdate` (or the occluded one), or accept the lag — it is the designed behaviour under load. |
| A unit is visible when it should be hidden | `Apply Shroud Visibility To Actor` is not being called for it, or `bHideInExplored` is off. |
| A frame spike when occlusion is switched on | Lower `MaxHeightFieldTracesPerUpdate` — the gathering is spread over more updates. |

---

© 2026 Simulated Flow. All rights reserved.
