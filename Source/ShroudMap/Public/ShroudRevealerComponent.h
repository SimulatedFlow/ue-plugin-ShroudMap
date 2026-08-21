// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ShroudRevealerComponent.generated.h"

class UShroudSubsystem;

/**
 * Hang this on anything that gives sight: a unit, a watchtower, a vehicle, a security camera, a flare
 * lying on the ground. It lights a circle of the map for its team and, unless told otherwise, leaves
 * that circle in the team's memory.
 *
 * It does not tick. There is no per-revealer timer, no per-revealer trace and no per-revealer draw
 * call. The world subsystem walks the registered revealers once per update, works out the shape each
 * one lights, and composites the lot into one texture per team.
 *
 * The shape is cached. Standing still costs nothing at all beyond stamping cells that were already
 * worked out; moving costs a rebuild, and rebuilds are budgeted, so a revealer that does not get its
 * turn stamps the shape it had last update rather than blinking out.
 */
UCLASS(ClassGroup = (ShroudMap), meta = (BlueprintSpawnableComponent, DisplayName = "Shroud Revealer"))
class SHROUDMAP_API UShroudRevealerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UShroudRevealerComponent();

	// UActorComponent interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	//~ Configuration ------------------------------------------------------------------------------------

	/** How far this revealer sees, in cm, measured on the XY plane from the component. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap", meta = (ClampMin = "0.0", UIMax = "20000.0"))
	float SightRadius = 1500.0f;

	/**
	 * Height of the eye above the component, in cm. Only terrain occlusion reads it - a taller eye
	 * clears a ridge a shorter one does not, which is the whole reason a watchtower is worth building.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap", meta = (ClampMin = "0.0", UIMax = "5000.0"))
	float EyeHeight = 180.0f;

	/** Team whose map this revealer writes into. Out-of-range values are clamped by the subsystem. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap", meta = (ClampMin = "0", ClampMax = "7"))
	int32 Team = 0;

	/**
	 * Fraction of the radius that fades out, 0..1. Negative takes the project default, which is what
	 * keeps a level consistent when nobody has thought about this particular unit yet.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float SoftEdge = -1.0f;

	/**
	 * How brightly this revealer lights its circle, 0..1. Below the project's visibility threshold the
	 * cells are drawn but the queries still call them unexplored - which is how a "half sight" unit,
	 * a scanner sweep or a wounded scout is expressed without a second system.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 1.0f;

	/** Off, the revealer stays registered and costs nothing. This is the cheap way to blind a unit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap")
	bool bRevealEnabled = true;

	/**
	 * On, what this revealer lights is folded into the team's memory and stays drawn once it leaves.
	 * Off, it only ever lights the current frame - a radar sweep, a flash of lightning, a searchlight
	 * that tells you where the enemy is without mapping the ground for you.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap")
	bool bLeavesMemory = true;

	/**
	 * Ignore the height field even when the world has terrain occlusion switched on. For anything that
	 * looks down on the map rather than across it: aircraft, satellites, the map-hack in a debug build.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap")
	bool bIgnoreTerrainOcclusion = false;

	//~ Runtime ------------------------------------------------------------------------------------------

	/** Move this revealer to another team. The old team keeps what it had already remembered. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetTeam(int32 NewTeam);

	/** Change the sight radius. Costs one footprint rebuild, on the next update, under the budget. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetSightRadius(float NewRadius);

	/** Turn revealing on or off without unregistering. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetRevealEnabled(bool bNewEnabled);

	/** Change how brightly the circle is lit, 0..1. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetStrength(float NewStrength);

	/**
	 * Ask for the cached shape to be worked out again on the next update. Only needed after something
	 * the component cannot see for itself changed - a piece of level geometry moving, for instance.
	 * Moving, resizing and retasking the revealer already do this on their own.
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void MarkFootprintDirty();

	/** Soft edge this revealer actually uses, with the project default already resolved. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	float GetEffectiveSoftEdge() const;

	/** World position of the eye: the component's location, raised by EyeHeight. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	FVector GetEyeLocation() const;

	/** True when this revealer would contribute to the map right now. */
	UFUNCTION(BlueprintPure, Category = "ShroudMap")
	bool IsRevealing() const;

private:
	/** Subsystem of the world this component registered with, so unregistering finds the same one back. */
	TWeakObjectPtr<UShroudSubsystem> RegisteredSubsystem;
};
