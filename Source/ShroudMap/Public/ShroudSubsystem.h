// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShroudTypes.h"
#include "ShroudSubsystem.generated.h"

class UMaterialParameterCollection;
class UShroudOccluderComponent;
class UShroudRevealerComponent;
class UTexture;
class UTexture2D;

/**
 * An inclusive rectangle in cell coordinates, or nothing at all.
 *
 * The whole update is driven by these: what has to be cleared, what was stamped, what has to be packed
 * and uploaded. A map is a megabyte and a battle touches a corner of it, so every pass is bounded by a
 * rectangle rather than walking the grid.
 */
struct FShroudCellRect
{
	int32 MinX = MAX_int32;
	int32 MinY = MAX_int32;
	int32 MaxX = MIN_int32;
	int32 MaxY = MIN_int32;

	bool IsEmpty() const { return MaxX < MinX || MaxY < MinY; }
	void Reset() { MinX = MAX_int32; MinY = MAX_int32; MaxX = MIN_int32; MaxY = MIN_int32; }

	void Include(int32 X, int32 Y)
	{
		MinX = FMath::Min(MinX, X);
		MinY = FMath::Min(MinY, Y);
		MaxX = FMath::Max(MaxX, X);
		MaxY = FMath::Max(MaxY, Y);
	}

	void IncludeRange(int32 X0, int32 Y0, int32 X1, int32 Y1)
	{
		Include(X0, Y0);
		Include(X1, Y1);
	}

	void Union(const FShroudCellRect& Other)
	{
		if (Other.IsEmpty())
		{
			return;
		}
		Include(Other.MinX, Other.MinY);
		Include(Other.MaxX, Other.MaxY);
	}

	void ClampToGrid(int32 Resolution)
	{
		if (IsEmpty())
		{
			return;
		}
		MinX = FMath::Clamp(MinX, 0, Resolution - 1);
		MinY = FMath::Clamp(MinY, 0, Resolution - 1);
		MaxX = FMath::Clamp(MaxX, 0, Resolution - 1);
		MaxY = FMath::Clamp(MaxY, 0, Resolution - 1);
	}

	int32 Width() const { return IsEmpty() ? 0 : MaxX - MinX + 1; }
	int32 Height() const { return IsEmpty() ? 0 : MaxY - MinY + 1; }

	static FShroudCellRect WholeGrid(int32 Resolution)
	{
		FShroudCellRect Rect;
		Rect.IncludeRange(0, 0, Resolution - 1, Resolution - 1);
		return Rect;
	}
};

/**
 * The set of cells one revealer lights, worked out once and stamped for free until it changes.
 *
 * Packed one cell per uint32: the cell index in the top 24 bits, the brightness in the bottom 8. At the
 * 2048-cell ceiling the index needs 22 bits, so this holds with room to spare, and a revealer's whole
 * footprint stays a flat array the stamping pass walks front to back.
 */
struct FShroudFootprint
{
	TArray<uint32> Cells;
	FShroudCellRect Bounds;

	FVector BuiltAtEye = FVector::ZeroVector;
	float BuiltRadius = -1.0f;
	float BuiltSoftEdge = -1.0f;
	float BuiltStrength = -1.0f;
	int32 BuiltTeam = INDEX_NONE;
	bool bBuiltWithOcclusion = false;
	bool bDirty = true;
	bool bValid = false;
};

/**
 * One team's map: what is lit now, what has ever been lit, and the pixels that carry both to the GPU.
 *
 * Vision and Memory are plain byte-per-cell masks at the texture's own resolution. That is what lets
 * IsVisible answer in O(1) on the game thread with no readback and no stall, and it is the same byte
 * the material samples - so a Blueprint's answer and the pixel on screen cannot drift apart.
 */
struct FShroudTeamState
{
	/** Brightness a revealer of this team is putting on each cell right now. Cleared every update. */
	TArray<uint8> Vision;

	/** Brightest this team has ever seen each cell. Never decays; only ClearMemory takes it away. */
	TArray<uint8> Memory;

	/** BGRA staging buffer: R = Vision, G = Memory as displayed, B = 0, A = 255. */
	TArray<uint8> Pixels;

	/** Cells where Vision may be non-zero. Everything outside is known to be zero without looking. */
	FShroudCellRect VisionRect;

	/** Cells whose pixels are known to be out of date, from reveals, clears and toggles. */
	FShroudCellRect DirtyRect;

	/** Cells above the threshold in Vision, counted where the work happens rather than by scanning. */
	int32 VisibleCells = 0;

	/** Cells above the threshold in Memory. Only ever grows, until ClearMemory. */
	int32 ExploredCells = 0;

	/** The next update packs and uploads the whole grid. Set by anything that changes it wholesale. */
	bool bFullDirty = true;
};

/**
 * Owns the fog for a world: the per-team masks, the visibility textures, the coarse height field, the
 * budget and the numbers.
 *
 * The update is four bounded passes. Revealer footprints are rebuilt - only the stale ones, only up to
 * the budget - then stamped into their team's sight mask, then folded into that team's memory, then the
 * rectangle that changed is packed to BGRA and handed to the render thread as a single region update.
 *
 * That last step is the design: one write per team per update, whether the world holds two revealers or
 * two hundred. Nothing here scales a draw call with the number of things that can see.
 */
UCLASS()
class SHROUDMAP_API UShroudSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override;

	/** Convenience getter. Returns null outside a world that supports the subsystem. */
	static UShroudSubsystem* Get(const UObject* WorldContextObject);

	//~ Registration -------------------------------------------------------------------------------------

	/** Called by the component itself on register. Safe to call twice. */
	void RegisterRevealer(UShroudRevealerComponent* Revealer);

	/** Called by the component itself on unregister. Safe to call for something never registered. */
	void UnregisterRevealer(UShroudRevealerComponent* Revealer);

	/** Called by the component itself on register. Safe to call twice. */
	void RegisterOccluder(UShroudOccluderComponent* Occluder);

	/** Called by the component itself on unregister. Safe to call for something never registered. */
	void UnregisterOccluder(UShroudOccluderComponent* Occluder);

	/** Ask for one revealer's cached shape to be worked out again on the next update. */
	void MarkRevealerDirty(const UShroudRevealerComponent* Revealer);

	/** Ask for every revealer's cached shape to be worked out again on the next update. */
	void MarkAllRevealersDirty();

	/** Ask for the occluders to be stamped into the height field again on the next update. */
	void MarkOccludersDirty();

	//~ Queries ------------------------------------------------------------------------------------------

	/**
	 * What a world position is for a team. O(1): one clamp, one index, two byte comparisons.
	 * Team below zero means the view team. Anything outside the mapped square is Unknown.
	 */
	UFUNCTION(BlueprintPure, Category = "ShroudMap|Query")
	EShroudVisibility GetVisibilityState(const FVector& WorldLocation, int32 Team = -1) const;

	/** True when a revealer of that team is lighting this position right now. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap|Query")
	bool IsVisible(const FVector& WorldLocation, int32 Team = -1) const;

	/** True when that team has ever seen this position - which stays true once it has. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap|Query")
	bool IsExplored(const FVector& WorldLocation, int32 Team = -1) const;

	/** Raw current-sight brightness at a world position, 0..1. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap|Query")
	float GetVisibilityValue(const FVector& WorldLocation, int32 Team = -1) const;

	/** Raw remembered brightness at a world position, 0..1. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap|Query")
	float GetMemoryValue(const FVector& WorldLocation, int32 Team = -1) const;

	//~ Texture and materials ----------------------------------------------------------------------------

	/**
	 * The visibility texture for a team. R is current sight, G is memory, both 0..1.
	 * Team below zero means the view team. Feed it to MF_ShroudSample, or sample it yourself.
	 */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	UTexture* GetShroudTexture(int32 Team = -1) const;

	/** UV of a world position inside the visibility texture. Outside the map this leaves 0..1. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	FVector2D WorldToShroudUV(const FVector& WorldLocation) const;

	//~ Teams --------------------------------------------------------------------------------------------

	/** Team the local player sees the world as. Queries and GetShroudTexture default to it. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetViewTeam(int32 NewTeam);

	/** Team the local player sees the world as. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	int32 GetViewTeam() const { return ViewTeam; }

	/** Teams this world keeps a map for. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	int32 GetNumTeams() const { return NumTeams; }

	//~ Reveal and memory --------------------------------------------------------------------------------

	/**
	 * Light a circle into a team's map without a revealer, and leave it in that team's memory.
	 *
	 * This is the one-off: a scripted scouting report, a captured map, a mission start that hands the
	 * player the road they arrived on. Team below zero reveals for every team.
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void RevealCircle(const FVector& Center, float Radius, int32 Team = -1, float SoftEdge = -1.0f, bool bAffectCurrentSight = false);

	/** Reveal the whole map for a team. Team below zero does every team. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void RevealAll(int32 Team = -1);

	/** Forget everything a team has seen. Team below zero clears every team. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void ClearMemory(int32 Team = -1);

	/**
	 * Turn the middle state on or off for the whole world.
	 *
	 * Off does not destroy anything: what a team has explored is kept and simply stops being drawn, so
	 * the map falls back to plain visible-or-nothing and comes straight back when this is switched on.
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetMemoryEnabled(bool bNewEnabled);

	/** True while the map keeps what it has seen. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	bool IsMemoryEnabled() const { return bMemoryEnabled; }

	//~ Terrain occlusion --------------------------------------------------------------------------------

	/**
	 * Let terrain and occluders stop sight. Switching it on starts gathering the height field, which is
	 * spread over updates; occlusion applies from the update the field is complete.
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetTerrainOcclusionEnabled(bool bNewEnabled);

	/** True while sight is stopped by the height field. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	bool IsTerrainOcclusionEnabled() const { return bTerrainOccludes; }

	/** True once the height field is fully gathered and occlusion is actually being applied. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	bool IsHeightFieldReady() const { return bHeightFieldReady; }

	/** Throw the gathered height field away and trace it again, for terrain that changed. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void RebuildHeightField();

	//~ Map geometry -------------------------------------------------------------------------------------

	/**
	 * Change the resolution of the texture and of the mask at runtime.
	 *
	 * Memory survives: what each team had explored is resampled into the new grid, so switching from 256
	 * to 1024 sharpens the map rather than wiping it. Everything else is rebuilt from scratch.
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetResolution(int32 NewResolution);

	/** Edge length of the texture and of the mask, in cells. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	int32 GetResolution() const { return Resolution; }

	/** Move or resize the square the map covers. This does wipe memory: the cells no longer line up. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetWorldBounds(FVector2D NewOrigin, float NewSize);

	/** Centre of the square the map covers. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	FVector2D GetWorldOrigin() const { return WorldOrigin; }

	/** Edge length of the square the map covers, in cm. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	float GetWorldSize() const { return WorldSize; }

	/** World size of one cell in cm. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	float GetCellSize() const { return CellSize; }

	/** How often the map is recomputed, in Hz. 0 means every frame. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetUpdatesPerSecond(float NewRate);

	/** How often the map is recomputed, in Hz. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	float GetUpdatesPerSecond() const { return UpdatesPerSecond; }

	//~ Stats --------------------------------------------------------------------------------------------

	/** Measured counters, refreshed on every update. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	const FShroudStats& GetStats() const { return Stats; }

	/** Write the current numbers into the log, one line per counter. Backs the Shroud.Stats command. */
	void LogStats() const;

	//~ Console support ----------------------------------------------------------------------------------

	/**
	 * Put Count revealers on a ring around Origin and keep them orbiting, for the Shroud.Test command.
	 * Count of 0 removes them again. This is the two-hundred-revealers-one-upload proof, in one line.
	 */
	int32 SpawnTestRevealers(int32 Count, const FVector& Origin, float RingRadius, float SightRadius);

	/** How many test revealers Shroud.Test currently has running. */
	int32 GetTestRevealerCount() const { return TestRevealers.Num(); }

	//~ Grid helpers -------------------------------------------------------------------------------------

	/** Continuous cell coordinates of a world position. Cell (x,y) has its centre at (x+0.5, y+0.5). */
	FVector2D WorldToCellF(const FVector& WorldLocation) const;

	/** Cell a world position falls in, unclamped - it can be outside the grid. */
	FIntPoint WorldToCell(const FVector& WorldLocation) const;

	/** World XY of a cell's centre. */
	FVector2D CellToWorld(int32 CellX, int32 CellY) const;

	/** True when a cell index is on the grid. */
	bool IsCellOnGrid(int32 CellX, int32 CellY) const
	{
		return CellX >= 0 && CellY >= 0 && CellX < Resolution && CellY < Resolution;
	}

private:
	//~ Update passes ------------------------------------------------------------------------------------

	/** Reallocate masks, textures and the height field after anything about the geometry changed. */
	void RebuildGrids(int32 OldResolution, bool bPreserveMemory);

	/** Create or recreate one team's visibility texture. */
	void CreateTeamTexture(int32 TeamIndex);

	/** Trace another slice of the height field. Returns the number of traces spent. */
	int32 GatherHeightFieldSlice();

	/** Copy the terrain heights into the combined field and raise them where occluders stand. */
	void StampOccluders();

	/** Rebuild the stale revealer footprints, up to the budget. */
	void UpdateFootprints();

	/** Work out the cells one revealer lights. The only genuinely expensive thing in here. */
	void BuildFootprint(const UShroudRevealerComponent& Revealer, FShroudFootprint& Footprint, bool bUseOcclusion);

	/** Plain disc: a bounding box walk with a radial falloff. What most projects ever need. */
	void BuildDiscFootprint(const FVector2D& CenterCell, float RadiusCells, float SoftEdge, float Strength, FShroudFootprint& Footprint);

	/** Radial horizon sampling over the height field, for a revealer that terrain can stop. */
	void BuildOccludedFootprint(const UShroudRevealerComponent& Revealer, const FVector2D& CenterCell, float RadiusCells, float SoftEdge, float Strength, FShroudFootprint& Footprint);

	/** Clear last update's sight, stamp the cached footprints, fold sight into memory, count cells. */
	void CompositeTeams();

	/** Pack the changed rectangle of one team to BGRA and hand it to the render thread. One upload. */
	void UploadTeamTexture(int32 TeamIndex, const FShroudCellRect& Rect);

	/** Push bounds and the three state colours into the configured material parameter collection. */
	void PublishMaterialParameters();

	/** Keep the Shroud.Test revealers orbiting so the demo proof is moving, not a still frame. */
	void UpdateTestRevealers(float DeltaSeconds);

	/** Remove the Shroud.Test revealers and their actors. */
	void DestroyTestRevealers();

	/** Bilinear height at continuous cell coordinates, from the combined field. */
	float SampleHeightAtCell(float CellX, float CellY) const;

	/** Write one cell into a team's memory, keeping the explored count exact. */
	void WriteMemoryCell(FShroudTeamState& State, int32 CellIndex, uint8 Value);

	/** Resolve a team argument: below zero means the view team, anything else is clamped. */
	int32 ResolveTeam(int32 Team) const;

	/** Re-read the project settings into the cached hot fields. */
	void ApplySettings();

	//~ State --------------------------------------------------------------------------------------------

	/** One visibility texture per team. Handed out by GetShroudTexture and never drawn into by the CPU. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> TeamTextures;

	/** Registered revealers. Parallel to Footprints; the two are always added and removed together. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UShroudRevealerComponent>> Revealers;

	/** Registered occluders. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UShroudOccluderComponent>> Occluders;

	/** Transient actors carrying the Shroud.Test revealers. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> TestRevealers;

	/** The parameter collection from the settings, resolved once. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> ResolvedParameterCollection;

	/** Cached shapes, one per registered revealer, in the same order. */
	TArray<FShroudFootprint> Footprints;

	/** Per-team masks and staging pixels. */
	TArray<FShroudTeamState> TeamStates;

	/** Terrain heights as traced, HeightFieldResolution squared. */
	TArray<float> TerrainHeights;

	/** Terrain heights with the occluders raised on top. This is what the horizon sampling reads. */
	TArray<float> CombinedHeights;

	/** Reused scratch for one revealer's occluded footprint, so a rebuild allocates nothing. */
	TArray<uint8> FootprintScratch;

	/** Phase of each Shroud.Test revealer on its orbit. */
	TArray<float> TestRevealerPhases;

	/** Centre of the Shroud.Test orbit. */
	FVector TestRingOrigin = FVector::ZeroVector;

	/** Radius of the Shroud.Test orbit, in cm. */
	float TestRingRadius = 0.0f;

	//~ Geometry -----------------------------------------------------------------------------------------

	int32 Resolution = 512;
	int32 NumTeams = 2;
	int32 ViewTeam = 0;
	FVector2D WorldOrigin = FVector2D::ZeroVector;
	float WorldSize = 100000.0f;
	float CellSize = 195.3125f;

	/** South-west corner of the mapped square, in world cm. Everything is measured from here. */
	FVector2D GridMin = FVector2D::ZeroVector;

	//~ Cached settings ----------------------------------------------------------------------------------

	float UpdatesPerSecond = 20.0f;
	float DefaultSoftEdge = 0.25f;
	float RebuildMoveTolerance = 0.5f;
	int32 MaxFootprintRebuildsPerUpdate = 64;
	uint8 VisibilityThresholdByte = 89;

	int32 HeightFieldResolution = 128;
	int32 MaxHeightFieldTracesPerUpdate = 1024;
	TEnumAsByte<ECollisionChannel> HeightTraceChannel = ECC_WorldStatic;
	float HeightTraceStartZ = 100000.0f;
	float HeightTraceEndZ = -100000.0f;
	float RaysPerRimCell = 2.0f;
	int32 MinRaysPerRevealer = 32;
	int32 MaxRaysPerRevealer = 512;
	float HorizonSlack = 0.03f;
	float NearFieldCells = 1.5f;
	int32 MaxOccludedFootprintRebuildsPerUpdate = 8;
	bool bTickInEditorWorlds = true;

	//~ Switches -----------------------------------------------------------------------------------------

	bool bMemoryEnabled = true;
	bool bTerrainOccludes = false;

	//~ Height field progress ----------------------------------------------------------------------------

	/** Next sample the gathering pass will trace. Equals HeightFieldResolution squared when finished. */
	int32 HeightFieldCursor = 0;

	/** True once every sample has been traced and the occluders have been stamped at least once. */
	bool bHeightFieldReady = false;

	/** The occluders have moved, appeared or gone; the combined field needs stamping again. */
	bool bOccludersDirty = true;

	//~ Budget -------------------------------------------------------------------------------------------

	/** Where the round-robin cursor left off, so no revealer can be starved by the one before it. */
	int32 RebuildCursor = 0;

	/** Seconds since the last update, against the update rate. */
	float UpdateAccumulator = 0.0f;

	/** Counters for the update in progress, published into Stats at the end of it. */
	int32 FootprintRebuildsThisUpdate = 0;
	int32 FootprintRebuildsDeferredThisUpdate = 0;
	int32 OcclusionSamplesThisUpdate = 0;
	int32 ActiveRevealersThisUpdate = 0;
	int32 TextureUploadsThisUpdate = 0;
	int32 UploadedBytesThisUpdate = 0;
	int32 HeightFieldTracesThisUpdate = 0;
	double FootprintSecondsThisUpdate = 0.0;
	double CompositeSecondsThisUpdate = 0.0;
	double UploadSecondsThisUpdate = 0.0;

	/** The parameter collection is written only when something in it actually moved. */
	bool bMaterialParametersDirty = true;

	/** Published through GetStats(). */
	FShroudStats Stats;
};
