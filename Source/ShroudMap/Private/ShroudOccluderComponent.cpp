// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "ShroudOccluderComponent.h"

#include "ShroudSubsystem.h"

UShroudOccluderComponent::UShroudOccluderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	bWantsOnUpdateTransform = false;
}

void UShroudOccluderComponent::OnRegister()
{
	Super::OnRegister();

	if (UShroudSubsystem* Subsystem = UShroudSubsystem::Get(this))
	{
		RegisteredSubsystem = Subsystem;
		LastStampedLocation = GetComponentLocation();
		Subsystem->RegisterOccluder(this);
	}
}

void UShroudOccluderComponent::OnUnregister()
{
	if (UShroudSubsystem* Subsystem = RegisteredSubsystem.Get())
	{
		Subsystem->UnregisterOccluder(this);
	}
	RegisteredSubsystem.Reset();

	Super::OnUnregister();
}

void UShroudOccluderComponent::SetOccluderEnabled(bool bNewEnabled)
{
	if (bOccluderEnabled == bNewEnabled)
	{
		return;
	}

	bOccluderEnabled = bNewEnabled;
	MarkOccluderDirty();
}

void UShroudOccluderComponent::SetBlockHeight(float NewHeight)
{
	NewHeight = FMath::Max(0.0f, NewHeight);
	if (FMath::IsNearlyEqual(BlockHeight, NewHeight))
	{
		return;
	}

	BlockHeight = NewHeight;
	MarkOccluderDirty();
}

void UShroudOccluderComponent::SetRadius(float NewRadius)
{
	NewRadius = FMath::Max(0.0f, NewRadius);
	if (FMath::IsNearlyEqual(Radius, NewRadius))
	{
		return;
	}

	Radius = NewRadius;
	MarkOccluderDirty();
}

void UShroudOccluderComponent::MarkOccluderDirty()
{
	if (UShroudSubsystem* Subsystem = RegisteredSubsystem.Get())
	{
		Subsystem->MarkOccludersDirty();
	}
}

FVector2D UShroudOccluderComponent::GetPlanarExtent() const
{
	if (Shape == EShroudOccluderShape::Box)
	{
		return FVector2D(FMath::Abs(BoxExtent.X), FMath::Abs(BoxExtent.Y));
	}

	return FVector2D(Radius, Radius);
}
