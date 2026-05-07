using UnrealBuildTool;

public class EagleEye : ModuleRules
{
    public EagleEye(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "OpenCV412",
            "InputCore",
            "EnhancedInput",
            "RenderCore",
            "ProceduralMeshComponent",
            "NavigationSystem",
            "AIModule",
            "GameplayTasks"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Renderer",
            "RenderCore",
            "RHI",
            "RHICore",
            "OpenCV412",
            "ProceduralMeshComponent",
            "NavigationSystem",
            "AIModule",
            "GameplayTasks"
        });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDependencyModuleNames.Add("D3D12RHI");
        }
        if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicSystemIncludePaths.Add("/usr/include/x86_64-linux-gnu");
            PublicSystemIncludePaths.Add("/usr/local/cuda-12.4/include");

            PublicAdditionalLibraries.Add("/lib/x86_64-linux-gnu/libnvinfer.so");
            PublicAdditionalLibraries.Add("/lib/x86_64-linux-gnu/libnvinfer_plugin.so");
            PublicAdditionalLibraries.Add("/usr/local/cuda-12.4/lib64/libcudart.so");

            RuntimeDependencies.Add("/lib/x86_64-linux-gnu/libnvinfer.so");
            RuntimeDependencies.Add("/lib/x86_64-linux-gnu/libnvinfer_plugin.so");
            RuntimeDependencies.Add("/usr/local/cuda-12.4/lib64/libcudart.so");
        }


        bEnableExceptions = true;
    }
}
