// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "ShroudSubsystem.h"

#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "PixelFormat.h"
#include "RHITypes.h"
#include "ShroudMapLog.h"
#include "ShroudOccluderComponent.h"
#include "ShroudRevealerComponent.h"
#include "ShroudSettings.h"

namespace ShroudMapPrivate
{
	/** Parameters the shipped MF_ShroudSample reads. Names the collection does not have are skipped. */
	static const FName ParamName_Bounds(TEXT("ShroudBounds"));
	static const FName ParamName_UnknownColor(TEXT("ShroudUnknownColor"));
	static const FName ParamName_ExploredColor(TEXT("ShroudExploredColor"));
	static const FName ParamName_VisibleColor(TEXT("ShroudVisibleColor"));
	static const FName ParamName_Resolution(TEXT("ShroudResolution"));
	static const FName ParamName_MemoryEnabled(TEXT("ShroudMemoryEnabled"));

	/** Bytes per pixel in the visibility texture. B8G8R8A8, so R is byte 2 and G is byte 1. */
	static constexpr int32 BytesPerPixel = 4;

	/** How far a ray steps between height samples, in cells. Half a cell leaves no gap along the ray. */
	static constexpr float RayStepCells = 0.5f;

	/** Ceiling on Shroud.Test, so a typo in the console cannot spawn a hundred thousand actors. */
	static constexpr int32 MaxTestRevealers = 4096;

	/** Smoothstep, so the soft rim reads as light falling off rather than as a linear ramp. */
	FORCEINLINE float SmoothFalloff(float Alpha)
	{
		Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		return Alpha * Alpha * (3.0f - 2.0f * Alpha);
	}

	UShroudSubsystem* GetSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UShroudSubsystem>() : nullptr;
	}

	/** Where a console command with no position of its own should act: in front of the local viewpoint. */
	FVector ResolveCommandOrigin(UWorld* World)
	{
		if (const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr)
		{
			FVector ViewLocation = FVector::ZeroVector;
			FRotator ViewRotation = FRotator::ZeroRotator;
			PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

			FVector Ahead = ViewRotation.Vector();
			Ahead.Z = 0.0f;
			Ahead = Ahead.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);

			return ViewLocation + Ahead * 1500.0f;
		}

		return FVector::ZeroVector;
	}
}

//~ Lifetime -------------------------------------------------------------------------------------------

bool UShroudSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}

	if (World->IsGameWorld())
	{
		return true;
	}

	// A plain editor world only gets a subsystem when the project asked for it: the fog allocates a few
	// megabytes and creates textures, which is not something to do behind a level designer's back.
	return World->WorldType == EWorldType::Editor && UShroudSettings::Get().bTickInEditorWorlds;
}

void UShroudSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ApplySettings();
	RebuildGrids(0, /*bPreserveMemory*/ false);

	UE_LOG(LogShroudMap, Log,
		TEXT("ShroudMap up: %d teams at %dx%d over %.0f cm (%.1f cm per cell), %.0f Hz, memory %s, terrain occlusion %s."),
		NumTeams, Resolution, Resolution, WorldSize, CellSize, UpdatesPerSecond,
		bMemoryEnabled ? TEXT("on") : TEXT("off"),
		bTerrainOccludes ? TEXT("on") : TEXT("off"));
}

void UShroudSubsystem::Deinitialize()
{
	DestroyTestRevealers();

	Revealers.Reset();
	Occluders.Reset();
	Footprints.Reset();
	TeamStates.Reset();
	TeamTextures.Reset();
	TerrainHeights.Reset();
	CombinedHeights.Reset();
	FootprintScratch.Reset();
	ResolvedParameterCollection = nullptr;

	Super::Deinitialize();
}

bool UShroudSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::Editor;
}

TStatId UShroudSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UShroudSubsystem, STATGROUP_Tickables);
}

bool UShroudSubsystem::IsTickableInEditor() const
{
	return bTickInEditorWorlds;
}

UShroudSubsystem* UShroudSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UShroudSubsystem>() : nullptr;
}

void UShroudSubsystem::ApplySettings()
{
	const UShroudSettings& Settings = UShroudSettings::Get();

	Resolution = FMath::Clamp(Settings.Resolution, 32, 2048);
	NumTeams = FMath::Clamp(Settings.NumTeams, 1, 8);
	ViewTeam = FMath::Clamp(Settings.DefaultViewTeam, 0, NumTeams - 1);
	WorldOrigin = Settings.WorldOrigin;
	WorldSize = FMath::Max(1000.0f, Settings.WorldSize);

	UpdatesPerSecond = FMath::Max(0.0f, Settings.UpdatesPerSecond);
	DefaultSoftEdge = FMath::Clamp(Settings.DefaultSoftEdge, 0.0f, 1.0f);
	RebuildMoveTolerance = FMath::Max(0.0f, Settings.RebuildMoveTolerance);
	MaxFootprintRebuildsPerUpdate = FMath::Max(0, Settings.MaxFootprintRebuildsPerUpdate);
	VisibilityThresholdByte = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Settings.VisibilityThreshold * 255.0f), 1, 255));

	bMemoryEnabled = Settings.bMemoryEnabled;
	bTerrainOccludes = Settings.bTerrainOccludes;
	bTickInEditorWorlds = Settings.bTickInEditorWorlds;

	HeightFieldResolution = FMath::Clamp(Settings.HeightFieldResolution, 16, 512);
	MaxHeightFieldTracesPerUpdate = FMath::Max(16, Settings.MaxHeightFieldTracesPerUpdate);
	HeightTraceChannel = Settings.HeightTraceChannel;
	HeightTraceStartZ = Settings.HeightTraceStartZ;
	HeightTraceEndZ = FMath::Min(Settings.HeightTraceEndZ, Settings.HeightTraceStartZ - 1.0f);
	RaysPerRimCell = FMath::Clamp(Settings.RaysPerRimCell, 0.5f, 4.0f);
	MinRaysPerRevealer = FMath::Clamp(Settings.MinRaysPerRevealer, 8, 2048);
	MaxRaysPerRevealer = FMath::Clamp(Settings.MaxRaysPerRevealer, MinRaysPerRevealer, 4096);
	HorizonSlack = FMath::Max(0.0f, Settings.HorizonSlack);
	NearFieldCells = FMath::Max(0.0f, Settings.NearFieldCells);
	MaxOccludedFootprintRebuildsPerUpdate = FMath::Max(0, Settings.MaxOccludedFootprintRebuildsPerUpdate);

	ResolvedParameterCollection = Settings.ParameterCollection.IsNull() ? nullptr : Settings.ParameterCollection.LoadSynchronous();
	bMaterialParametersDirty = true;
}

//~ Grids ----------------------------------------------------------------------------------------------

void UShroudSubsystem::RebuildGrids(int32 OldResolution, bool bPreserveMemory)
{
	const int32 Total = Resolution * Resolution;
	CellSize = WorldSize / static_cast<float>(Resolution);
	GridMin = WorldOrigin - FVector2D(WorldSize * 0.5f, WorldSize * 0.5f);

	// Lift the old memory out before the states are thrown away. This is what lets the resolution change
	// on a button press without the player losing the map they spent five minutes uncovering.
	TArray<TArray<uint8>> CarriedMemory;
	const bool bCanCarry = bPreserveMemory && OldResolution > 0 && TeamStates.Num() > 0;
	if (bCanCarry)
	{
		CarriedMemory.SetNum(TeamStates.Num());
		for (int32 Index = 0; Index < TeamStates.Num(); ++Index)
		{
			CarriedMemory[Index] = MoveTemp(TeamStates[Index].Memory);
		}
	}

	TeamStates.Reset();
	TeamStates.SetNum(NumTeams);

	for (int32 TeamIndex = 0; TeamIndex < NumTeams; ++TeamIndex)
	{
		FShroudTeamState& State = TeamStates[TeamIndex];
		State.Vision.SetNumZeroed(Total);
		State.Memory.SetNumZeroed(Total);
		State.Pixels.SetNumZeroed(Total * ShroudMapPrivate::BytesPerPixel);
		State.VisionRect.Reset();
		State.DirtyRect.Reset();
		State.VisibleCells = 0;
		State.ExploredCells = 0;
		State.bFullDirty = true;

		if (bCanCarry && CarriedMemory.IsValidIndex(TeamIndex) && CarriedMemory[TeamIndex].Num() == OldResolution * OldResolution)
		{
			const TArray<uint8>& Source = CarriedMemory[TeamIndex];
			int32 Explored = 0;
			for (int32 Y = 0; Y < Resolution; ++Y)
			{
				const int32 SourceY = (Y * OldResolution) / Resolution;
				for (int32 X = 0; X < Resolution; ++X)
				{
					const int32 SourceX = (X * OldResolution) / Resolution;
					const uint8 Value = Source[SourceY * OldResolution + SourceX];
					State.Memory[Y * Resolution + X] = Value;
					if (Value >= VisibilityThresholdByte)
					{
						++Explored;
					}
				}
			}
			State.ExploredCells = Explored;
		}
	}

	TeamTextures.Reset();
	TeamTextures.SetNum(NumTeams);
	for (int32 TeamIndex = 0; TeamIndex < NumTeams; ++TeamIndex)
	{
		CreateTeamTexture(TeamIndex);
	}

	ViewTeam = FMath::Clamp(ViewTeam, 0, NumTeams - 1);

	MarkAllRevealersDirty();
	bMaterialParametersDirty = true;
}

void UShroudSubsystem::CreateTeamTexture(int32 TeamIndex)
{
	if (!TeamTextures.IsValidIndex(TeamIndex))
	{
		return;
	}

	const FName TextureName(*FString::Printf(TEXT("ShroudMap_Team%d"), TeamIndex));
	UTexture2D* Texture = UTexture2D::CreateTransient(Resolution, Resolution, PF_B8G8R8A8, TextureName);
	if (!Texture)
	{
		UE_LOG(LogShroudMap, Warning, TEXT("Could not create the visibility texture for team %d at %dx%d."), TeamIndex, Resolution, Resolution);
		return;
	}

	// Linear, not sRGB: these bytes are two masks, not a colour. Bilinear so the fog edge is smooth
	// between cells; clamped so a material sampling outside the mapped square gets the rim, not a wrap.
	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->CompressionSettings = TC_VectorDisplacementmap;
	Texture->UpdateResource();

	TeamTextures[TeamIndex] = Texture;
}

//~ Registration ---------------------------------------------------------------------------------------

void UShroudSubsystem::RegisterRevealer(UShroudRevealerComponent* Revealer)
{
	if (!Revealer || Revealers.Contains(Revealer))
	{
		return;
	}

	Revealers.Add(Revealer);
	Footprints.AddDefaulted();
}

void UShroudSubsystem::UnregisterRevealer(UShroudRevealerComponent* Revealer)
{
	const int32 Index = Revealers.IndexOfByKey(Revealer);
	if (Index == INDEX_NONE)
	{
		return;
	}

	// Swap removal on both arrays at once keeps them parallel, which is the whole contract between them.
	Revealers.RemoveAtSwap(Index, EAllowShrinking::No);
	Footprints.RemoveAtSwap(Index, EAllowShrinking::No);

	if (RebuildCursor >= Revealers.Num())
	{
		RebuildCursor = 0;
	}
}

void UShroudSubsystem::RegisterOccluder(UShroudOccluderComponent* Occluder)
{
	if (!Occluder || Occluders.Contains(Occluder))
	{
		return;
	}

	Occluders.Add(Occluder);
	bOccludersDirty = true;
}

void UShroudSubsystem::UnregisterOccluder(UShroudOccluderComponent* Occluder)
{
	if (Occluders.RemoveSingleSwap(Occluder, EAllowShrinking::No) > 0)
	{
		bOccludersDirty = true;
	}
}

void UShroudSubsystem::MarkRevealerDirty(const UShroudRevealerComponent* Revealer)
{
	const int32 Index = Revealers.IndexOfByKey(Revealer);
	if (Footprints.IsValidIndex(Index))
	{
		Footprints[Index].bDirty = true;
	}
}

void UShroudSubsystem::MarkAllRevealersDirty()
{
	for (FShroudFootprint& Footprint : Footprints)
	{
		Footprint.bDirty = true;
	}
}

void UShroudSubsystem::MarkOccludersDirty()
{
	bOccludersDirty = true;
}

//~ Grid helpers ---------------------------------------------------------------------------------------

FVector2D UShroudSubsystem::WorldToCellF(const FVector& WorldLocation) const
{
	return FVector2D(
		(static_cast<float>(WorldLocation.X) - GridMin.X) / CellSize,
		(static_cast<float>(WorldLocation.Y) - GridMin.Y) / CellSize);
}

FIntPoint UShroudSubsystem::WorldToCell(const FVector& WorldLocation) const
{
	const FVector2D CellF = WorldToCellF(WorldLocation);
	return FIntPoint(FMath::FloorToInt(CellF.X), FMath::FloorToInt(CellF.Y));
}

FVector2D UShroudSubsystem::CellToWorld(int32 CellX, int32 CellY) const
{
	return GridMin + FVector2D((CellX + 0.5f) * CellSize, (CellY + 0.5f) * CellSize);
}

int32 UShroudSubsystem::ResolveTeam(int32 Team) const
{
	if (Team < 0)
	{
		return ViewTeam;
	}

	return FMath::Clamp(Team, 0, FMath::Max(0, NumTeams - 1));
}

//~ Queries --------------------------------------------------------------------------------------------

EShroudVisibility UShroudSubsystem::GetVisibilityState(const FVector& WorldLocation, int32 Team) const
{
	const int32 TeamIndex = ResolveTeam(Team);
	if (!TeamStates.IsValidIndex(TeamIndex))
	{
		return EShroudVisibility::Unknown;
	}

	const FIntPoint Cell = WorldToCell(WorldLocation);
	if (!IsCellOnGrid(Cell.X, Cell.Y))
	{
		// Off the map is not "explored by default". A level that spills over the mapped square is a
		// setup mistake, and answering Unknown makes it show up as one instead of hiding it.
		return EShroudVisibility::Unknown;
	}

	const FShroudTeamState& State = TeamStates[TeamIndex];
	const int32 CellIndex = Cell.Y * Resolution + Cell.X;

	if (State.Vision[CellIndex] >= VisibilityThresholdByte)
	{
		return EShroudVisibility::Visible;
	}

	if (bMemoryEnabled && State.Memory[CellIndex] >= VisibilityThresholdByte)
	{
		return EShroudVisibility::Explored;
	}

	return EShroudVisibility::Unknown;
}

bool UShroudSubsystem::IsVisible(const FVector& WorldLocation, int32 Team) const
{
	return GetVisibilityState(WorldLocation, Team) == EShroudVisibility::Visible;
}

bool UShroudSubsystem::IsExplored(const FVector& WorldLocation, int32 Team) const
{
	return GetVisibilityState(WorldLocation, Team) != EShroudVisibility::Unknown;
}

float UShroudSubsystem::GetVisibilityValue(const FVector& WorldLocation, int32 Team) const
{
	const int32 TeamIndex = ResolveTeam(Team);
	const FIntPoint Cell = WorldToCell(WorldLocation);
	if (!TeamStates.IsValidIndex(TeamIndex) || !IsCellOnGrid(Cell.X, Cell.Y))
	{
		return 0.0f;
	}

	return TeamStates[TeamIndex].Vision[Cell.Y * Resolution + Cell.X] / 255.0f;
}

float UShroudSubsystem::GetMemoryValue(const FVector& WorldLocation, int32 Team) const
{
	const int32 TeamIndex = ResolveTeam(Team);
	const FIntPoint Cell = WorldToCell(WorldLocation);
	if (!TeamStates.IsValidIndex(TeamIndex) || !IsCellOnGrid(Cell.X, Cell.Y))
	{
		return 0.0f;
	}

	return TeamStates[TeamIndex].Memory[Cell.Y * Resolution + Cell.X] / 255.0f;
}

UTexture* UShroudSubsystem::GetShroudTexture(int32 Team) const
{
	const int32 TeamIndex = ResolveTeam(Team);
	return TeamTextures.IsValidIndex(TeamIndex) ? TeamTextures[TeamIndex].Get() : nullptr;
}

FVector2D UShroudSubsystem::WorldToShroudUV(const FVector& WorldLocation) const
{
	if (WorldSize <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}

	return FVector2D(
		(static_cast<float>(WorldLocation.X) - GridMin.X) / WorldSize,
		(static_cast<float>(WorldLocation.Y) - GridMin.Y) / WorldSize);
}

//~ Teams and switches ---------------------------------------------------------------------------------

void UShroudSubsystem::SetViewTeam(int32 NewTeam)
{
	NewTeam = FMath::Clamp(NewTeam, 0, FMath::Max(0, NumTeams - 1));
	if (ViewTeam == NewTeam)
	{
		return;
	}

	ViewTeam = NewTeam;
	bMaterialParametersDirty = true;
}

void UShroudSubsystem::SetMemoryEnabled(bool bNewEnabled)
{
	if (bMemoryEnabled == bNewEnabled)
	{
		return;
	}

	bMemoryEnabled = bNewEnabled;

	// Nothing is destroyed either way. Switching memory off stops the remembered channel being drawn and
	// stops IsExplored answering for it; switching it back on brings the same bytes straight back.
	for (FShroudTeamState& State : TeamStates)
	{
		State.bFullDirty = true;
	}

	bMaterialParametersDirty = true;
}

void UShroudSubsystem::SetTerrainOcclusionEnabled(bool bNewEnabled)
{
	if (bTerrainOccludes == bNewEnabled)
	{
		return;
	}

	bTerrainOccludes = bNewEnabled;
	MarkAllRevealersDirty();
}

void UShroudSubsystem::RebuildHeightField()
{
	TerrainHeights.Reset();
	CombinedHeights.Reset();
	HeightFieldCursor = 0;
	bHeightFieldReady = false;
	bOccludersDirty = true;
	MarkAllRevealersDirty();
}

void UShroudSubsystem::SetResolution(int32 NewResolution)
{
	NewResolution = FMath::Clamp(NewResolution, 32, 2048);
	if (NewResolution == Resolution)
	{
		return;
	}

	const int32 OldResolution = Resolution;
	Resolution = NewResolution;
	RebuildGrids(OldResolution, /*bPreserveMemory*/ true);

	UE_LOG(LogShroudMap, Log, TEXT("ShroudMap resolution %d -> %d (%.1f cm per cell); memory carried over."),
		OldResolution, Resolution, CellSize);
}

void UShroudSubsystem::SetWorldBounds(FVector2D NewOrigin, float NewSize)
{
	NewSize = FMath::Max(1000.0f, NewSize);
	if (NewOrigin.Equals(WorldOrigin) && FMath::IsNearlyEqual(NewSize, WorldSize))
	{
		return;
	}

	WorldOrigin = NewOrigin;
	WorldSize = NewSize;

	// Moving the square means the cells no longer line up with what they used to mean, so memory goes
	// and the height field has to be traced again. Both are stated rather than quietly approximated.
	RebuildGrids(0, /*bPreserveMemory*/ false);
	RebuildHeightField();
}

void UShroudSubsystem::SetUpdatesPerSecond(float NewRate)
{
	UpdatesPerSecond = FMath::Max(0.0f, NewRate);
	UpdateAccumulator = 0.0f;
}

//~ Reveal and memory ----------------------------------------------------------------------------------

void UShroudSubsystem::WriteMemoryCell(FShroudTeamState& State, int32 CellIndex, uint8 Value)
{
	uint8& Stored = State.Memory[CellIndex];
	if (Value <= Stored)
	{
		return;
	}

	const bool bWasExplored = Stored >= VisibilityThresholdByte;
	Stored = Value;
	if (!bWasExplored && Stored >= VisibilityThresholdByte)
	{
		++State.ExploredCells;
	}
}

void UShroudSubsystem::RevealCircle(const FVector& Center, float Radius, int32 Team, float SoftEdge, bool bAffectCurrentSight)
{
	if (Radius <= 0.0f || TeamStates.Num() == 0 || CellSize <= 0.0f)
	{
		return;
	}

	const float Soft = SoftEdge < 0.0f ? DefaultSoftEdge : FMath::Clamp(SoftEdge, 0.0f, 1.0f);

	FShroudFootprint Stamp;
	BuildDiscFootprint(WorldToCellF(Center), Radius / CellSize, Soft, 1.0f, Stamp);
	if (Stamp.Cells.Num() == 0)
	{
		return;
	}

	const int32 First = Team < 0 ? 0 : ResolveTeam(Team);
	const int32 Last = Team < 0 ? TeamStates.Num() - 1 : First;

	for (int32 TeamIndex = First; TeamIndex <= Last; ++TeamIndex)
	{
		FShroudTeamState& State = TeamStates[TeamIndex];
		for (const uint32 Packed : Stamp.Cells)
		{
			const int32 CellIndex = static_cast<int32>(Packed >> 8);
			const uint8 Strength = static_cast<uint8>(Packed & 0xFF);

			WriteMemoryCell(State, CellIndex, Strength);
			if (bAffectCurrentSight && Strength > State.Vision[CellIndex])
			{
				State.Vision[CellIndex] = Strength;
			}
		}

		State.DirtyRect.Union(Stamp.Bounds);
		if (bAffectCurrentSight)
		{
			// The sight mask's rectangle is what the next update clears, so a hand-written stamp has to
			// go in it. Otherwise these cells would stay lit for good, which is not what "current" means.
			State.VisionRect.Union(Stamp.Bounds);
		}
	}
}

void UShroudSubsystem::RevealAll(int32 Team)
{
	if (TeamStates.Num() == 0)
	{
		return;
	}

	const int32 First = Team < 0 ? 0 : ResolveTeam(Team);
	const int32 Last = Team < 0 ? TeamStates.Num() - 1 : First;

	for (int32 TeamIndex = First; TeamIndex <= Last; ++TeamIndex)
	{
		FShroudTeamState& State = TeamStates[TeamIndex];
		FMemory::Memset(State.Memory.GetData(), 0xFF, State.Memory.Num());
		State.ExploredCells = State.Memory.Num();
		State.bFullDirty = true;
	}
}

void UShroudSubsystem::ClearMemory(int32 Team)
{
	if (TeamStates.Num() == 0)
	{
		return;
	}

	const int32 First = Team < 0 ? 0 : ResolveTeam(Team);
	const int32 Last = Team < 0 ? TeamStates.Num() - 1 : First;

	for (int32 TeamIndex = First; TeamIndex <= Last; ++TeamIndex)
	{
		FShroudTeamState& State = TeamStates[TeamIndex];
		FMemory::Memzero(State.Memory.GetData(), State.Memory.Num());
		State.ExploredCells = 0;
		State.bFullDirty = true;
	}
}

//~ Height field ---------------------------------------------------------------------------------------

int32 UShroudSubsystem::GatherHeightFieldSlice()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}

	const int32 FieldSize = HeightFieldResolution;
	const int32 Total = FieldSize * FieldSize;

	if (TerrainHeights.Num() != Total)
	{
		TerrainHeights.SetNumUninitialized(Total);
		HeightFieldCursor = 0;
	}

	if (HeightFieldCursor >= Total)
	{
		return 0;
	}

	// Spread over updates on purpose. Sixteen thousand traces in one frame is a visible hitch, and the
	// only thing waiting on them is a feature that was off a moment ago.
	const int32 Count = FMath::Min(MaxHeightFieldTracesPerUpdate, Total - HeightFieldCursor);
	const float SampleStep = WorldSize / static_cast<float>(FieldSize);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ShroudHeightField), /*bTraceComplex*/ false);
	QueryParams.bReturnPhysicalMaterial = false;

	for (int32 Offset = 0; Offset < Count; ++Offset)
	{
		const int32 Index = HeightFieldCursor + Offset;
		const int32 SampleX = Index % FieldSize;
		const int32 SampleY = Index / FieldSize;

		const FVector2D SampleXY = GridMin + FVector2D((SampleX + 0.5f) * SampleStep, (SampleY + 0.5f) * SampleStep);
		const FVector TraceStart(SampleXY.X, SampleXY.Y, HeightTraceStartZ);
		const FVector TraceEnd(SampleXY.X, SampleXY.Y, HeightTraceEndZ);

		FHitResult Hit;
		TerrainHeights[Index] = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, HeightTraceChannel, QueryParams)
			? static_cast<float>(Hit.ImpactPoint.Z)
			: HeightTraceEndZ;
	}

	HeightFieldCursor += Count;
	if (HeightFieldCursor >= Total)
	{
		bOccludersDirty = true;
	}

	return Count;
}

void UShroudSubsystem::StampOccluders()
{
	const int32 FieldSize = HeightFieldResolution;
	const int32 Total = FieldSize * FieldSize;
	if (TerrainHeights.Num() != Total)
	{
		return;
	}

	CombinedHeights = TerrainHeights;
	const float SampleStep = WorldSize / static_cast<float>(FieldSize);

	for (UShroudOccluderComponent* Occluder : Occluders)
	{
		if (!Occluder || !Occluder->bOccluderEnabled)
		{
			continue;
		}

		const FVector Location = Occluder->GetComponentLocation();
		const FVector2D Extent = Occluder->GetPlanarExtent();
		const float TopZ = static_cast<float>(Location.Z) + Occluder->BlockHeight;
		const float RadiusSquared = Occluder->Radius * Occluder->Radius;

		const int32 MinX = FMath::Max(0, FMath::FloorToInt((static_cast<float>(Location.X) - Extent.X - GridMin.X) / SampleStep - 0.5f));
		const int32 MaxX = FMath::Min(FieldSize - 1, FMath::CeilToInt((static_cast<float>(Location.X) + Extent.X - GridMin.X) / SampleStep - 0.5f));
		const int32 MinY = FMath::Max(0, FMath::FloorToInt((static_cast<float>(Location.Y) - Extent.Y - GridMin.Y) / SampleStep - 0.5f));
		const int32 MaxY = FMath::Min(FieldSize - 1, FMath::CeilToInt((static_cast<float>(Location.Y) + Extent.Y - GridMin.Y) / SampleStep - 0.5f));

		for (int32 SampleY = MinY; SampleY <= MaxY; ++SampleY)
		{
			for (int32 SampleX = MinX; SampleX <= MaxX; ++SampleX)
			{
				if (Occluder->Shape == EShroudOccluderShape::Circle)
				{
					const FVector2D SampleXY = GridMin + FVector2D((SampleX + 0.5f) * SampleStep, (SampleY + 0.5f) * SampleStep);
					const float DistanceSquared = FVector2D::DistSquared(SampleXY, FVector2D(Location.X, Location.Y));
					if (DistanceSquared > RadiusSquared)
					{
						continue;
					}
				}

				float& Height = CombinedHeights[SampleY * FieldSize + SampleX];
				Height = FMath::Max(Height, TopZ);
			}
		}

		Occluder->LastStampedLocation = Location;
	}

	bOccludersDirty = false;
}

float UShroudSubsystem::SampleHeightAtCell(float CellX, float CellY) const
{
	const int32 FieldSize = HeightFieldResolution;
	if (CombinedHeights.Num() != FieldSize * FieldSize)
	{
		return HeightTraceEndZ;
	}

	const float FieldX = (CellX / static_cast<float>(Resolution)) * static_cast<float>(FieldSize) - 0.5f;
	const float FieldY = (CellY / static_cast<float>(Resolution)) * static_cast<float>(FieldSize) - 0.5f;

	int32 X0 = FMath::FloorToInt(FieldX);
	int32 Y0 = FMath::FloorToInt(FieldY);
	const float FracX = FieldX - X0;
	const float FracY = FieldY - Y0;

	int32 X1 = X0 + 1;
	int32 Y1 = Y0 + 1;
	X0 = FMath::Clamp(X0, 0, FieldSize - 1);
	Y0 = FMath::Clamp(Y0, 0, FieldSize - 1);
	X1 = FMath::Clamp(X1, 0, FieldSize - 1);
	Y1 = FMath::Clamp(Y1, 0, FieldSize - 1);

	const float H00 = CombinedHeights[Y0 * FieldSize + X0];
	const float H10 = CombinedHeights[Y0 * FieldSize + X1];
	const float H01 = CombinedHeights[Y1 * FieldSize + X0];
	const float H11 = CombinedHeights[Y1 * FieldSize + X1];

	return FMath::Lerp(FMath::Lerp(H00, H10, FracX), FMath::Lerp(H01, H11, FracX), FracY);
}

//~ Footprints -----------------------------------------------------------------------------------------

void UShroudSubsystem::BuildDiscFootprint(const FVector2D& CenterCell, float RadiusCells, float SoftEdge, float Strength, FShroudFootprint& Footprint)
{
	const float Inner = RadiusCells * (1.0f - SoftEdge);
	const float FalloffSpan = FMath::Max(RadiusCells - Inner, UE_KINDA_SMALL_NUMBER);
	const float RadiusSquared = RadiusCells * RadiusCells;

	const int32 MinX = FMath::Max(0, FMath::FloorToInt(CenterCell.X - RadiusCells));
	const int32 MaxX = FMath::Min(Resolution - 1, FMath::CeilToInt(CenterCell.X + RadiusCells));
	const int32 MinY = FMath::Max(0, FMath::FloorToInt(CenterCell.Y - RadiusCells));
	const int32 MaxY = FMath::Min(Resolution - 1, FMath::CeilToInt(CenterCell.Y + RadiusCells));

	Footprint.Cells.Reserve((MaxX - MinX + 1) * (MaxY - MinY + 1));

	for (int32 CellY = MinY; CellY <= MaxY; ++CellY)
	{
		const float DeltaY = (CellY + 0.5f) - CenterCell.Y;
		for (int32 CellX = MinX; CellX <= MaxX; ++CellX)
		{
			const float DeltaX = (CellX + 0.5f) - CenterCell.X;
			const float DistanceSquared = DeltaX * DeltaX + DeltaY * DeltaY;
			if (DistanceSquared > RadiusSquared)
			{
				continue;
			}

			const float Distance = FMath::Sqrt(DistanceSquared);
			const float Alpha = Distance <= Inner ? 1.0f : 1.0f - (Distance - Inner) / FalloffSpan;

			const int32 Byte = FMath::RoundToInt(ShroudMapPrivate::SmoothFalloff(Alpha) * Strength * 255.0f);
			if (Byte <= 0)
			{
				continue;
			}

			const int32 CellIndex = CellY * Resolution + CellX;
			Footprint.Cells.Add((static_cast<uint32>(CellIndex) << 8) | static_cast<uint32>(FMath::Min(Byte, 255)));
			Footprint.Bounds.Include(CellX, CellY);
		}
	}
}

void UShroudSubsystem::BuildOccludedFootprint(const UShroudRevealerComponent& Revealer, const FVector2D& CenterCell, float RadiusCells, float SoftEdge, float Strength, FShroudFootprint& Footprint)
{
	const int32 MinX = FMath::Max(0, FMath::FloorToInt(CenterCell.X - RadiusCells));
	const int32 MaxX = FMath::Min(Resolution - 1, FMath::CeilToInt(CenterCell.X + RadiusCells));
	const int32 MinY = FMath::Max(0, FMath::FloorToInt(CenterCell.Y - RadiusCells));
	const int32 MaxY = FMath::Min(Resolution - 1, FMath::CeilToInt(CenterCell.Y + RadiusCells));

	const int32 ScratchWidth = MaxX - MinX + 1;
	const int32 ScratchHeight = MaxY - MinY + 1;
	if (ScratchWidth <= 0 || ScratchHeight <= 0)
	{
		return;
	}

	// One reused buffer for every revealer in the world, so a rebuild allocates nothing at all.
	FootprintScratch.SetNumUninitialized(ScratchWidth * ScratchHeight, EAllowShrinking::No);
	FMemory::Memzero(FootprintScratch.GetData(), FootprintScratch.Num());

	// A revealer whose actor sits below the ground it stands on would shadow itself, so the eye is
	// lifted to whichever is higher: the component, or the height field under it.
	const float GroundZ = SampleHeightAtCell(CenterCell.X, CenterCell.Y);
	const float EyeZ = FMath::Max(static_cast<float>(Revealer.GetComponentLocation().Z), GroundZ) + Revealer.EyeHeight;

	const float Inner = RadiusCells * (1.0f - SoftEdge);
	const float FalloffSpan = FMath::Max(RadiusCells - Inner, UE_KINDA_SMALL_NUMBER);

	// Rays scale with the circumference, not with a fixed count: a big circle needs more of them for the
	// same gap at the rim, and a small one would only be paying for overlap.
	const int32 NumRays = FMath::Clamp(
		FMath::CeilToInt(2.0f * UE_PI * RadiusCells * RaysPerRimCell),
		MinRaysPerRevealer,
		MaxRaysPerRevealer);

	const float AngleStep = 2.0f * UE_PI / static_cast<float>(NumRays);
	int32 Samples = 0;

	for (int32 RayIndex = 0; RayIndex < NumRays; ++RayIndex)
	{
		const float Angle = AngleStep * RayIndex;
		const float DirX = FMath::Cos(Angle);
		const float DirY = FMath::Sin(Angle);

		// The horizon so far along this ray. Ground that does not clear it is behind something.
		float MaxSlope = -TNumericLimits<float>::Max();

		for (float Travel = ShroudMapPrivate::RayStepCells; Travel <= RadiusCells; Travel += ShroudMapPrivate::RayStepCells)
		{
			const float PointX = CenterCell.X + DirX * Travel;
			const float PointY = CenterCell.Y + DirY * Travel;

			const int32 CellX = FMath::FloorToInt(PointX);
			const int32 CellY = FMath::FloorToInt(PointY);
			if (!IsCellOnGrid(CellX, CellY))
			{
				break;
			}

			const float Height = SampleHeightAtCell(PointX, PointY);
			++Samples;

			const float DistanceWorld = Travel * CellSize;
			const float Slope = (Height - EyeZ) / FMath::Max(DistanceWorld, UE_KINDA_SMALL_NUMBER);

			const bool bLit = Travel <= NearFieldCells || Slope + HorizonSlack >= MaxSlope;
			MaxSlope = FMath::Max(MaxSlope, Slope);

			if (!bLit)
			{
				continue;
			}

			const float Alpha = Travel <= Inner ? 1.0f : 1.0f - (Travel - Inner) / FalloffSpan;
			const int32 Byte = FMath::RoundToInt(ShroudMapPrivate::SmoothFalloff(Alpha) * Strength * 255.0f);
			if (Byte <= 0)
			{
				continue;
			}

			uint8& Scratch = FootprintScratch[(CellY - MinY) * ScratchWidth + (CellX - MinX)];
			Scratch = FMath::Max(Scratch, static_cast<uint8>(FMath::Min(Byte, 255)));
		}
	}

	OcclusionSamplesThisUpdate += Samples;

	Footprint.Cells.Reserve(ScratchWidth * ScratchHeight);
	for (int32 ScratchY = 0; ScratchY < ScratchHeight; ++ScratchY)
	{
		for (int32 ScratchX = 0; ScratchX < ScratchWidth; ++ScratchX)
		{
			const uint8 Value = FootprintScratch[ScratchY * ScratchWidth + ScratchX];
			if (Value == 0)
			{
				continue;
			}

			const int32 CellX = MinX + ScratchX;
			const int32 CellY = MinY + ScratchY;
			const int32 CellIndex = CellY * Resolution + CellX;
			Footprint.Cells.Add((static_cast<uint32>(CellIndex) << 8) | static_cast<uint32>(Value));
			Footprint.Bounds.Include(CellX, CellY);
		}
	}
}

void UShroudSubsystem::BuildFootprint(const UShroudRevealerComponent& Revealer, FShroudFootprint& Footprint, bool bUseOcclusion)
{
	Footprint.Cells.Reset();
	Footprint.Bounds.Reset();

	const FVector Eye = Revealer.GetEyeLocation();
	const float SoftEdge = Revealer.GetEffectiveSoftEdge();
	const float Strength = FMath::Clamp(Revealer.Strength, 0.0f, 1.0f);
	const float RadiusCells = CellSize > 0.0f ? Revealer.SightRadius / CellSize : 0.0f;

	if (RadiusCells > 0.0f && Strength > 0.0f)
	{
		const FVector2D CenterCell = WorldToCellF(Eye);
		if (bUseOcclusion)
		{
			BuildOccludedFootprint(Revealer, CenterCell, RadiusCells, SoftEdge, Strength, Footprint);
		}
		else
		{
			BuildDiscFootprint(CenterCell, RadiusCells, SoftEdge, Strength, Footprint);
		}
	}

	Footprint.BuiltAtEye = Eye;
	Footprint.BuiltRadius = Revealer.SightRadius;
	Footprint.BuiltSoftEdge = SoftEdge;
	Footprint.BuiltStrength = Strength;
	Footprint.BuiltTeam = FMath::Clamp(Revealer.Team, 0, FMath::Max(0, NumTeams - 1));
	Footprint.bBuiltWithOcclusion = bUseOcclusion;
	Footprint.bDirty = false;
	Footprint.bValid = true;
}

void UShroudSubsystem::UpdateFootprints()
{
	const double StartTime = FPlatformTime::Seconds();

	FootprintRebuildsThisUpdate = 0;
	FootprintRebuildsDeferredThisUpdate = 0;

	const int32 NumRevealers = Revealers.Num();
	if (NumRevealers == 0)
	{
		FootprintSecondsThisUpdate = FPlatformTime::Seconds() - StartTime;
		return;
	}

	const bool bOcclusionActive = bTerrainOccludes && bHeightFieldReady;

	// Two budgets, because the two paths are two orders of magnitude apart in cost. See the settings.
	const int32 ConfiguredBudget = bOcclusionActive ? MaxOccludedFootprintRebuildsPerUpdate : MaxFootprintRebuildsPerUpdate;
	const int32 Budget = ConfiguredBudget <= 0 ? MAX_int32 : ConfiguredBudget;

	if (RebuildCursor >= NumRevealers)
	{
		RebuildCursor = 0;
	}

	const float MoveTolerance = RebuildMoveTolerance * CellSize;
	const float MoveToleranceSquared = MoveTolerance * MoveTolerance;

	int32 NextCursor = RebuildCursor;

	for (int32 Step = 0; Step < NumRevealers; ++Step)
	{
		const int32 Index = (RebuildCursor + Step) % NumRevealers;

		UShroudRevealerComponent* Revealer = Revealers[Index];
		FShroudFootprint& Footprint = Footprints[Index];

		if (!Revealer || !Revealer->IsRevealing())
		{
			// Keeping the entry but emptying it means switching a revealer back on is one rebuild, not a
			// re-registration, and nothing downstream has to know the difference.
			Footprint.Cells.Reset();
			Footprint.Bounds.Reset();
			Footprint.bValid = false;
			Footprint.bDirty = true;
			continue;
		}

		const bool bUseOcclusion = bOcclusionActive && !Revealer->bIgnoreTerrainOcclusion;
		const float SoftEdge = Revealer->GetEffectiveSoftEdge();
		const float Strength = FMath::Clamp(Revealer->Strength, 0.0f, 1.0f);
		const int32 TeamIndex = FMath::Clamp(Revealer->Team, 0, FMath::Max(0, NumTeams - 1));

		bool bNeedsRebuild =
			Footprint.bDirty
			|| !Footprint.bValid
			|| Footprint.bBuiltWithOcclusion != bUseOcclusion
			|| Footprint.BuiltTeam != TeamIndex
			|| !FMath::IsNearlyEqual(Footprint.BuiltRadius, Revealer->SightRadius)
			|| !FMath::IsNearlyEqual(Footprint.BuiltSoftEdge, SoftEdge)
			|| !FMath::IsNearlyEqual(Footprint.BuiltStrength, Strength);

		if (!bNeedsRebuild)
		{
			// A drift of less than half a cell cannot move a single pixel, so it is not worth a rebuild.
			bNeedsRebuild = FVector::DistSquared(Revealer->GetEyeLocation(), Footprint.BuiltAtEye) > MoveToleranceSquared;
		}

		if (!bNeedsRebuild)
		{
			continue;
		}

		if (FootprintRebuildsThisUpdate >= Budget)
		{
			// Over budget: this revealer stamps the shape it had last update. The fog edge lags; nothing
			// blinks out, and the cursor guarantees this one is served before the ones behind it.
			Footprint.bDirty = true;
			++FootprintRebuildsDeferredThisUpdate;
			continue;
		}

		BuildFootprint(*Revealer, Footprint, bUseOcclusion);
		++FootprintRebuildsThisUpdate;
		NextCursor = (Index + 1) % NumRevealers;
	}

	RebuildCursor = NextCursor;
	FootprintSecondsThisUpdate = FPlatformTime::Seconds() - StartTime;
}

//~ Compositing ----------------------------------------------------------------------------------------

void UShroudSubsystem::CompositeTeams()
{
	const double StartTime = FPlatformTime::Seconds();

	const int32 NumStates = TeamStates.Num();
	if (NumStates == 0)
	{
		return;
	}

	TArray<FShroudCellRect, TInlineAllocator<8>> ClearRects;
	ClearRects.SetNum(NumStates);

	for (int32 TeamIndex = 0; TeamIndex < NumStates; ++TeamIndex)
	{
		FShroudTeamState& State = TeamStates[TeamIndex];

		// Only last update's rectangle is cleared. Outside it the sight mask is known to be zero without
		// looking, which is what keeps a megabyte of grid off the per-update bill.
		ClearRects[TeamIndex] = State.VisionRect;
		if (!State.VisionRect.IsEmpty())
		{
			const int32 RowBytes = State.VisionRect.Width();
			for (int32 Row = State.VisionRect.MinY; Row <= State.VisionRect.MaxY; ++Row)
			{
				FMemory::Memzero(State.Vision.GetData() + Row * Resolution + State.VisionRect.MinX, RowBytes);
			}
		}

		State.VisionRect.Reset();
		State.VisibleCells = 0;

		if (State.bFullDirty)
		{
			State.DirtyRect = FShroudCellRect::WholeGrid(Resolution);
			State.bFullDirty = false;
		}
	}

	ActiveRevealersThisUpdate = 0;
	int32 CachedFootprintCells = 0;

	auto StampFootprint = [this](FShroudTeamState& State, const FShroudFootprint& Footprint)
	{
		uint8* VisionData = State.Vision.GetData();
		for (const uint32 Packed : Footprint.Cells)
		{
			const int32 CellIndex = static_cast<int32>(Packed >> 8);
			const uint8 Strength = static_cast<uint8>(Packed & 0xFF);
			if (Strength > VisionData[CellIndex])
			{
				VisionData[CellIndex] = Strength;
			}
		}
		State.VisionRect.Union(Footprint.Bounds);
	};

	// Pass one: everything that leaves a trace behind. These are folded into memory below.
	for (int32 Index = 0; Index < Revealers.Num(); ++Index)
	{
		const UShroudRevealerComponent* Revealer = Revealers[Index];
		const FShroudFootprint& Footprint = Footprints[Index];
		CachedFootprintCells += Footprint.Cells.Num();

		if (!Revealer || !Footprint.bValid || Footprint.Cells.Num() == 0 || !Revealer->IsRevealing())
		{
			continue;
		}

		++ActiveRevealersThisUpdate;
		if (!Revealer->bLeavesMemory)
		{
			continue;
		}

		const int32 TeamIndex = FMath::Clamp(Revealer->Team, 0, NumStates - 1);
		StampFootprint(TeamStates[TeamIndex], Footprint);
	}

	TArray<FShroudCellRect, TInlineAllocator<8>> MemoryRects;
	MemoryRects.SetNum(NumStates);

	for (int32 TeamIndex = 0; TeamIndex < NumStates; ++TeamIndex)
	{
		FShroudTeamState& State = TeamStates[TeamIndex];

		FShroudCellRect Rect = ClearRects[TeamIndex];
		Rect.Union(State.VisionRect);
		Rect.Union(State.DirtyRect);
		Rect.ClampToGrid(Resolution);
		MemoryRects[TeamIndex] = Rect;

		if (!bMemoryEnabled || Rect.IsEmpty())
		{
			continue;
		}

		const uint8* VisionData = State.Vision.GetData();
		for (int32 Row = Rect.MinY; Row <= Rect.MaxY; ++Row)
		{
			const int32 RowStart = Row * Resolution;
			for (int32 Column = Rect.MinX; Column <= Rect.MaxX; ++Column)
			{
				const int32 CellIndex = RowStart + Column;
				WriteMemoryCell(State, CellIndex, VisionData[CellIndex]);
			}
		}
	}

	// Pass two: revealers that light without mapping - a radar sweep, a searchlight. They go in after the
	// memory fold, which is the whole of what "does not leave a trace" costs to implement.
	for (int32 Index = 0; Index < Revealers.Num(); ++Index)
	{
		const UShroudRevealerComponent* Revealer = Revealers[Index];
		const FShroudFootprint& Footprint = Footprints[Index];

		if (!Revealer || Revealer->bLeavesMemory || !Footprint.bValid || Footprint.Cells.Num() == 0 || !Revealer->IsRevealing())
		{
			continue;
		}

		const int32 TeamIndex = FMath::Clamp(Revealer->Team, 0, NumStates - 1);
		StampFootprint(TeamStates[TeamIndex], Footprint);
	}

	CompositeSecondsThisUpdate = FPlatformTime::Seconds() - StartTime;

	// Pass three: pack the rectangle that changed and hand it over. One upload per team, and only for
	// the teams that actually moved.
	const double UploadStart = FPlatformTime::Seconds();
	TextureUploadsThisUpdate = 0;
	UploadedBytesThisUpdate = 0;

	for (int32 TeamIndex = 0; TeamIndex < NumStates; ++TeamIndex)
	{
		FShroudTeamState& State = TeamStates[TeamIndex];

		FShroudCellRect Rect = MemoryRects[TeamIndex];
		Rect.Union(State.VisionRect);
		Rect.ClampToGrid(Resolution);

		if (!Rect.IsEmpty())
		{
			const uint8* VisionData = State.Vision.GetData();
			const uint8* MemoryData = State.Memory.GetData();
			uint8* PixelData = State.Pixels.GetData();

			int32 VisibleInRect = 0;
			for (int32 Row = Rect.MinY; Row <= Rect.MaxY; ++Row)
			{
				const int32 RowStart = Row * Resolution;
				for (int32 Column = Rect.MinX; Column <= Rect.MaxX; ++Column)
				{
					const int32 CellIndex = RowStart + Column;
					const uint8 Sight = VisionData[CellIndex];
					if (Sight >= VisibilityThresholdByte)
					{
						++VisibleInRect;
					}

					// Memory switched off shows current sight in both channels, so explored ground falls
					// back to black on screen exactly as the queries say it has.
					const uint8 Remembered = bMemoryEnabled ? MemoryData[CellIndex] : Sight;

					uint8* Pixel = PixelData + CellIndex * ShroudMapPrivate::BytesPerPixel;
					Pixel[0] = 0;			// B - free for whatever a project wants next
					Pixel[1] = Remembered;	// G - memory
					Pixel[2] = Sight;		// R - current sight
					Pixel[3] = 255;			// A
				}
			}

			// Everything outside the rectangle is zero in the sight mask by construction, so counting
			// inside it is the whole count and not an approximation of one.
			State.VisibleCells = VisibleInRect;

			UploadTeamTexture(TeamIndex, Rect);
		}

		State.DirtyRect.Reset();
	}

	UploadSecondsThisUpdate = FPlatformTime::Seconds() - UploadStart;
	Stats.FootprintCells = CachedFootprintCells;
}

void UShroudSubsystem::UploadTeamTexture(int32 TeamIndex, const FShroudCellRect& Rect)
{
	if (Rect.IsEmpty() || !TeamTextures.IsValidIndex(TeamIndex) || !TeamStates.IsValidIndex(TeamIndex))
	{
		return;
	}

	UTexture2D* Texture = TeamTextures[TeamIndex];
	if (!Texture || !Texture->GetResource())
	{
		return;
	}

	// The texture has to match the grid this rectangle was measured in, and the rectangle has to fit
	// inside the texture. Neither is free: SetResolution() rebuilds the grids and creates new textures,
	// and between those two steps a rectangle from the NEW grid can meet the OLD texture.
	//
	// Measured on 21.08.2026: pressing the demo's resolution button took the grid to 1024 while
	// TeamTextures still held the 512 one, and the upload went in with DestX 61, Width 868 against a
	// 512-wide texture. RHICommandList.h asserts on that ("UpdateTexture2D out of bounds on X") and the
	// editor dies on the spot - no log line, no recoverable error, in the middle of Phase 3.
	//
	// Skipping is correct rather than clamping: RebuildGrids() sets bFullDirty, so the very next update
	// re-uploads the whole map into the matching texture. Clamping would instead paint half a frame of
	// the new grid into the old texture and look like a rendering bug.
	const int32 TextureWidth = static_cast<int32>(Texture->GetSizeX());
	const int32 TextureHeight = static_cast<int32>(Texture->GetSizeY());
	if (TextureWidth != Resolution || TextureHeight != Resolution)
	{
		UE_LOG(LogShroudMap, Verbose,
			TEXT("Skipping a team %d upload: texture is %dx%d but the grid is %dx%d. The next update carries it."),
			TeamIndex, TextureWidth, TextureHeight, Resolution, Resolution);
		return;
	}

	if (Rect.MinX < 0 || Rect.MinY < 0 || Rect.MaxX >= Resolution || Rect.MaxY >= Resolution)
	{
		UE_LOG(LogShroudMap, Warning,
			TEXT("Refusing a team %d upload outside the grid: (%d,%d)-(%d,%d) in %dx%d."),
			TeamIndex, Rect.MinX, Rect.MinY, Rect.MaxX, Rect.MaxY, Resolution, Resolution);
		return;
	}

	const FShroudTeamState& State = TeamStates[TeamIndex];
	if (State.Pixels.Num() != Resolution * Resolution * ShroudMapPrivate::BytesPerPixel)
	{
		// The pixel buffer belongs to a different grid than the one we are about to read with. Reading it
		// would run off the end of the array before the RHI ever saw the region.
		return;
	}

	const int32 RectWidth = Rect.Width();
	const int32 RectHeight = Rect.Height();
	const int32 RowBytes = RectWidth * ShroudMapPrivate::BytesPerPixel;
	const int32 TotalBytes = RowBytes * RectHeight;

	// The staging copy is tightly packed and owned by the render command, because the buffer it came
	// from is overwritten on the next update and the upload has not necessarily happened by then.
	uint8* Payload = static_cast<uint8*>(FMemory::Malloc(TotalBytes));
	for (int32 Row = 0; Row < RectHeight; ++Row)
	{
		const int32 SourceOffset = ((Rect.MinY + Row) * Resolution + Rect.MinX) * ShroudMapPrivate::BytesPerPixel;
		FMemory::Memcpy(Payload + Row * RowBytes, State.Pixels.GetData() + SourceOffset, RowBytes);
	}

	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(
		static_cast<uint32>(Rect.MinX),
		static_cast<uint32>(Rect.MinY),
		0, 0,
		static_cast<uint32>(RectWidth),
		static_cast<uint32>(RectHeight));

	Texture->UpdateTextureRegions(
		0, 1, Region,
		static_cast<uint32>(RowBytes),
		static_cast<uint32>(ShroudMapPrivate::BytesPerPixel),
		Payload,
		[](uint8* InPayload, const FUpdateTextureRegion2D* InRegions)
		{
			FMemory::Free(InPayload);
			delete InRegions;
		});

	++TextureUploadsThisUpdate;
	UploadedBytesThisUpdate += TotalBytes;
}

void UShroudSubsystem::PublishMaterialParameters()
{
	if (!bMaterialParametersDirty || !ResolvedParameterCollection)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UShroudSettings& Settings = UShroudSettings::Get();
	const float InverseSize = WorldSize > 0.0f ? 1.0f / WorldSize : 0.0f;

	using namespace ShroudMapPrivate;
	UKismetMaterialLibrary::SetVectorParameterValue(World, ResolvedParameterCollection, ParamName_Bounds,
		FLinearColor(GridMin.X, GridMin.Y, WorldSize, InverseSize));
	UKismetMaterialLibrary::SetVectorParameterValue(World, ResolvedParameterCollection, ParamName_UnknownColor, Settings.UnknownColor);
	UKismetMaterialLibrary::SetVectorParameterValue(World, ResolvedParameterCollection, ParamName_ExploredColor, Settings.ExploredColor);
	UKismetMaterialLibrary::SetVectorParameterValue(World, ResolvedParameterCollection, ParamName_VisibleColor, Settings.VisibleColor);
	UKismetMaterialLibrary::SetScalarParameterValue(World, ResolvedParameterCollection, ParamName_Resolution, static_cast<float>(Resolution));
	UKismetMaterialLibrary::SetScalarParameterValue(World, ResolvedParameterCollection, ParamName_MemoryEnabled, bMemoryEnabled ? 1.0f : 0.0f);

	bMaterialParametersDirty = false;
}

//~ Tick -----------------------------------------------------------------------------------------------

void UShroudSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (TeamStates.Num() == 0 || !GetWorld())
	{
		return;
	}

	UpdateTestRevealers(DeltaTime);

	if (UpdatesPerSecond > 0.0f)
	{
		const float Interval = 1.0f / UpdatesPerSecond;
		UpdateAccumulator += DeltaTime;
		if (UpdateAccumulator < Interval)
		{
			return;
		}

		// Never carry more than one interval forward: a hitch must not buy the fog a burst of catch-up
		// updates it has no use for.
		UpdateAccumulator = FMath::Min(UpdateAccumulator - Interval, Interval);
	}

	const double UpdateStart = FPlatformTime::Seconds();

	OcclusionSamplesThisUpdate = 0;
	HeightFieldTracesThisUpdate = 0;

	if (bTerrainOccludes)
	{
		HeightFieldTracesThisUpdate = GatherHeightFieldSlice();

		const int32 FieldTotal = HeightFieldResolution * HeightFieldResolution;
		if (HeightFieldCursor >= FieldTotal && TerrainHeights.Num() == FieldTotal)
		{
			// An occluder that moved since the last stamp is caught here rather than by a per-occluder
			// notification, because a handful of comparisons per update is cheaper than the bookkeeping.
			for (const UShroudOccluderComponent* Occluder : Occluders)
			{
				if (Occluder && !Occluder->GetComponentLocation().Equals(Occluder->LastStampedLocation, 1.0))
				{
					bOccludersDirty = true;
					break;
				}
			}

			if (bOccludersDirty || CombinedHeights.Num() != FieldTotal)
			{
				StampOccluders();
				MarkAllRevealersDirty();
			}

			if (!bHeightFieldReady)
			{
				bHeightFieldReady = true;
				MarkAllRevealersDirty();
				UE_LOG(LogShroudMap, Log, TEXT("ShroudMap height field ready: %dx%d samples, %d occluders stamped."),
					HeightFieldResolution, HeightFieldResolution, Occluders.Num());
			}
		}
	}
	else if (bHeightFieldReady)
	{
		bHeightFieldReady = false;
	}

	UpdateFootprints();
	CompositeTeams();
	PublishMaterialParameters();

	//~ Publish ------------------------------------------------------------------------------------------

	const FShroudTeamState* ViewState = TeamStates.IsValidIndex(ViewTeam) ? &TeamStates[ViewTeam] : nullptr;
	const int32 FieldTotal = HeightFieldResolution * HeightFieldResolution;

	Stats.Teams = TeamStates.Num();
	Stats.ViewTeam = ViewTeam;
	Stats.Resolution = Resolution;
	Stats.TotalCells = Resolution * Resolution;
	Stats.CellSize = CellSize;
	Stats.Revealers = Revealers.Num();
	Stats.ActiveRevealers = ActiveRevealersThisUpdate;
	Stats.Occluders = Occluders.Num();
	Stats.VisibleCells = ViewState ? ViewState->VisibleCells : 0;
	Stats.ExploredCells = ViewState ? (bMemoryEnabled ? ViewState->ExploredCells : ViewState->VisibleCells) : 0;
	Stats.TextureUploads = TextureUploadsThisUpdate;
	Stats.UploadedBytes = UploadedBytesThisUpdate;
	Stats.FootprintRebuilds = FootprintRebuildsThisUpdate;
	Stats.FootprintRebuildsDeferred = FootprintRebuildsDeferredThisUpdate;
	Stats.OcclusionSamples = OcclusionSamplesThisUpdate;
	Stats.FootprintMilliseconds = static_cast<float>(FootprintSecondsThisUpdate * 1000.0);
	Stats.CompositeMilliseconds = static_cast<float>(CompositeSecondsThisUpdate * 1000.0);
	Stats.UploadMilliseconds = static_cast<float>(UploadSecondsThisUpdate * 1000.0);
	Stats.UpdateMilliseconds = static_cast<float>((FPlatformTime::Seconds() - UpdateStart) * 1000.0);
	Stats.bMemoryEnabled = bMemoryEnabled;
	Stats.bTerrainOcclusionEnabled = bTerrainOccludes;
	Stats.HeightFieldResolution = HeightFieldResolution;
	Stats.HeightFieldProgress = FieldTotal > 0 ? FMath::Clamp(static_cast<float>(HeightFieldCursor) / static_cast<float>(FieldTotal), 0.0f, 1.0f) : 0.0f;
	Stats.HeightFieldTraces = HeightFieldTracesThisUpdate;
	Stats.UpdatesPerSecond = UpdatesPerSecond;
	++Stats.UpdateCount;
}

//~ Test revealers -------------------------------------------------------------------------------------

int32 UShroudSubsystem::SpawnTestRevealers(int32 Count, const FVector& Origin, float RingRadius, float SightRadius)
{
	DestroyTestRevealers();

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || Count <= 0)
	{
		return 0;
	}

	Count = FMath::Min(Count, ShroudMapPrivate::MaxTestRevealers);
	TestRingOrigin = Origin;
	TestRingRadius = FMath::Max(0.0f, RingRadius);

	TestRevealers.Reserve(Count);
	TestRevealerPhases.Reserve(Count);

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Phase = 2.0f * UE_PI * Index / static_cast<float>(Count);

		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
		if (!Actor)
		{
			continue;
		}

		UShroudRevealerComponent* Revealer = NewObject<UShroudRevealerComponent>(Actor);
		Revealer->SightRadius = SightRadius;
		Revealer->Team = Index % FMath::Max(1, NumTeams);
		Actor->SetRootComponent(Revealer);
		Revealer->RegisterComponent();

		Actor->SetActorLocation(TestRingOrigin + FVector(FMath::Cos(Phase) * TestRingRadius, FMath::Sin(Phase) * TestRingRadius, 0.0f));

		TestRevealers.Add(Actor);
		TestRevealerPhases.Add(Phase);
	}

	UE_LOG(LogShroudMap, Display, TEXT("Shroud.Test: %d revealers orbiting at %.0f cm with %.0f cm sight."),
		TestRevealers.Num(), TestRingRadius, SightRadius);

	return TestRevealers.Num();
}

void UShroudSubsystem::UpdateTestRevealers(float DeltaSeconds)
{
	if (TestRevealers.Num() == 0)
	{
		return;
	}

	// A still frame proves nothing. These keep moving so the fog is visibly being uncovered while the
	// statistics box next to it stays at one upload per team.
	const float AngularSpeed = 0.35f;

	for (int32 Index = 0; Index < TestRevealers.Num(); ++Index)
	{
		AActor* Actor = TestRevealers[Index];
		if (!Actor)
		{
			continue;
		}

		float& Phase = TestRevealerPhases[Index];
		Phase = FMath::Fmod(Phase + AngularSpeed * DeltaSeconds, 2.0f * UE_PI);

		Actor->SetActorLocation(TestRingOrigin + FVector(FMath::Cos(Phase) * TestRingRadius, FMath::Sin(Phase) * TestRingRadius, 0.0f));
	}
}

void UShroudSubsystem::DestroyTestRevealers()
{
	for (AActor* Actor : TestRevealers)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}

	TestRevealers.Reset();
	TestRevealerPhases.Reset();
}

//~ Stats ----------------------------------------------------------------------------------------------

void UShroudSubsystem::LogStats() const
{
	UE_LOG(LogShroudMap, Display, TEXT("--- ShroudMap ---"));
	UE_LOG(LogShroudMap, Display, TEXT("  Teams              %d (view team %d)"), Stats.Teams, Stats.ViewTeam);
	UE_LOG(LogShroudMap, Display, TEXT("  Resolution         %d x %d (%.1f cm per cell)"), Stats.Resolution, Stats.Resolution, Stats.CellSize);
	UE_LOG(LogShroudMap, Display, TEXT("  Revealers          %d (%d active)"), Stats.Revealers, Stats.ActiveRevealers);
	UE_LOG(LogShroudMap, Display, TEXT("  Occluders          %d"), Stats.Occluders);
	UE_LOG(LogShroudMap, Display, TEXT("  Visible cells      %d"), Stats.VisibleCells);
	UE_LOG(LogShroudMap, Display, TEXT("  Explored cells     %d / %d"), Stats.ExploredCells, Stats.TotalCells);
	UE_LOG(LogShroudMap, Display, TEXT("  Texture uploads    %d (%d bytes)"), Stats.TextureUploads, Stats.UploadedBytes);
	UE_LOG(LogShroudMap, Display, TEXT("  Footprint rebuilds %d (%d deferred, %d cached cells)"), Stats.FootprintRebuilds, Stats.FootprintRebuildsDeferred, Stats.FootprintCells);
	UE_LOG(LogShroudMap, Display, TEXT("  Occlusion samples  %d"), Stats.OcclusionSamples);
	UE_LOG(LogShroudMap, Display, TEXT("  Update             %.3f ms (footprints %.3f, composite %.3f, upload %.3f)"),
		Stats.UpdateMilliseconds, Stats.FootprintMilliseconds, Stats.CompositeMilliseconds, Stats.UploadMilliseconds);
	UE_LOG(LogShroudMap, Display, TEXT("  Memory             %s"), Stats.bMemoryEnabled ? TEXT("on") : TEXT("off"));
	UE_LOG(LogShroudMap, Display, TEXT("  Terrain occlusion  %s (height field %d, %.0f%% gathered)"),
		Stats.bTerrainOcclusionEnabled ? TEXT("on") : TEXT("off"), Stats.HeightFieldResolution, Stats.HeightFieldProgress * 100.0f);
}

//~ Console commands -----------------------------------------------------------------------------------

namespace ShroudMapPrivate
{
	static FAutoConsoleCommandWithWorldAndArgs CmdTest(
		TEXT("Shroud.Test"),
		TEXT("Shroud.Test [Count] [RingRadius] [SightRadius] - put orbiting revealers on the map. 0 removes them."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UShroudSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogShroudMap, Warning, TEXT("Shroud.Test: no ShroudMap subsystem in this world."));
				return;
			}

			const int32 Count = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 0, MaxTestRevealers) : 200;
			const float RingRadius = Args.Num() > 1 ? FMath::Max(0.0f, FCString::Atof(*Args[1])) : 12000.0f;
			const float SightRadius = Args.Num() > 2 ? FMath::Max(1.0f, FCString::Atof(*Args[2])) : 2500.0f;

			const int32 Spawned = Subsystem->SpawnTestRevealers(Count, ResolveCommandOrigin(World), RingRadius, SightRadius);
			UE_LOG(LogShroudMap, Display,
				TEXT("Shroud.Test: %d revealers. Watch the texture upload count on the stats box - it does not move."),
				Spawned);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdReveal(
		TEXT("Shroud.Reveal"),
		TEXT("Shroud.Reveal [Radius|all] [Team] - reveal a circle at the viewpoint, or the whole map."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UShroudSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogShroudMap, Warning, TEXT("Shroud.Reveal: no ShroudMap subsystem in this world."));
				return;
			}

			const int32 Team = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : -1;

			if (Args.Num() > 0 && Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase))
			{
				Subsystem->RevealAll(Team);
				UE_LOG(LogShroudMap, Display, TEXT("Shroud.Reveal: whole map revealed."));
				return;
			}

			const float Radius = Args.Num() > 0 ? FMath::Max(1.0f, FCString::Atof(*Args[0])) : 5000.0f;
			Subsystem->RevealCircle(ResolveCommandOrigin(World), Radius, Team);
			UE_LOG(LogShroudMap, Display, TEXT("Shroud.Reveal: %.0f cm circle revealed."), Radius);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdClear(
		TEXT("Shroud.Clear"),
		TEXT("Shroud.Clear [Team] - forget everything a team has seen. No team means every team."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UShroudSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogShroudMap, Warning, TEXT("Shroud.Clear: no ShroudMap subsystem in this world."));
				return;
			}

			const int32 Team = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : -1;
			Subsystem->ClearMemory(Team);
			UE_LOG(LogShroudMap, Display, TEXT("Shroud.Clear: memory cleared."));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdStats(
		TEXT("Shroud.Stats"),
		TEXT("Shroud.Stats - print the measured counters to the log."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const UShroudSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogShroudMap, Warning, TEXT("Shroud.Stats: no ShroudMap subsystem in this world."));
				return;
			}

			Subsystem->LogStats();
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdResolution(
		TEXT("Shroud.Resolution"),
		TEXT("Shroud.Resolution [N] - print or set the map resolution. Memory is carried across."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UShroudSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogShroudMap, Warning, TEXT("Shroud.Resolution: no ShroudMap subsystem in this world."));
				return;
			}

			if (Args.Num() > 0)
			{
				Subsystem->SetResolution(FCString::Atoi(*Args[0]));
			}

			UE_LOG(LogShroudMap, Display, TEXT("ShroudMap resolution: %d x %d (%.1f cm per cell), update %.3f ms."),
				Subsystem->GetResolution(), Subsystem->GetResolution(), Subsystem->GetCellSize(),
				Subsystem->GetStats().UpdateMilliseconds);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdMemory(
		TEXT("Shroud.Memory"),
		TEXT("Shroud.Memory [0|1] - print or set whether the map keeps what it has seen."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UShroudSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogShroudMap, Warning, TEXT("Shroud.Memory: no ShroudMap subsystem in this world."));
				return;
			}

			if (Args.Num() > 0)
			{
				Subsystem->SetMemoryEnabled(FCString::Atoi(*Args[0]) != 0);
			}

			UE_LOG(LogShroudMap, Display, TEXT("ShroudMap memory: %s."), Subsystem->IsMemoryEnabled() ? TEXT("on") : TEXT("off"));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdOcclusion(
		TEXT("Shroud.Occlusion"),
		TEXT("Shroud.Occlusion [0|1] - print or set whether terrain and occluders stop sight."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UShroudSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogShroudMap, Warning, TEXT("Shroud.Occlusion: no ShroudMap subsystem in this world."));
				return;
			}

			if (Args.Num() > 0)
			{
				Subsystem->SetTerrainOcclusionEnabled(FCString::Atoi(*Args[0]) != 0);
			}

			UE_LOG(LogShroudMap, Display, TEXT("ShroudMap terrain occlusion: %s (height field %.0f%% gathered)."),
				Subsystem->IsTerrainOcclusionEnabled() ? TEXT("on") : TEXT("off"),
				Subsystem->GetStats().HeightFieldProgress * 100.0f);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdTeam(
		TEXT("Shroud.Team"),
		TEXT("Shroud.Team [N] - print or set the team the local player sees the world as."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UShroudSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogShroudMap, Warning, TEXT("Shroud.Team: no ShroudMap subsystem in this world."));
				return;
			}

			if (Args.Num() > 0)
			{
				Subsystem->SetViewTeam(FCString::Atoi(*Args[0]));
			}

			UE_LOG(LogShroudMap, Display, TEXT("ShroudMap view team: %d of %d."), Subsystem->GetViewTeam(), Subsystem->GetNumTeams());
		}));
}
