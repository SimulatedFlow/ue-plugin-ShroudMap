// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "ShroudRevealerComponent.h"

#include "ShroudSettings.h"
#include "ShroudSubsystem.h"

UShroudRevealerComponent::UShroudRevealerComponent()
{
	// It never ticks. The subsystem walks the registered revealers once per update, which is the whole
	// reason two hundred of these cost what two of them cost.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	bWantsOnUpdateTransform = false;
}

void UShroudRevealerComponent::OnRegister()
{
	Super::OnRegister();

	if (UShroudSubsystem* Subsystem = UShroudSubsystem::Get(this))
	{
		RegisteredSubsystem = Subsystem;
		Subsystem->RegisterRevealer(this);
	}
}

void UShroudRevealerComponent::OnUnregister()
{
	// Deliberately the subsystem we registered with rather than the one this component can reach now:
	// during world teardown the second one can already be gone, and the first must still be told.
	if (UShroudSubsystem* Subsystem = RegisteredSubsystem.Get())
	{
		Subsystem->UnregisterRevealer(this);
	}
	RegisteredSubsystem.Reset();

	Super::OnUnregister();
}

void UShroudRevealerComponent::SetTeam(int32 NewTeam)
{
	if (Team == NewTeam)
	{
		return;
	}

	Team = NewTeam;
	MarkFootprintDirty();
}

void UShroudRevealerComponent::SetSightRadius(float NewRadius)
{
	NewRadius = FMath::Max(0.0f, NewRadius);
	if (FMath::IsNearlyEqual(SightRadius, NewRadius))
	{
		return;
	}

	SightRadius = NewRadius;
	MarkFootprintDirty();
}

void UShroudRevealerComponent::SetRevealEnabled(bool bNewEnabled)
{
	if (bRevealEnabled == bNewEnabled)
	{
		return;
	}

	bRevealEnabled = bNewEnabled;
	MarkFootprintDirty();
}

void UShroudRevealerComponent::SetStrength(float NewStrength)
{
	NewStrength = FMath::Clamp(NewStrength, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(Strength, NewStrength))
	{
		return;
	}

	Strength = NewStrength;
	MarkFootprintDirty();
}

void UShroudRevealerComponent::MarkFootprintDirty()
{
	if (UShroudSubsystem* Subsystem = RegisteredSubsystem.Get())
	{
		Subsystem->MarkRevealerDirty(this);
	}
}

float UShroudRevealerComponent::GetEffectiveSoftEdge() const
{
	if (SoftEdge < 0.0f)
	{
		return FMath::Clamp(UShroudSettings::Get().DefaultSoftEdge, 0.0f, 1.0f);
	}

	return FMath::Clamp(SoftEdge, 0.0f, 1.0f);
}

FVector UShroudRevealerComponent::GetEyeLocation() const
{
	FVector Location = GetComponentLocation();
	Location.Z += EyeHeight;
	return Location;
}

bool UShroudRevealerComponent::IsRevealing() const
{
	return bRevealEnabled && SightRadius > 0.0f && Strength > 0.0f;
}
