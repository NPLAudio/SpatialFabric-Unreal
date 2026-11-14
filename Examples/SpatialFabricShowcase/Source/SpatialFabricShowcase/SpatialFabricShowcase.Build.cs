using UnrealBuildTool;
public class SpatialFabricShowcase : ModuleRules
{
    public SpatialFabricShowcase(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore" });
    }
}
