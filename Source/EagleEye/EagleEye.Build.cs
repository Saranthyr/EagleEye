// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class EagleEye : ModuleRules
{
	public EagleEye(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"OpenCV",
				"OpenCVHelper",
				"InputCore",
				"EnhancedInput",
				"RenderCore",
				"ProceduralMeshComponent"
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
				// "D3D12RHI",
				"OpenCV",
				"OpenCVHelper",
				"ProceduralMeshComponent"
				});
				
		if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDependencyModuleNames.Add("D3D12RHI");
        }
		bEnableExceptions = true;
	}
}
