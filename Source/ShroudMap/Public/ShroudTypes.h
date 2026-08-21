// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShroudTypes.generated.h"

/**
 * The three states a place on the map can be in for one team.
 *
 * The middle one is the whole point of this plugin. A line-of-sight system answers "can this observer
 * see that spot right now" and has nothing to say about a spot nobody is looking at. A strategy game
 * needs the map to keep what the team has learned: the hill you walked over at minute two is still
 * drawn at minute twenty, and the enemy that walked onto it since is not.
 */
UENUM(BlueprintType)
enum class EShroudVisibility : uint8
{
	/** Never seen by this team. Fully shrouded - no terrain, no props, no units. */
	Unknown = 0 UMETA(DisplayName = "Unknown"),

	/** Seen before but not now. Terrain stays drawn, dimmed; anything that can move is hidden. */
	Explored = 1 UMETA(DisplayName = "Explored"),

	/** A revealer of this team is looking at it this very update. Everything is drawn. */
	Visible = 2 UMETA(DisplayName = "Visible")
};

/** Footprint an occluder stamps into the height field. */
UENUM(BlueprintType)
enum class EShroudOccluderShape : uint8
{
	/** Radius around the component, the cheapest and the usual choice for a rock or a tree. */
	Circle = 0 UMETA(DisplayName = "Circle"),

	/** Axis-aligned rectangle from BoxExtent. Rotation is ignored: the height field is axis-aligned. */
	Box = 1 UMETA(DisplayName = "Box")
};

/**
 * Everything the subsystem measured on its last update. Nothing here is estimated or assumed; every
 * number is counted where the work happens, which is what makes the claims on the store page checkable
 * with the statistics box rather than on trust.
 */
USTRUCT(BlueprintType)
struct SHROUDMAP_API FShroudStats
{
	GENERATED_BODY()

	/** Teams the map is kept for. One visibility texture and one CPU mask per team. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 Teams = 0;

	/** Team whose texture GetShroudTexture returns and whose answers the queries default to. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 ViewTeam = 0;

	/** Edge length of the visibility texture and of the CPU mask, in cells. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 Resolution = 0;

	/** Resolution * Resolution. What one team's mask costs in bytes, twice over. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 TotalCells = 0;

	/** World size of one cell in cm. Anything smaller than this the map cannot tell apart. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	float CellSize = 0.0f;

	/** Revealer components registered in this world, enabled or not. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 Revealers = 0;

	/** Revealers that actually contributed to the last update. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 ActiveRevealers = 0;

	/** Occluder components registered in this world. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 Occluders = 0;

	/** Cells above the visibility threshold in the view team's current-sight channel. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 VisibleCells = 0;

	/** Cells the view team has ever seen. Only ever grows, until ClearMemory. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 ExploredCells = 0;

	/**
	 * Writes into the visibility textures on the last update, across every team.
	 *
	 * This is the number the design stands or falls on: it is one per team that changed, whether the
	 * world holds two revealers or two hundred. A revealer never gets a write of its own.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 TextureUploads = 0;

	/** Bytes pushed to the GPU on the last update. Only the rectangle that changed is sent. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 UploadedBytes = 0;

	/** Revealer footprints rebuilt on the last update. Capped by the budget in the settings. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 FootprintRebuilds = 0;

	/** Revealer footprints that wanted a rebuild and did not get one, so they stamped their last shape. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 FootprintRebuildsDeferred = 0;

	/** Cells held across every cached revealer footprint. This is what the caching costs in memory. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 FootprintCells = 0;

	/** Height samples taken for radial occlusion on the last update. Zero when occlusion is off. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 OcclusionSamples = 0;

	/** Whole update, game thread, in ms. Everything below adds up to roughly this. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	float UpdateMilliseconds = 0.0f;

	/** Rebuilding revealer footprints, in ms. This is where terrain occlusion shows up. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	float FootprintMilliseconds = 0.0f;

	/** Stamping cached footprints into the masks and folding sight into memory, in ms. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	float CompositeMilliseconds = 0.0f;

	/** Packing pixels and handing the region to the render thread, in ms. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	float UploadMilliseconds = 0.0f;

	/** True while the map keeps what it has seen. Off, explored terrain falls back to unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	bool bMemoryEnabled = true;

	/** True while sight is stopped by the height field instead of running straight through hills. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	bool bTerrainOcclusionEnabled = false;

	/** Edge length of the coarse height field in samples. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 HeightFieldResolution = 0;

	/** How much of the height field has been gathered, 0..1. Occlusion only applies at 1. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	float HeightFieldProgress = 0.0f;

	/** Downward traces spent gathering the height field on the last update. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 HeightFieldTraces = 0;

	/** Updates per second the subsystem is aiming for. 0 means every frame. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	float UpdatesPerSecond = 0.0f;

	/** Updates run since this world's subsystem came up. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroudMap|Stats")
	int32 UpdateCount = 0;
};
