// Copyright (c) 2026 SpatialFabric Contributors. Licensed under the MIT License.

// Links this compiled module to the engine's module system (name must match .Build.cs).

#include "SpatialFabricModule.h"
#include "SpatialSceneQuery.h"

IMPLEMENT_MODULE(FSpatialFabricModule, SpatialFabric)

void FSpatialFabricModule::StartupModule()
{
	SpatialSceneQuery::Register();
}

void FSpatialFabricModule::ShutdownModule()
{
	SpatialSceneQuery::Unregister();
}
