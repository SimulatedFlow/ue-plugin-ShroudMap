// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "ShroudSettings.h"

UShroudSettings::UShroudSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("ShroudMap");
}

FName UShroudSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UShroudSettings::GetSectionName() const
{
	return TEXT("ShroudMap");
}

const UShroudSettings& UShroudSettings::Get()
{
	const UShroudSettings* Settings = GetDefault<UShroudSettings>();
	check(Settings);
	return *Settings;
}
