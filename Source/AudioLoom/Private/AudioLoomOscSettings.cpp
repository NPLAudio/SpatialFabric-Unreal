// Copyright (c) 2026 AudioLoom Contributors.

/** @file AudioLoomOscSettings.cpp — default category name for settings UI. */

#include "AudioLoomOscSettings.h"

UAudioLoomOscSettings::UAudioLoomOscSettings()
{
	CategoryName = TEXT("Plugins"); // alongside Spatial Fabric's spatial-control settings
}

FName UAudioLoomOscSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}
