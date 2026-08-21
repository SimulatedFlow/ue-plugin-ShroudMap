// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "ShroudStatics.h"

#include "GameFramework/Actor.h"
#include "ShroudSettings.h"
#include "ShroudSubsystem.h"

//~ Queries --------------------------------------------------------------------------------------------

bool UShroudStatics::IsVisible(const UObject* WorldContextObject, const FVector& WorldLocation, int32 Team)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem && Subsystem->IsVisible(WorldLocation, Team);
}

bool UShroudStatics::IsExplored(const UObject* WorldContextObject, const FVector& WorldLocation, int32 Team)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem && Subsystem->IsExplored(WorldLocation, Team);
}

EShroudVisibility UShroudStatics::GetVisibilityState(const UObject* WorldContextObject, const FVector& WorldLocation, int32 Team)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetVisibilityState(WorldLocation, Team) : EShroudVisibility::Unknown;
}

float UShroudStatics::GetVisibilityValue(const UObject* WorldContextObject, const FVector& WorldLocation, int32 Team)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetVisibilityValue(WorldLocation, Team) : 0.0f;
}

float UShroudStatics::GetMemoryValue(const UObject* WorldContextObject, const FVector& WorldLocation, int32 Team)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetMemoryValue(WorldLocation, Team) : 0.0f;
}

EShroudVisibility UShroudStatics::GetActorVisibilityState(const UObject* WorldContextObject, const AActor* Actor, int32 Team)
{
	if (!Actor)
	{
		return EShroudVisibility::Unknown;
	}

	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject ? WorldContextObject : Actor);
	return Subsystem ? Subsystem->GetVisibilityState(Actor->GetActorLocation(), Team) : EShroudVisibility::Unknown;
}

bool UShroudStatics::ApplyShroudVisibilityToActor(const UObject* WorldContextObject, AActor* Actor, int32 Team, bool bHideInExplored)
{
	if (!Actor)
	{
		return false;
	}

	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject ? WorldContextObject : Actor);
	if (!Subsystem)
	{
		// No shroud in this world: leave the actor exactly as it was rather than hiding it. A Blueprint
		// written against ShroudMap has to keep working in a map that never set the plugin up.
		return !Actor->IsHidden();
	}

	const EShroudVisibility State = Subsystem->GetVisibilityState(Actor->GetActorLocation(), Team);
	const bool bShouldBeVisible = State == EShroudVisibility::Visible
		|| (State == EShroudVisibility::Explored && !bHideInExplored);

	Actor->SetActorHiddenInGame(!bShouldBeVisible);
	return bShouldBeVisible;
}

//~ Texture and mapping --------------------------------------------------------------------------------

UTexture* UShroudStatics::GetShroudTexture(const UObject* WorldContextObject, int32 Team)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetShroudTexture(Team) : nullptr;
}

FVector2D UShroudStatics::WorldToShroudUV(const UObject* WorldContextObject, const FVector& WorldLocation)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->WorldToShroudUV(WorldLocation) : FVector2D::ZeroVector;
}

void UShroudStatics::GetShroudBounds(const UObject* WorldContextObject, FVector2D& OutOrigin, float& OutWorldSize, float& OutCellSize)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	if (!Subsystem)
	{
		OutOrigin = FVector2D::ZeroVector;
		OutWorldSize = 0.0f;
		OutCellSize = 0.0f;
		return;
	}

	const float WorldSize = Subsystem->GetWorldSize();
	OutOrigin = Subsystem->GetWorldOrigin() - FVector2D(WorldSize * 0.5f, WorldSize * 0.5f);
	OutWorldSize = WorldSize;
	OutCellSize = Subsystem->GetCellSize();
}

void UShroudStatics::GetShroudStateColors(FLinearColor& OutUnknown, FLinearColor& OutExplored, FLinearColor& OutVisible)
{
	const UShroudSettings& Settings = UShroudSettings::Get();
	OutUnknown = Settings.UnknownColor;
	OutExplored = Settings.ExploredColor;
	OutVisible = Settings.VisibleColor;
}

//~ Teams ----------------------------------------------------------------------------------------------

void UShroudStatics::SetTeam(const UObject* WorldContextObject, int32 Team)
{
	if (UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetViewTeam(Team);
	}
}

int32 UShroudStatics::GetTeam(const UObject* WorldContextObject)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetViewTeam() : 0;
}

int32 UShroudStatics::GetNumTeams(const UObject* WorldContextObject)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetNumTeams() : 0;
}

//~ Reveal and memory ----------------------------------------------------------------------------------

void UShroudStatics::RevealCircle(const UObject* WorldContextObject, const FVector& Center, float Radius, int32 Team, float SoftEdge, bool bAffectCurrentSight)
{
	if (UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject))
	{
		Subsystem->RevealCircle(Center, Radius, Team, SoftEdge, bAffectCurrentSight);
	}
}

void UShroudStatics::RevealAll(const UObject* WorldContextObject, int32 Team)
{
	if (UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject))
	{
		Subsystem->RevealAll(Team);
	}
}

void UShroudStatics::ClearMemory(const UObject* WorldContextObject, int32 Team)
{
	if (UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject))
	{
		Subsystem->ClearMemory(Team);
	}
}

void UShroudStatics::SetMemoryEnabled(const UObject* WorldContextObject, bool bEnabled)
{
	if (UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetMemoryEnabled(bEnabled);
	}
}

bool UShroudStatics::IsMemoryEnabled(const UObject* WorldContextObject)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem && Subsystem->IsMemoryEnabled();
}

//~ Terrain occlusion ----------------------------------------------------------------------------------

void UShroudStatics::SetTerrainOcclusionEnabled(const UObject* WorldContextObject, bool bEnabled)
{
	if (UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetTerrainOcclusionEnabled(bEnabled);
	}
}

bool UShroudStatics::IsTerrainOcclusionEnabled(const UObject* WorldContextObject)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem && Subsystem->IsTerrainOcclusionEnabled();
}

bool UShroudStatics::IsHeightFieldReady(const UObject* WorldContextObject)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem && Subsystem->IsHeightFieldReady();
}

void UShroudStatics::RebuildHeightField(const UObject* WorldContextObject)
{
	if (UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject))
	{
		Subsystem->RebuildHeightField();
	}
}

//~ Map geometry ---------------------------------------------------------------------------------------

void UShroudStatics::SetShroudResolution(const UObject* WorldContextObject, int32 Resolution)
{
	if (UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetResolution(Resolution);
	}
}

int32 UShroudStatics::GetShroudResolution(const UObject* WorldContextObject)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetResolution() : 0;
}

void UShroudStatics::SetShroudWorldBounds(const UObject* WorldContextObject, FVector2D Origin, float WorldSize)
{
	if (UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetWorldBounds(Origin, WorldSize);
	}
}

void UShroudStatics::SetShroudUpdatesPerSecond(const UObject* WorldContextObject, float UpdatesPerSecond)
{
	if (UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetUpdatesPerSecond(UpdatesPerSecond);
	}
}

//~ Stats ----------------------------------------------------------------------------------------------

FShroudStats UShroudStatics::GetShroudStats(const UObject* WorldContextObject)
{
	const UShroudSubsystem* Subsystem = UShroudSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetStats() : FShroudStats();
}

bool UShroudStatics::HasShroud(const UObject* WorldContextObject)
{
	return UShroudSubsystem::Get(WorldContextObject) != nullptr;
}
