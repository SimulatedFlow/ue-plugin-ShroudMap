// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Runtime module for ShroudMap. Loads at PreDefault so the world subsystem, the components and the
 * Shroud.* console commands exist before the first game world is created.
 */
class FShroudMapModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
