// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "ShroudMap.h"
#include "ShroudMapLog.h"

DEFINE_LOG_CATEGORY(LogShroudMap);

#define LOCTEXT_NAMESPACE "FShroudMapModule"

void FShroudMapModule::StartupModule()
{
	UE_LOG(LogShroudMap, Log, TEXT("ShroudMap started."));
}

void FShroudMapModule::ShutdownModule()
{
	UE_LOG(LogShroudMap, Log, TEXT("ShroudMap shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FShroudMapModule, ShroudMap)
