// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class ShroudMap : ModuleRules
{
	public ShroudMap(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// One runtime module and nothing else.
		//
		// Deliberately NOT here:
		//   UMG      - the fog itself is a texture and a material; it never needs a widget. The demo's
		//              buttons are UMG assets in Content, which call the Blueprint library like any other
		//              project would. Nothing in the drawing path depends on Slate.
		//   Niagara  - a revealer is a circle in a texture, not a particle system.
		//   AIModule - this is what the *player* sees. Perception is a different product and a different
		//              question; see the notes on TurretMind in the documentation.
		//   UnrealEd - everything here ships. There is no editor module and no editor-only code path.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
		});

		// RenderCore gives us GWhiteTexture for the statistics box background; RHI gives us
		// FUpdateTextureRegion2D, which is how a whole team's visibility map reaches the GPU in one go.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI",
		});
	}
}
