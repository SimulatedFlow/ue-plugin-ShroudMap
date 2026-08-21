// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "ShroudSettings.generated.h"

class UMaterialParameterCollection;

/**
 * Project-wide defaults for the shroud.
 * Edit under Project Settings > Plugins > ShroudMap; stored in DefaultGame.ini.
 *
 * Everything here is read once, when a world's subsystem comes up. Resolution, world bounds, update
 * rate, memory and terrain occlusion can all be moved afterwards at runtime - through the subsystem's
 * setters, the Blueprint library, or the Shroud.* console commands - without touching the config. A
 * demo button and a shipped game therefore drive exactly the same code.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ShroudMap"))
class SHROUDMAP_API UShroudSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UShroudSettings();

	// UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** Convenience accessor. Never returns null. */
	static const UShroudSettings& Get();

	// --------------------------------------------------------------------------------------------------
	// Map
	// --------------------------------------------------------------------------------------------------

	/**
	 * Edge length of the visibility texture and of the CPU mask, in cells. Square, power of two.
	 *
	 * This is the only knob that costs on both sides at once: memory grows with the square, and so does
	 * the rectangle that has to be packed and uploaded. 512 over a one-kilometre map gives just under two
	 * metres per cell, which is finer than a unit and finer than anyone reads off a screen. Go to 1024
	 * when the map is large enough that two metres starts to look like stair steps at the fog edge.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Map", meta = (ClampMin = "32", ClampMax = "2048"))
	int32 Resolution = 512;

	/** Centre of the square the map covers, on the XY plane, in world cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Map")
	FVector2D WorldOrigin = FVector2D::ZeroVector;

	/**
	 * Edge length of that square in cm. The map is always square; a rectangular level just wastes a few
	 * cells at the sides, which is far cheaper than the bookkeeping two axes would cost everywhere else.
	 *
	 * Size it generously. Outside the square a query answers Unknown, and the texture clamps to its edge
	 * pixel, so a level that spills over the boundary shows a smear at the rim.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Map", meta = (ClampMin = "1000.0", UIMax = "500000.0"))
	float WorldSize = 100000.0f;

	/** Teams the map is kept for, each with its own texture and its own mask. */
	UPROPERTY(Config, EditAnywhere, Category = "Map", meta = (ClampMin = "1", ClampMax = "8"))
	int32 NumTeams = 2;

	/** Team the local player belongs to at startup. Change it at runtime with SetTeam. */
	UPROPERTY(Config, EditAnywhere, Category = "Map", meta = (ClampMin = "0", ClampMax = "7"))
	int32 DefaultViewTeam = 0;

	// --------------------------------------------------------------------------------------------------
	// Update
	// --------------------------------------------------------------------------------------------------

	/**
	 * How often the map is recomputed, in Hz. 0 means every frame.
	 *
	 * Fog does not need frame rate. 20 Hz is already faster than a unit crosses a cell, and it divides
	 * the whole cost - rasterising, compositing and uploading - by three at 60 fps.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Update", meta = (ClampMin = "0.0", UIMax = "60.0"))
	float UpdatesPerSecond = 20.0f;

	/** Run the subsystem in a plain editor world too, so the fog is visible without pressing Play. */
	UPROPERTY(Config, EditAnywhere, Category = "Update")
	bool bTickInEditorWorlds = true;

	/**
	 * Hard cap on revealer footprints rebuilt in one update. 0 lifts the cap.
	 *
	 * A footprint is the set of cells one revealer lights, worked out once and then stamped for free
	 * until the revealer moves. Rebuilding is the only part that costs, and with terrain occlusion on it
	 * costs a lot, so a cursor walks the revealer list and whoever does not get a turn stamps the shape
	 * it had last time. Nothing blinks out; the fog edge lags by an update or two under load, which is
	 * the trade a two-hundred-revealer battle wants to make.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Update", meta = (ClampMin = "0", UIMax = "512"))
	int32 MaxFootprintRebuildsPerUpdate = 64;

	/**
	 * How far a revealer may drift, in cells, before its footprint is considered stale. Below this the
	 * cached shape is stamped as it is, because a shift of half a cell cannot change a single pixel.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Update", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float RebuildMoveTolerance = 0.5f;

	// --------------------------------------------------------------------------------------------------
	// Appearance
	// --------------------------------------------------------------------------------------------------

	/** Memory on: terrain once seen stays drawn. Off: the map only ever shows what is being looked at. */
	UPROPERTY(Config, EditAnywhere, Category = "Appearance")
	bool bMemoryEnabled = true;

	/**
	 * Fraction of a revealer's radius that fades out, 0..1, when the revealer does not set its own.
	 * 0 gives a hard circle, which reads as a cookie cutter; a quarter of the radius is enough to look
	 * like light falling off without eating into the useful area.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultSoftEdge = 0.25f;

	/**
	 * How bright a cell has to be, 0..1, before IsVisible and IsExplored call it lit.
	 *
	 * This is the one number that ties the picture to the answers: everything at or above it is what the
	 * queries call visible, and it is the same byte the material reads. Set it low and the soft rim
	 * counts as sight; set it high and only the solid middle does.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Appearance", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float VisibilityThreshold = 0.35f;

	/** Colour of never-seen ground. Published to the material parameter collection as ShroudUnknownColor. */
	UPROPERTY(Config, EditAnywhere, Category = "Appearance")
	FLinearColor UnknownColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);

	/** Tint of remembered ground. Published as ShroudExploredColor. */
	UPROPERTY(Config, EditAnywhere, Category = "Appearance")
	FLinearColor ExploredColor = FLinearColor(0.22f, 0.24f, 0.30f, 1.0f);

	/** Tint of ground in sight. Published as ShroudVisibleColor. */
	UPROPERTY(Config, EditAnywhere, Category = "Appearance")
	FLinearColor VisibleColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	/**
	 * Optional collection the subsystem publishes the map's bounds and the three state colours into,
	 * so MF_ShroudSample works in any material without a Blueprint pushing parameters around.
	 *
	 * Expected parameters - missing ones are simply skipped:
	 *   Vector ShroudBounds        (X, Y = the south-west corner in world cm; Z = WorldSize; W = 1/WorldSize)
	 *   Vector ShroudUnknownColor
	 *   Vector ShroudExploredColor
	 *   Vector ShroudVisibleColor
	 *   Scalar ShroudResolution
	 *   Scalar ShroudMemoryEnabled
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Appearance", meta = (AllowedClasses = "/Script/Engine.MaterialParameterCollection"))
	TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection;

	// --------------------------------------------------------------------------------------------------
	// Terrain occlusion
	// --------------------------------------------------------------------------------------------------

	/**
	 * Let terrain stop sight. Off - the default - a revealer lights a plain disc, which is what most
	 * top-down games want and costs almost nothing.
	 *
	 * On, sight spreads by radial horizon sampling over a coarse height field: a ray leaving the eye
	 * keeps the steepest downward angle it has met, and ground that does not clear that angle stays dark.
	 * That is one pass per ray, not one trace per cell, and the price is on the statistics box.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain Occlusion")
	bool bTerrainOccludes = false;

	/**
	 * Edge length of the height field in samples. It is gathered by tracing straight down once, so this
	 * squared is how many traces the world pays - spread over updates, never in one frame.
	 *
	 * Coarse on purpose. The field answers "is there a ridge between here and there", and a ridge is not
	 * a five-metre feature. 128 over a kilometre is a sample every eight metres.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain Occlusion", meta = (ClampMin = "16", ClampMax = "512"))
	int32 HeightFieldResolution = 128;

	/** Traces spent gathering the height field per update, so switching occlusion on never hitches. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain Occlusion", meta = (ClampMin = "16", UIMax = "8192"))
	int32 MaxHeightFieldTracesPerUpdate = 1024;

	/** Collision channel the downward height traces use. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain Occlusion")
	TEnumAsByte<ECollisionChannel> HeightTraceChannel = ECC_WorldStatic;

	/** Height the downward trace starts from, in world cm. Must be above everything you want measured. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain Occlusion")
	float HeightTraceStartZ = 100000.0f;

	/** Height the downward trace ends at, and the height a sample gets when it hits nothing. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain Occlusion")
	float HeightTraceEndZ = -100000.0f;

	/**
	 * Rays fired per cell of circumference when sampling a revealer's horizon. 2 leaves no gaps at the
	 * rim; 1 halves the cost and lets the odd single cell slip through at a long radius.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain Occlusion", meta = (ClampMin = "0.5", ClampMax = "4.0"))
	float RaysPerRimCell = 2.0f;

	/** Never fire fewer rays than this, however small the radius. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain Occlusion", meta = (ClampMin = "8", ClampMax = "2048"))
	int32 MinRaysPerRevealer = 32;

	/** Never fire more rays than this, however large the radius. The ceiling on one revealer's cost. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain Occlusion", meta = (ClampMin = "8", ClampMax = "4096"))
	int32 MaxRaysPerRevealer = 512;

	/**
	 * Slack in the horizon test, as a slope. Ground has noise in it and the height field is coarse, so
	 * without a little tolerance a revealer standing on a gentle rise shadows its own feet.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain Occlusion", meta = (ClampMin = "0.0", UIMax = "0.5"))
	float HorizonSlack = 0.03f;

	/** Distance from the eye, in cells, that is lit whatever the height field says. You see your own feet. */
	UPROPERTY(Config, EditAnywhere, Category = "Terrain Occlusion", meta = (ClampMin = "0.0", UIMax = "8.0"))
	float NearFieldCells = 1.5f;

	// --------------------------------------------------------------------------------------------------
	// Debug
	// --------------------------------------------------------------------------------------------------

	/** Show the statistics box as soon as a world with a ShroudHUD comes up. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bShowStatsByDefault = true;
};
