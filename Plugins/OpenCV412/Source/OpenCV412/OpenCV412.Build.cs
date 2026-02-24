// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;

public class OpenCV412 : ModuleRules
{
	public OpenCV412(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		string ThirdPartyPath = Path.Combine(ModuleDirectory, "ThirdParty", "OpenCV");
		string LinuxCudaPath = Path.Combine(ThirdPartyPath, "LinuxCuda");
		string IncludePath = Path.Combine(ThirdPartyPath, "include");

		// Prefer the headers that match the platform-specific binaries (cmake installs to include/opencv4).
		if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			// Prefer a CUDA-enabled install if present (built via our scripts).
			string LinuxCudaIncludePath = Path.Combine(LinuxCudaPath, "include", "opencv4");
			if (Directory.Exists(LinuxCudaIncludePath))
			{
				PublicIncludePaths.Add(LinuxCudaIncludePath);
			}

			string LinuxIncludePath = Path.Combine(ThirdPartyPath, "Linux", "include", "opencv4");
			if (Directory.Exists(LinuxIncludePath) && !PublicIncludePaths.Contains(LinuxIncludePath))
			{
				PublicIncludePaths.Add(LinuxIncludePath);
			}
			else if (Directory.Exists(IncludePath))
			{
				PublicIncludePaths.Add(IncludePath);
			}
		}
		else if (Directory.Exists(IncludePath))
		{
			PublicIncludePaths.Add(IncludePath);
		}

		if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			string LibPath = Path.Combine(ThirdPartyPath, "Linux", "lib");
			string LinuxCudaLibPath = Path.Combine(LinuxCudaPath, "lib");
			if (Directory.Exists(LinuxCudaLibPath))
			{
				LibPath = LinuxCudaLibPath;
			}
			if (Directory.Exists(LibPath))
			{
				bool bUsingSharedOpenCV = false;
                bool bUsingStaticOpenCV = false;
                HashSet<string> AddedLibraries = new HashSet<string>();
				string[] Libraries =
				{
					"opencv_core",
					"opencv_imgproc",
					"opencv_imgcodecs",
					"opencv_highgui",
					"opencv_dnn",
					"opencv_videoio",
					"opencv_video",
					"opencv_features2d",
					"opencv_photo",
					"opencv_flann",
					"opencv_cudev",
					"opencv_cudaarithm",
					"opencv_cudaimgproc",
					"opencv_cudafilters",
					"opencv_cudawarping",
					"opencv_calib3d"
				};

				foreach (string Lib in Libraries)
				{
					string SoPath = Path.Combine(LibPath, $"lib{Lib}.so");
					string StaticPath = Path.Combine(LibPath, $"lib{Lib}.a");
					if (File.Exists(StaticPath))
					{
                        if (AddedLibraries.Add(StaticPath))
                        {
						    PublicAdditionalLibraries.Add(StaticPath);
                        }
                        bUsingStaticOpenCV = true;
					}
					else if (File.Exists(SoPath))
					{
                        if (AddedLibraries.Add(SoPath))
                        {
						    PublicAdditionalLibraries.Add(SoPath);
                        }
						RuntimeDependencies.Add(SoPath);
						bUsingSharedOpenCV = true;

						string Versioned = SoPath + ".4.12.0";
						if (File.Exists(Versioned))
						{
							RuntimeDependencies.Add(Versioned);
						}
					}
				}

                if (bUsingStaticOpenCV)
                {
                    string[] StaticDependencyCandidates =
                    {
                        "liblibprotobuf.a", "libprotobuf.a",
                        "libzlib.a", "libz.a",
                        "liblibjpeg-turbo.a", "libjpeg-turbo.a",
                        "liblibpng.a", "libpng.a",
                        "liblibtiff.a", "libtiff.a",
                        "liblibwebp.a", "libwebp.a",
                        "liblibopenjp2.a", "libopenjp2.a"
                    };

                    foreach (string Candidate in StaticDependencyCandidates)
                    {
                        string CandidatePath = Path.Combine(LibPath, Candidate);
                        if (File.Exists(CandidatePath) && AddedLibraries.Add(CandidatePath))
                        {
                            PublicAdditionalLibraries.Add(CandidatePath);
                        }
                    }
                }

				PublicSystemLibraries.AddRange(new[] { "pthread", "dl", "m" });

				// If we are using shared OpenCV libs built against libc++, stage libc++ runtime alongside them.
				if (bUsingSharedOpenCV)
				{
					foreach (string RuntimeLib in new[] { "libc++.so.1", "libc++abi.so.1" })
					{
						string RuntimeSoPath = Path.Combine(LibPath, RuntimeLib);
						if (File.Exists(RuntimeSoPath))
						{
							RuntimeDependencies.Add(RuntimeSoPath);
						}
					}
				}
			}
			else
			{
				// Keep these for consumers even if LibPath doesn't exist.
				PublicSystemLibraries.AddRange(new[] { "pthread", "dl", "m" });
			}
		}

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "CoreUObject",
            "Engine"
        });
	}
}
