// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ShroudTypes.h"
#include "ShroudOccluderComponent.generated.h"

class UShroudSubsystem;

/**
 * Optional. Hang this on something that should block sight but is not terrain: a building, a wall, a
 * boulder, a container stack.
 *
 * It does not add a code path of its own. An occluder raises the coarse height field the radial sight
 * sampling already runs over, so a wall and a ridge stop a scout in exactly the same way and cost
 * exactly the same nothing per update. That also means it only has any effect while the world has
 * terrain occlusion switched on.
 *
 * It does not tick. Moving it, resizing it or switching it off marks the height field for a restamp,
 * which is a memcpy and a handful of cells - not a re-trace of the terrain.
 */
UCLASS(ClassGroup = (ShroudMap), meta = (BlueprintSpawnableComponent, DisplayName = "Shroud Occluder"))
class SHROUDMAP_API UShroudOccluderComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UShroudOccluderComponent();

	// UActorComponent interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	//~ Configuration ------------------------------------------------------------------------------------

	/** Circle or axis-aligned box. Rotation is ignored either way: the height field has no rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap")
	EShroudOccluderShape Shape = EShroudOccluderShape::Circle;

	/** Radius of the blocked footprint in cm, for the Circle shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap", meta = (ClampMin = "0.0", UIMax = "10000.0", EditCondition = "Shape == EShroudOccluderShape::Circle", EditConditionHides))
	float Radius = 300.0f;

	/** Half-size of the blocked footprint on X and Y in cm, for the Box shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap", meta = (EditCondition = "Shape == EShroudOccluderShape::Box", EditConditionHides))
	FVector2D BoxExtent = FVector2D(300.0f, 300.0f);

	/**
	 * How far above the component sight is blocked, in cm. This is a height, not a radius: a one-storey
	 * wall stops a soldier and does not stop the watchtower looking over it, which falls straight out of
	 * comparing the two against the same height field.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap", meta = (ClampMin = "0.0", UIMax = "10000.0"))
	float BlockHeight = 500.0f;

	/** Off, the occluder stays registered and stops blocking on the next height field restamp. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroudMap")
	bool bOccluderEnabled = true;

	//~ Runtime ------------------------------------------------------------------------------------------

	/** Turn blocking on or off without unregistering. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetOccluderEnabled(bool bNewEnabled);

	/** Change how far above the component sight is blocked. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetBlockHeight(float NewHeight);

	/** Change the blocked radius, for the Circle shape. */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void SetRadius(float NewRadius);

	/**
	 * Ask for the height field to be stamped again on the next update. Moving, resizing and switching
	 * the occluder already do this on their own; this is for anything the component cannot see.
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroudMap")
	void MarkOccluderDirty();

	/** World-space XY extent this occluder covers, whatever its shape. */
	FVector2D GetPlanarExtent() const;

private:
	/** Subsystem of the world this component registered with, so unregistering finds the same one back. */
	TWeakObjectPtr<UShroudSubsystem> RegisteredSubsystem;

	/** Last transform the height field was stamped with, so a nudge below one sample is not restamped. */
	FVector LastStampedLocation = FVector::ZeroVector;

	friend class UShroudSubsystem;
};
