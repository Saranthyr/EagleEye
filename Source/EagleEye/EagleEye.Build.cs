// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class EagleEye : ModuleRules
{
	public EagleEye(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "ThirdParty/OpenCV/include"));

        // PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "ThirdParty/OpenCV/lib/opencv_world4100.lib"));
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"OpenCV",
				"OpenCVHelper",
				"InputCore",
				"EnhancedInput",
				"RenderCore"
		});
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"Renderer",
				"RenderCore",
				"RHI",
				"RHICore",
				"D3D12RHI",
				"OpenCV",
				"OpenCVHelper"
				});
		bEnableExceptions = true;
	}
}
