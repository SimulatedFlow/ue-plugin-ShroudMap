// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "ShroudHUD.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GlobalRenderResources.h"
#include "Misc/StringBuilder.h"
#include "SceneTypes.h"
#include "ShroudSettings.h"
#include "ShroudSubsystem.h"

namespace ShroudHudPrivate
{
	static constexpr float LineHeight = 15.0f;
	static constexpr float BoxPadding = 8.0f;
	static constexpr int32 StatsLineCount = 14;

	static const FLinearColor PanelBackground(0.0f, 0.0f, 0.0f, 0.62f);
	static const FLinearColor HeadingColor(0.55f, 0.85f, 1.0f, 1.0f);
	static const FLinearColor BodyColor(0.9f, 0.9f, 0.9f, 1.0f);
	static const FLinearColor GoodColor(0.55f, 0.95f, 0.55f, 1.0f);
	static const FLinearColor WarnColor(0.98f, 0.78f, 0.35f, 1.0f);

	static void DrawFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Position, GWhiteTexture, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}
}

AShroudHUD::AShroudHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AShroudHUD::BeginPlay()
{
	Super::BeginPlay();

	bShowStats = UShroudSettings::Get().bShowStatsByDefault;
}

void AShroudHUD::ToggleStats()
{
	bShowStats = !bShowStats;
}

void AShroudHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !bShowStats)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	DrawStatsBox(Canvas, Font, UShroudSubsystem::Get(this));
}

float AShroudHUD::DrawStatsBox(UCanvas* InCanvas, UFont* Font, const UShroudSubsystem* Subsystem)
{
	using namespace ShroudHudPrivate;

	const float BoxHeight = StatsLineCount * LineHeight + BoxPadding * 2.0f;
	DrawFilledRect(
		InCanvas,
		FVector2D(StatsBoxOrigin.X - BoxPadding, StatsBoxOrigin.Y - BoxPadding),
		FVector2D(StatsBoxWidth, BoxHeight),
		PanelBackground);

	float LineY = static_cast<float>(StatsBoxOrigin.Y);
	auto DrawLine = [&](FStringView Line, const FLinearColor& Color)
	{
		FCanvasTextStringViewItem Item(FVector2D(StatsBoxOrigin.X, LineY), Line, Font, Color);
		InCanvas->DrawItem(Item);
		LineY += LineHeight;
	};

	TStringBuilder<192> Line;

	Line.Reset();
	Line.Append(TEXT("ShroudMap"));
	DrawLine(Line.ToView(), HeadingColor);

	if (!Subsystem)
	{
		Line.Reset();
		Line.Append(TEXT("no subsystem in this world"));
		DrawLine(Line.ToView(), WarnColor);
		return BoxHeight;
	}

	const FShroudStats& Stats = Subsystem->GetStats();

	Line.Reset();
	Line.Appendf(TEXT("Revealers          %d  (%d active)"), Stats.Revealers, Stats.ActiveRevealers);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Occluders          %d"), Stats.Occluders);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Visible cells      %d"), Stats.VisibleCells);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Explored cells     %d / %d"), Stats.ExploredCells, Stats.TotalCells);
	DrawLine(Line.ToView(), BodyColor);

	// The headline number. It stays at one per team however many revealers are on the map, which is the
	// claim the whole plugin is sold on - so it is on the box, not in the small print.
	Line.Reset();
	Line.Appendf(TEXT("Texture uploads    %d  (%d teams)"), Stats.TextureUploads, Stats.Teams);
	DrawLine(Line.ToView(), Stats.TextureUploads <= Stats.Teams ? GoodColor : WarnColor);

	Line.Reset();
	Line.Appendf(TEXT("Uploaded           %.1f KB"), Stats.UploadedBytes / 1024.0f);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Update             %.3f ms"), Stats.UpdateMilliseconds);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("  footprints       %.3f ms  (%d built, %d deferred)"),
		Stats.FootprintMilliseconds, Stats.FootprintRebuilds, Stats.FootprintRebuildsDeferred);
	DrawLine(Line.ToView(), Stats.FootprintRebuildsDeferred > 0 ? WarnColor : BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("  composite        %.3f ms"), Stats.CompositeMilliseconds);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("  upload           %.3f ms"), Stats.UploadMilliseconds);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Resolution         %d x %d  (%.0f cm per cell)"), Stats.Resolution, Stats.Resolution, Stats.CellSize);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Memory             %s        Team %d of %d"),
		Stats.bMemoryEnabled ? TEXT("ON ") : TEXT("OFF"), Stats.ViewTeam, Stats.Teams);
	DrawLine(Line.ToView(), Stats.bMemoryEnabled ? GoodColor : WarnColor);

	Line.Reset();
	if (!Stats.bTerrainOcclusionEnabled)
	{
		Line.Append(TEXT("Terrain occlusion  OFF"));
	}
	else if (Stats.HeightFieldProgress < 1.0f)
	{
		Line.Appendf(TEXT("Terrain occlusion  ON  (height field %.0f%%)"), Stats.HeightFieldProgress * 100.0f);
	}
	else
	{
		Line.Appendf(TEXT("Terrain occlusion  ON  (%d samples/update)"), Stats.OcclusionSamples);
	}
	DrawLine(Line.ToView(), Stats.bTerrainOcclusionEnabled ? GoodColor : BodyColor);

	return BoxHeight;
}
