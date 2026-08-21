# ShroudMap — Documentation

Unreal Engine 5.8 · Runtime module, `LoadingPhase: PreDefault` · Win64 built and verified;
Mac and Linux allow-listed but not built.

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

## 2. How it is computed

One update, four bounded passes. Everything is driven by rectangles in cell space, because a map is a
megabyte and a battle touches a corner of it.

### 2.1 Grid

The world is covered by a square, `WorldSize` cm on a side, centred on `WorldOrigin`, cut into
`Resolution × Resolution` cells. Per team there are three arrays at that resolution:

- `Vision` — one byte per cell, how brightly a revealer of this team is lighting it **now**.
  Cleared every update.
- `Memory` — one byte per cell, the brightest this team has **ever** seen it. `max(old, new)`.
  Never decays; only `ClearMemory` takes it away.
- `Pixels` — the BGRA staging buffer. `R = Vision`, `G = Memory`, `B = 0`, `A = 255`.

`Vision` and `Memory` are what `IsVisible` and `IsExplored` read, and they are the same bytes the
texture carries. That is deliberate: there is one number, not a CPU estimate that tracks a GPU truth.

### 2.2 Footprints

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

### 2.3 Compositing

1. Clear each team's `Vision` — only over **last update's** rectangle. Outside it the mask is known
   to be zero without looking.
2. Stamp the cached footprints of every revealer with `bLeavesMemory`.
3. Fold `Vision` into `Memory` over the changed rectangle, counting cells that cross the threshold.
4. Stamp the footprints of revealers **without** `bLeavesMemory` — a radar sweep, a searchlight, a
   flash of lightning. Going in after the fold is the whole of what "lights without mapping" costs.
5. Pack the changed rectangle to BGRA, count visible cells, and hand the region to the render thread.

### 2.4 The upload

**This is the number the design stands on.** One `UpdateTextureRegions` call per team per update,
covering only the rectangle that changed, whether the world holds two revealers or two hundred. A
revealer never gets a GPU write of its own.

The staging copy is tightly packed and owned by the render command, because the buffer it came from
is overwritten on the next update and the upload has not necessarily happened by then.

> **Deviation worth stating plainly.** The design brief called for the revealers to be drawn into a
> `UTextureRenderTarget2D` as instanced canvas quads in a single draw call. What ships instead is a
> CPU rasterisation into a `UTexture2D` pushed up as one region update. Two reasons, both in your
> favour: it is *one* GPU operation per team rather than a clear plus a batch, and — more
> importantly — it means the CPU mask a Blueprint queries and the pixels a material samples are
> **literally the same bytes**, computed once. A GPU draw path would need the mask computed a second
> time on the CPU anyway (that is what makes `IsVisible` O(1) with no readback), and two computations
> of the same thing drift. The public surface is unchanged: `GetShroudTexture` hands you a `UTexture*`
> with sight in R and memory in G.

### 2.5 Terrain occlusion

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

## 3. Classes

### `UShroudRevealerComponent`

Hang it on anything that gives sight. **It does not tick.**

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

Setters (`SetTeam`, `SetSightRadius`, `SetRevealEnabled`, `SetStrength`) mark the footprint stale for
you. `MarkFootprintDirty` is only for changes the component cannot see — level geometry moving, say.

### `UShroudOccluderComponent`

Optional. `Shape` (Circle or axis-aligned Box), `Radius` / `BoxExtent`, `BlockHeight`,
`bOccluderEnabled`. Rotation is ignored: the height field is axis-aligned. Only has any effect while
terrain occlusion is on.

### `UShroudSubsystem`

`UTickableWorldSubsystem`. `DoesSupportWorldType` covers **Game, PIE and Editor** — a plain editor
world only gets one when `bTickInEditorWorlds` is set, because the fog allocates and creates textures
and that is not something to do behind a level designer's back.

Everything on it is also on `UShroudStatics`; reach for the subsystem directly from C++.

### `UShroudStatics`

The whole plugin from Blueprint. Every call is safe in a world with no shroud: queries answer
`Unknown`, setters do nothing, `ApplyShroudVisibilityToActor` leaves the actor alone. That matters —
a Blueprint written against ShroudMap still runs in a test map where the plugin was never set up.

A `Team` argument **below zero always means "the local view team"**, which is what a single-player and
a hotseat game both want with no branching.

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
| `SetTerrainOcclusionEnabled` / `IsTerrainOcclusionEnabled` / `IsHeightFieldReady` / `RebuildHeightField` | |
| `SetShroudResolution` / `GetShroudResolution` / `SetShroudWorldBounds` / `SetShroudUpdatesPerSecond` | |
| `GetShroudStats` / `HasShroud` | |

### `AShroudHUD`

The counters box, drawn on `UCanvas` from `AHUD`. **There are deliberately no buttons on it**, for two
reasons that pull the same way:

- The box has to survive a cooked Shipping build. `DrawDebug` is compiled out there and a debug widget
  is usually stripped; a Canvas overlay is not.
- Anything that has to be *clicked* belongs in UMG. An `AHUD` hit box is tested against
  `UGameViewportClient::GetMousePosition()`, which reports nothing on a machine with no mouse attached
  — a capture rig, a build agent, a headless test. Widgets are unaffected.

So the numbers live here, where they always draw and cost nothing, and the controls live in a widget,
where they always receive the click.

---

## 4. Project settings

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
| `ParameterCollection` | none | optional; see below |

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

---

## 5. Materials

The texture is linear (not sRGB), bilinear, clamped, `B8G8R8A8`:

- **R** — current sight, 0..1
- **G** — memory, 0..1 (equals R while memory is switched off)
- **B** — free
- **A** — 1

UV from world position:

```
UV = (WorldXY - GridMin) / WorldSize        GridMin = WorldOrigin - WorldSize/2
```

`MF_ShroudSample` (shipped in `Content/ShroudMap/`) does this and returns the three-state tint, and a
ready-made postprocess material is there for a running start. Set the texture parameter from
`GetShroudTexture` — a texture cannot travel in a parameter collection.

### Optional: material parameter collection

Point `ParameterCollection` at a collection and the subsystem publishes, whenever any of it moves:

| Parameter | Type | Value |
|---|---|---|
| `ShroudBounds` | Vector | X,Y = `GridMin`; Z = `WorldSize`; W = `1/WorldSize` |
| `ShroudUnknownColor` / `ShroudExploredColor` / `ShroudVisibleColor` | Vector | from the settings |
| `ShroudResolution` | Scalar | |
| `ShroudMemoryEnabled` | Scalar | 1 or 0 |

Parameters the collection does not have are simply skipped. This is what lets a landscape material
follow a runtime `SetShroudWorldBounds` with no Blueprint pushing values around.

---

## 6. Console

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

## 7. Checking the claims

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

## 8. Notes and limits

- **The map is square.** A rectangular level wastes a few cells at the sides, which is far cheaper
  than the bookkeeping two independent axes would cost in every loop.
- **Outside the square is `Unknown`,** while the texture clamps to its rim pixel. A level that spills
  over the boundary is a setup mistake, and answering `Unknown` makes it show up as one.
- **`SetShroudWorldBounds` wipes memory** — the cells no longer mean what they meant.
  `SetShroudResolution` does **not**: memory is resampled across.
- **Nothing is replicated.** Replicate the revealers; each client computes the same map.
- **Terrain occlusion reads a height field, not the collision world.** A bridge you can see under, an
  archway, an overhang: a height field has one height per sample and will treat these as solid. That
  is the trade that buys radial sampling instead of a trace per cell.
- **`Shroud.Test` needs a game world.** It spawns actors; an editor world does not get them.

---

## 9. ShroudMap and TurretMind

They are not alternatives and should not be weighed against each other.

- **TurretMind** decides what a turret *aims at* — a targeting service, a spatial index of targets, a
  budget for re-evaluation, lead prediction.
- **ShroudMap** decides what the *player sees* — a map of visible, remembered and unknown ground.

Different questions, different data, no overlap. A game can use both, and they do not talk to each
other.

---

© 2026 Silvan Teufel. All rights reserved.
