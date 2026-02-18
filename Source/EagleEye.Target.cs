using UnrealBuildTool;

public class EagleEyeTarget : TargetRules
{
    public EagleEyeTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;

        ExtraModuleNames.AddRange(new string[] { "EagleEye" });
    }
}
