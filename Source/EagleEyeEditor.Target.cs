using UnrealBuildTool;

public class EagleEyeEditorTarget : TargetRules
{
    public EagleEyeEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;

        ExtraModuleNames.AddRange(new string[] { "EagleEye" });
    }
}
