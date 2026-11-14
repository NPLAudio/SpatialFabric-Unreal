using UnrealBuildTool;
public class SpatialFabricShowcaseTarget : TargetRules
{
    public SpatialFabricShowcaseTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("SpatialFabricShowcase");
    }
}
