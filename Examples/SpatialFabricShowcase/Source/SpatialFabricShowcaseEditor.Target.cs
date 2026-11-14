using UnrealBuildTool;
public class SpatialFabricShowcaseEditorTarget : TargetRules
{
    public SpatialFabricShowcaseEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("SpatialFabricShowcase");
    }
}
