// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShroudTypes.h"
#include "ShroudStatics.generated.h"

class AActor;
class UTexture;

/**
 * The whole plugin from Blueprint, without a reference to the subsystem in sight.
 *
 * Every call here is world-context-aware and safe in a world that has no shroud at all: queries answer
 * Unknown, setters do nothing. That matters more than it sounds, because it means a Blueprint written
 * against ShroudMap still runs in a test map where the plugin was never set up.
 *
 * A Team argument below zero always means "the team the local player sees the world as", which is what
 * a single-player game and a hotseat game both want without any branching.
 */
UCLASS(meta = (DisplayName = "ShroudMap"))
class SHROUDMAP_API UShroudStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ Queries ------------------------------------------------------------------------------------------

	/** True when a revealer of that team is lighting this position right now. O(1), no readback. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap|Query", meta = (WorldContext = "WorldContextObject"))
	static bool IsVisible(const UObject* WorldContextObject, const FVector& WorldLocation, int32 Team = -1);

	/** True when that team has ever seen this position. Stays true once it is - that is the point. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap|Query", meta = (WorldContext = "WorldContextObject"))
	static bool IsExplored(const UObject* WorldContextObject, const FVector& WorldLocation, int32 Team = -1);

	/** Unknown, Explored or Visible in one call, which is what a Switch node wants. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap|Query", meta = (WorldContext = "WorldContextObject"))
	static EShroudVisibility GetVisibilityState(const UObject* WorldContextObject, const FVector& WorldLocation, int32 Team = -1);

	/** Raw current-sight brightness at a world position, 0..1. The same byte the material samples. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap|Query", meta = (WorldContext = "WorldContextObject"))
	static float GetVisibilityValue(const UObject* WorldContextObject, const FVector& WorldLocation, int32 Team = -1);

	/** Raw remembered brightness at a world position, 0..1. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap|Query", meta = (WorldContext = "WorldContextObject"))
	static float GetMemoryValue(const UObject* WorldContextObject, const FVector& WorldLocation, int32 Team = -1);

	/** Where an actor stands, as a shroud state. Uses the actor's location, not its bounds. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap|Query", meta = (WorldContext = "WorldContextObject"))
	static EShroudVisibility GetActorVisibilityState(const UObject* WorldContextObject, const AActor* Actor, int32 Team = -1);

	/**
	 * Hide or show an actor according to what the team can see where it stands, and return whether it
	 * ended up visible.
	 *
	 * This is the three-state model paying off in one node. A unit standing in remembered ground is
	 * hidden while the ground under it stays drawn - which is exactly what a player expects and what a
	 * plain visible-or-not fog cannot express. bHideInExplored off keeps the unit drawn on remembered
	 * ground, for anything that does not move: a building, a wreck, a resource node.
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap|Query", meta = (WorldContext = "WorldContextObject"))
	static bool ApplyShroudVisibilityToActor(const UObject* WorldContextObject, AActor* Actor, int32 Team = -1, bool bHideInExplored = true);

	//~ Texture and mapping ------------------------------------------------------------------------------

	/**
	 * The visibility texture for a team. R is current sight, G is memory.
	 * Set it on your own material instance, or let MF_ShroudSample take it.
	 */
	UFUNCTION(BlueprintPure, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static UTexture* GetShroudTexture(const UObject* WorldContextObject, int32 Team = -1);

	/** UV of a world position inside the visibility texture, for a material driven from Blueprint. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static FVector2D WorldToShroudUV(const UObject* WorldContextObject, const FVector& WorldLocation);

	/** South-west corner of the mapped square and its edge length, for building the UV yourself. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static void GetShroudBounds(const UObject* WorldContextObject, FVector2D& OutOrigin, float& OutWorldSize, float& OutCellSize);

	/** The three state colours from the project settings, for a material driven from Blueprint. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	static void GetShroudStateColors(FLinearColor& OutUnknown, FLinearColor& OutExplored, FLinearColor& OutVisible);

	//~ Teams --------------------------------------------------------------------------------------------

	/** Set the team the local player sees the world as. This is the TEAM 1 / TEAM 2 button. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Team"))
	static void SetTeam(const UObject* WorldContextObject, int32 Team);

	/** Team the local player sees the world as. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Team"))
	static int32 GetTeam(const UObject* WorldContextObject);

	/** Teams this world keeps a map for. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static int32 GetNumTeams(const UObject* WorldContextObject);

	//~ Reveal and memory --------------------------------------------------------------------------------

	/**
	 * Light a circle into a team's map without a revealer, and leave it there.
	 * Team below zero reveals for every team. SoftEdge below zero takes the project default.
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static void RevealCircle(const UObject* WorldContextObject, const FVector& Center, float Radius, int32 Team = -1, float SoftEdge = -1.0f, bool bAffectCurrentSight = false);

	/** Reveal the whole map for a team. Team below zero does every team. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static void RevealAll(const UObject* WorldContextObject, int32 Team = -1);

	/** Forget everything a team has seen. Team below zero clears every team. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static void ClearMemory(const UObject* WorldContextObject, int32 Team = -1);

	/** Turn the remembered state on or off. Off keeps the bytes and stops drawing them. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static void SetMemoryEnabled(const UObject* WorldContextObject, bool bEnabled);

	/** True while the map keeps what it has seen. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static bool IsMemoryEnabled(const UObject* WorldContextObject);

	//~ Terrain occlusion --------------------------------------------------------------------------------

	/** Let terrain and occluders stop sight. The height field is gathered in the background. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static void SetTerrainOcclusionEnabled(const UObject* WorldContextObject, bool bEnabled);

	/** True while sight is stopped by the height field. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static bool IsTerrainOcclusionEnabled(const UObject* WorldContextObject);

	/** True once the height field is gathered and occlusion is actually being applied. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static bool IsHeightFieldReady(const UObject* WorldContextObject);

	/** Throw the gathered height field away and trace it again, for terrain that changed. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static void RebuildHeightField(const UObject* WorldContextObject);

	//~ Map geometry -------------------------------------------------------------------------------------

	/** Change the resolution at runtime. Memory is resampled rather than thrown away. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static void SetShroudResolution(const UObject* WorldContextObject, int32 Resolution);

	/** Edge length of the texture and of the mask, in cells. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static int32 GetShroudResolution(const UObject* WorldContextObject);

	/** Move or resize the square the map covers. This does wipe memory: the cells no longer line up. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static void SetShroudWorldBounds(const UObject* WorldContextObject, FVector2D Origin, float WorldSize);

	/** How often the map is recomputed, in Hz. 0 means every frame. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static void SetShroudUpdatesPerSecond(const UObject* WorldContextObject, float UpdatesPerSecond);

	//~ Stats --------------------------------------------------------------------------------------------

	/** Everything measured on the last update. Drives the statistics box, and any HUD you write. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static FShroudStats GetShroudStats(const UObject* WorldContextObject);

	/** True when this world has a shroud at all, so a Blueprint can branch instead of guessing. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap", meta = (WorldContext = "WorldContextObject"))
	static bool HasShroud(const UObject* WorldContextObject);
};
