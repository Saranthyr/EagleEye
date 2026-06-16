using UnrealBuildTool;
using System;
using System.IO;
using System.Collections.Generic;

public class EagleEye : ModuleRules
{
    private readonly HashSet<string> RuntimeDependencyTargets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

    public EagleEye(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "OpenCV412",
            "InputCore",
            "EnhancedInput",
            "DeveloperSettings",
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
            "DeveloperSettings",
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

            string TensorRTRoot = Environment.GetEnvironmentVariable("TENSORRT_ROOT");
            if (String.IsNullOrEmpty(TensorRTRoot))
            {
                TensorRTRoot = Environment.GetEnvironmentVariable("TENSORRT_PATH");
            }

            string TensorRTIncludePath = !String.IsNullOrEmpty(TensorRTRoot) ? Path.Combine(TensorRTRoot, "include") : null;
            string TensorRTLibPath = !String.IsNullOrEmpty(TensorRTRoot) ? Path.Combine(TensorRTRoot, "lib") : null;
            string TensorRTInferLib = FindVersionedThenUnversionedImportLibrary(TensorRTLibPath, "nvinfer");
            string TensorRTPluginLib = FindVersionedThenUnversionedImportLibrary(TensorRTLibPath, "nvinfer_plugin");

            string CudaRoot = Environment.GetEnvironmentVariable("CUDA_PATH");
            if (String.IsNullOrEmpty(CudaRoot))
            {
                CudaRoot = Environment.GetEnvironmentVariable("CUDA_HOME");
            }

            string CudaIncludePath = !String.IsNullOrEmpty(CudaRoot) ? Path.Combine(CudaRoot, "include") : null;
            string CudaRuntimeLib = !String.IsNullOrEmpty(CudaRoot) ? Path.Combine(CudaRoot, "lib", "x64", "cudart.lib") : null;

            bool bWithTensorRT = Directory.Exists(TensorRTIncludePath) &&
                File.Exists(TensorRTInferLib) &&
                File.Exists(TensorRTPluginLib) &&
                Directory.Exists(CudaIncludePath) &&
                File.Exists(CudaRuntimeLib);

            if (bWithTensorRT)
            {
                AddSystemIncludePath(TensorRTIncludePath);
                AddImportLibrary(TensorRTInferLib);
                AddImportLibrary(TensorRTPluginLib);
                StageMatchingRuntimeFiles(Path.Combine(TensorRTRoot, "bin"), "nvinfer*.dll");
                StageMatchingRuntimeFiles(Path.Combine(TensorRTRoot, "bin"), "nvinfer_plugin*.dll");
                CopyMatchingRuntimeFilesToBinaryOutput(Path.Combine(TensorRTRoot, "bin"), "nvinfer*.dll");
                CopyMatchingRuntimeFilesToBinaryOutput(Path.Combine(TensorRTRoot, "bin"), "nvinfer_plugin*.dll");
                AddSystemIncludePath(CudaIncludePath);
                AddImportLibrary(CudaRuntimeLib);
                StageMatchingRuntimeFiles(Path.Combine(CudaRoot, "bin"), "cudart64*.dll");
                StageMatchingRuntimeFiles(Path.Combine(CudaRoot, "bin", "x64"), "cudart64*.dll");
                CopyMatchingRuntimeFilesToBinaryOutput(Path.Combine(CudaRoot, "bin"), "cudart64*.dll");
                CopyMatchingRuntimeFilesToBinaryOutput(Path.Combine(CudaRoot, "bin", "x64"), "cudart64*.dll");
            }
            PublicDefinitions.Add(bWithTensorRT ? "WITH_TENSORRT=1" : "WITH_TENSORRT=0");
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            string TensorRTRoot = Environment.GetEnvironmentVariable("TENSORRT_ROOT");
            if (String.IsNullOrEmpty(TensorRTRoot))
            {
                TensorRTRoot = Environment.GetEnvironmentVariable("TENSORRT_PATH");
            }

            string CudaRoot = Environment.GetEnvironmentVariable("CUDA_HOME");
            if (String.IsNullOrEmpty(CudaRoot))
            {
                CudaRoot = Environment.GetEnvironmentVariable("CUDA_PATH");
            }
            if (String.IsNullOrEmpty(CudaRoot))
            {
                CudaRoot = "/usr/local/cuda-12.4";
            }

            string TensorRTIncludePath = !String.IsNullOrEmpty(TensorRTRoot) ? Path.Combine(TensorRTRoot, "include") : "/usr/include/x86_64-linux-gnu";
            string CudaIncludePath = Path.Combine(CudaRoot, "include");

            List<string> TensorRTLibraryDirectories = new List<string>();
            if (!String.IsNullOrEmpty(TensorRTRoot))
            {
                TensorRTLibraryDirectories.Add(Path.Combine(TensorRTRoot, "lib"));
                TensorRTLibraryDirectories.Add(Path.Combine(TensorRTRoot, "lib64"));
            }
            TensorRTLibraryDirectories.Add("/lib/x86_64-linux-gnu");
            TensorRTLibraryDirectories.Add("/usr/lib/x86_64-linux-gnu");

            string TensorRTInferLib = FindFirstExistingFile(TensorRTLibraryDirectories, "libnvinfer.so");
            string TensorRTPluginLib = FindFirstExistingFile(TensorRTLibraryDirectories, "libnvinfer_plugin.so");
            string CudaRuntimeLib = FindFirstExistingFile(new List<string> { Path.Combine(CudaRoot, "lib64"), Path.Combine(CudaRoot, "lib") }, "libcudart.so");

            bool bWithTensorRT = Directory.Exists(TensorRTIncludePath) &&
                Directory.Exists(CudaIncludePath) &&
                File.Exists(TensorRTInferLib) &&
                File.Exists(TensorRTPluginLib) &&
                File.Exists(CudaRuntimeLib);

            if (bWithTensorRT)
            {
                AddSystemIncludePath(TensorRTIncludePath);
                AddSystemIncludePath(CudaIncludePath);
                AddNativeSharedLibrary(TensorRTInferLib);
                AddNativeSharedLibrary(TensorRTPluginLib);
                AddNativeSharedLibrary(CudaRuntimeLib);
                StageAndCopyMatchingRuntimeFiles(Path.GetDirectoryName(TensorRTInferLib), "libnvinfer.so*");
                StageAndCopyMatchingRuntimeFiles(Path.GetDirectoryName(TensorRTPluginLib), "libnvinfer_plugin.so*");
                StageAndCopyMatchingRuntimeFiles(Path.GetDirectoryName(CudaRuntimeLib), "libcudart.so*");
            }
            else
            {
                LogInferenceDependencyHintIfVerbose(
                    "TensorRT",
                    "set TENSORRT_ROOT/CUDA_HOME or run Scripts/setup-inference-deps.sh --install-system --force-tensorrt");
            }
            PublicDefinitions.Add(bWithTensorRT ? "WITH_TENSORRT=1" : "WITH_TENSORRT=0");
        }
        else
        {
            PublicDefinitions.Add("WITH_TENSORRT=0");
        }

        string OnnxRuntimeRoot = Environment.GetEnvironmentVariable("ONNXRUNTIME_ROOT");
        if (String.IsNullOrEmpty(OnnxRuntimeRoot))
        {
            OnnxRuntimeRoot = Environment.GetEnvironmentVariable("ORT_ROOT");
        }
        if (String.IsNullOrEmpty(OnnxRuntimeRoot))
        {
            string ProjectOnnxRuntimeRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "OnnxRuntimeDirectML"));
            if (Directory.Exists(ProjectOnnxRuntimeRoot))
            {
                OnnxRuntimeRoot = ProjectOnnxRuntimeRoot;
            }
        }

        bool bWithOnnxRuntime = false;
        bool bWithOnnxRuntimeDirectML = false;
        bool bWithOnnxRuntimeMIGraphX = false;
        if (!String.IsNullOrEmpty(OnnxRuntimeRoot))
        {
            string DirectMLRedistRoot = Environment.GetEnvironmentVariable("DIRECTML_ROOT");
            if (String.IsNullOrEmpty(DirectMLRedistRoot))
            {
                string ProjectDirectMLRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "DirectML"));
                if (Directory.Exists(ProjectDirectMLRoot))
                {
                    DirectMLRedistRoot = ProjectDirectMLRoot;
                }
            }

            string OnnxIncludePath = FindFirstExistingDirectory(new List<string>
            {
                Path.Combine(OnnxRuntimeRoot, "include"),
                Path.Combine(OnnxRuntimeRoot, "build", "native", "include")
            });
            List<string> OnnxLibDirs = Target.Platform == UnrealTargetPlatform.Win64
                ? new List<string>
                {
                    Path.Combine(OnnxRuntimeRoot, "lib"),
                    Path.Combine(OnnxRuntimeRoot, "lib", "x64"),
                    Path.Combine(OnnxRuntimeRoot, "runtimes", "win-x64", "native"),
                    Path.Combine(OnnxRuntimeRoot, "build", "native"),
                    Path.Combine(OnnxRuntimeRoot, "build", "native", "lib"),
                    Path.Combine(OnnxRuntimeRoot, "build", "native", "lib", "x64")
                }
                : new List<string>
                {
                    Path.Combine(OnnxRuntimeRoot, "lib"),
                    Path.Combine(OnnxRuntimeRoot, "lib64")
                };
            string OnnxImportOrSharedLib = Target.Platform == UnrealTargetPlatform.Win64
                ? FindFirstExistingFile(OnnxLibDirs, "onnxruntime.lib")
                : FindFirstExistingFile(OnnxLibDirs, "libonnxruntime.so");

            bWithOnnxRuntime = Directory.Exists(OnnxIncludePath) && File.Exists(OnnxImportOrSharedLib);
            if (bWithOnnxRuntime)
            {
                AddSystemIncludePath(OnnxIncludePath);
                AddSystemIncludePath(Path.Combine(OnnxIncludePath, "onnxruntime", "core", "session"));
                string DirectMLProviderHeader = FindFirstExistingFile(new List<string>
                {
                    OnnxIncludePath,
                    Path.Combine(OnnxIncludePath, "onnxruntime", "core", "providers", "dml")
                }, "dml_provider_factory.h");
                string MIGraphXProviderHeader = Path.Combine(OnnxIncludePath, "onnxruntime", "core", "providers", "migraphx", "migraphx_provider_factory.h");

                if (Target.Platform == UnrealTargetPlatform.Win64)
                {
                    PublicSystemLibraries.Add("dxgi.lib");
                    List<string> OnnxRuntimeDllDirs = new List<string>
                    {
                        Path.Combine(OnnxRuntimeRoot, "bin"),
                        Path.Combine(OnnxRuntimeRoot, "lib"),
                        Path.Combine(OnnxRuntimeRoot, "lib", "x64"),
                        Path.Combine(OnnxRuntimeRoot, "runtimes", "win-x64", "native"),
                        Path.Combine(OnnxRuntimeRoot, "build", "native"),
                        Path.Combine(OnnxRuntimeRoot, "build", "native", "bin"),
                        Path.Combine(OnnxRuntimeRoot, "build", "native", "bin", "x64")
                    };
                    if (!String.IsNullOrEmpty(DirectMLRedistRoot))
                    {
                        OnnxRuntimeDllDirs.AddRange(new List<string>
                        {
                            Path.Combine(DirectMLRedistRoot, "bin", "x64-win"),
                            Path.Combine(DirectMLRedistRoot, "runtimes", "win-x64", "native"),
                            Path.Combine(DirectMLRedistRoot, "native"),
                            DirectMLRedistRoot
                        });
                    }
                    PublicAdditionalLibraries.Add(OnnxImportOrSharedLib);
                    PublicDelayLoadDLLs.Add("onnxruntime.dll");
                    string DirectMLDll = FindFirstExistingFile(OnnxRuntimeDllDirs, "DirectML.dll");
                    bWithOnnxRuntimeDirectML = File.Exists(DirectMLProviderHeader);

                    foreach (string DllDir in OnnxRuntimeDllDirs)
                    {
                        StageMatchingRuntimeFiles(DllDir, "onnxruntime*.dll");
                        CopyMatchingRuntimeFilesToBinaryOutput(DllDir, "onnxruntime*.dll");
                    }
                    if (File.Exists(DirectMLDll))
                    {
                        PublicDelayLoadDLLs.Add("DirectML.dll");
                        StageRuntimeFile(DirectMLDll);
                        CopyRuntimeFileToBinaryOutput(DirectMLDll);
                        RuntimeDependencies.Add(
                            Path.Combine("$(TargetOutputDir)", Path.GetFileName(DirectMLDll)),
                            DirectMLDll,
                            StagedFileType.NonUFS);
                        RuntimeDependencies.Add(
                            Path.Combine("$(BinaryOutputDir)", Path.GetFileName(DirectMLDll)),
                            DirectMLDll,
                            StagedFileType.NonUFS);
                    }
                }
                else if (Target.Platform == UnrealTargetPlatform.Linux)
                {
                    AddNativeSharedLibrary(OnnxImportOrSharedLib);
                    string MIGraphXProviderLib = FindFirstExistingFile(OnnxLibDirs, "libonnxruntime_providers_migraphx.so");
                    string RocmHome = Environment.GetEnvironmentVariable("ROCM_HOME");
                    if (String.IsNullOrEmpty(RocmHome))
                    {
                        RocmHome = "/opt/rocm";
                    }
                    List<string> MIGraphXRuntimeDirs = new List<string>(OnnxLibDirs)
                    {
                        Path.Combine(RocmHome, "lib"),
                        Path.Combine(RocmHome, "lib", "migraphx"),
                        Path.Combine(RocmHome, "lib", "migraphx", "lib"),
                        "/usr/lib/x86_64-linux-gnu"
                    };
                    string MIGraphXRuntimeLib = FindFirstMatchingFile(MIGraphXRuntimeDirs, "libmigraphx.so*");
                    string MIGraphXOnnxRuntimeLib = FindFirstMatchingFile(MIGraphXRuntimeDirs, "libmigraphx_onnx.so*");
                    string MIGraphXTfRuntimeLib = FindFirstMatchingFile(MIGraphXRuntimeDirs, "libmigraphx_tf.so*");
                    string MIGraphXCRuntimeLib = FindFirstMatchingFile(MIGraphXRuntimeDirs, "libmigraphx_c.so*");
                    string MIGraphXGpuRuntimeLib = FindFirstMatchingFile(MIGraphXRuntimeDirs, "libmigraphx_gpu.so*");
                    string HipBlasLtRuntimeLib = FindFirstMatchingFile(MIGraphXRuntimeDirs, "libhipblaslt.so*");
                    string RocmCoreRuntimeLib = FindFirstMatchingFile(MIGraphXRuntimeDirs, "librocm-core.so*");
                    string RocRollerRuntimeLib = FindFirstMatchingFile(MIGraphXRuntimeDirs, "librocroller.so*");
                    bWithOnnxRuntimeMIGraphX =
                        File.Exists(MIGraphXProviderHeader) &&
                        File.Exists(MIGraphXProviderLib) &&
                        File.Exists(MIGraphXRuntimeLib) &&
                        File.Exists(MIGraphXOnnxRuntimeLib) &&
                        File.Exists(MIGraphXTfRuntimeLib) &&
                        File.Exists(MIGraphXCRuntimeLib) &&
                        File.Exists(MIGraphXGpuRuntimeLib) &&
                        File.Exists(HipBlasLtRuntimeLib) &&
                        File.Exists(RocmCoreRuntimeLib) &&
                        File.Exists(RocRollerRuntimeLib);

                    foreach (string LibDir in OnnxLibDirs)
                    {
                        AddRuntimeLibraryPath(LibDir);
                        StageAndCopyMatchingRuntimeFiles(LibDir, "libonnxruntime.so*");
                        StageAndCopyMatchingRuntimeFiles(LibDir, "libonnxruntime_providers*.so*");
                    }
                    if (bWithOnnxRuntimeMIGraphX)
                    {
                        foreach (string MIGraphXRuntimeDir in MIGraphXRuntimeDirs)
                        {
                            AddRuntimeLibraryPath(MIGraphXRuntimeDir);
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "libmigraphx*.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "libhipblaslt.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "libhiprtc.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "libhiprtc-builtins.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "libMIOpen.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "librocblas.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "libamd_comgr.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "libroctx64.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "librocm-core.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "librocroller.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "libhipblas.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "libhipsparse.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "librocfft.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "librocsolver.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "librocsparse.so*");
                            StageAndCopyMatchingRuntimeFiles(MIGraphXRuntimeDir, "librccl.so*");
                        }
                    }
                }
            }
        }
        PublicDefinitions.Add(bWithOnnxRuntime ? "WITH_ONNXRUNTIME=1" : "WITH_ONNXRUNTIME=0");
        PublicDefinitions.Add(bWithOnnxRuntimeDirectML ? "WITH_ONNXRUNTIME_DML=1" : "WITH_ONNXRUNTIME_DML=0");
        PublicDefinitions.Add(bWithOnnxRuntimeMIGraphX ? "WITH_ONNXRUNTIME_MIGRAPHX=1" : "WITH_ONNXRUNTIME_MIGRAPHX=0");

        if (!bWithOnnxRuntime)
        {
            LogInferenceDependencyHint(
                "ONNX Runtime",
                Target.Platform == UnrealTargetPlatform.Win64
                    ? "run Scripts/setup-inference-deps.ps1 -InstallDirectML, or set ONNXRUNTIME_ROOT/ORT_ROOT"
                    : "source Saved/InferenceDeps.env or run Scripts/setup-inference-deps.sh --build-onnxruntime");
        }
        else if (Target.Platform == UnrealTargetPlatform.Win64 && !bWithOnnxRuntimeDirectML)
        {
            LogInferenceDependencyHint(
                "ONNX Runtime DirectML EP",
                "run Scripts/setup-inference-deps.ps1 -InstallDirectML, or point ONNXRUNTIME_ROOT at Microsoft.ML.OnnxRuntime.DirectML");
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux && !bWithOnnxRuntimeMIGraphX)
        {
            LogInferenceDependencyHint(
                "ONNX Runtime MIGraphX EP",
                "run Scripts/setup-inference-deps.sh --install-system --build-onnxruntime, then source Saved/InferenceDeps.env");
        }

        string[] RuntimeModelExtensions =
        {
            ".names",
            ".onnx",
            ".plan"
        };

        foreach (string RuntimeModelFile in Directory.GetFiles(ModuleDirectory, "*.*", SearchOption.TopDirectoryOnly))
        {
            string Extension = Path.GetExtension(RuntimeModelFile).ToLowerInvariant();
            if (Array.IndexOf(RuntimeModelExtensions, Extension) >= 0)
            {
                RuntimeDependencies.Add(
                    Path.Combine("$(TargetOutputDir)", "Models", Path.GetFileName(RuntimeModelFile)),
                    RuntimeModelFile,
                    StagedFileType.NonUFS);
            }
        }
        string ProjectRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
        StageRuntimeModelDirectory(Path.Combine(ProjectRoot, "Models"), Path.Combine("$(TargetOutputDir)", "Models"), RuntimeModelExtensions);
        StageRuntimeModelDirectory(Path.Combine(ModuleDirectory, "Models"), Path.Combine("$(TargetOutputDir)", "Models"), RuntimeModelExtensions);

        bEnableExceptions = true;
    }

    private void AddSystemIncludePath(string IncludePath)
    {
        if (Directory.Exists(IncludePath))
        {
            PublicSystemIncludePaths.Add(IncludePath);
        }
    }

    private void AddImportLibrary(string LibraryPath)
    {
        if (File.Exists(LibraryPath))
        {
            PublicAdditionalLibraries.Add(LibraryPath);
        }
    }

    private void AddNativeSharedLibrary(string LibraryPath)
    {
        if (!String.IsNullOrEmpty(LibraryPath) && File.Exists(LibraryPath))
        {
            PublicAdditionalLibraries.Add(LibraryPath);
            AddRuntimeDependencyOnce(
                Path.Combine("$(TargetOutputDir)", Path.GetFileName(LibraryPath)),
                LibraryPath,
                StagedFileType.NonUFS);
        }
    }

    private void AddRuntimeLibraryPath(string DirectoryPath)
    {
        if (!String.IsNullOrEmpty(DirectoryPath) && Directory.Exists(DirectoryPath) && !PublicRuntimeLibraryPaths.Contains(DirectoryPath))
        {
            PublicRuntimeLibraryPaths.Add(DirectoryPath);
        }
    }

    private void StageAndCopyMatchingRuntimeFiles(string DirectoryPath, string Pattern)
    {
        StageMatchingRuntimeFiles(DirectoryPath, Pattern);
        CopyMatchingRuntimeFilesToBinaryOutput(DirectoryPath, Pattern);
    }

    private void StageMatchingRuntimeFiles(string DirectoryPath, string Pattern)
    {
        if (!Directory.Exists(DirectoryPath))
        {
            return;
        }

        foreach (string RuntimeFile in Directory.GetFiles(DirectoryPath, Pattern, SearchOption.TopDirectoryOnly))
        {
            AddRuntimeDependencyOnce(
                Path.Combine("$(TargetOutputDir)", Path.GetFileName(RuntimeFile)),
                RuntimeFile,
                StagedFileType.NonUFS);
        }
    }

    private void StageRuntimeFile(string RuntimeFile)
    {
        if (File.Exists(RuntimeFile))
        {
            AddRuntimeDependencyOnce(
                Path.Combine("$(TargetOutputDir)", Path.GetFileName(RuntimeFile)),
                RuntimeFile,
                StagedFileType.NonUFS);
        }
    }

    private void CopyRuntimeFileToBinaryOutput(string RuntimeFile)
    {
        if (File.Exists(RuntimeFile))
        {
            AddRuntimeDependencyOnce(
                Path.Combine("$(BinaryOutputDir)", Path.GetFileName(RuntimeFile)),
                RuntimeFile,
                StagedFileType.NonUFS);
        }
    }

    private void CopyMatchingRuntimeFilesToBinaryOutput(string DirectoryPath, string Pattern)
    {
        if (!Directory.Exists(DirectoryPath))
        {
            return;
        }

        foreach (string RuntimeFile in Directory.GetFiles(DirectoryPath, Pattern, SearchOption.TopDirectoryOnly))
        {
            AddRuntimeDependencyOnce(
                Path.Combine("$(BinaryOutputDir)", Path.GetFileName(RuntimeFile)),
                RuntimeFile,
                StagedFileType.NonUFS);
        }
    }

    private void StageRuntimeModelDirectory(string DirectoryPath, string TargetRoot, string[] RuntimeModelExtensions)
    {
        if (!Directory.Exists(DirectoryPath))
        {
            return;
        }

        foreach (string RuntimeModelFile in Directory.GetFiles(DirectoryPath, "*.*", SearchOption.AllDirectories))
        {
            string Extension = Path.GetExtension(RuntimeModelFile).ToLowerInvariant();
            if (Array.IndexOf(RuntimeModelExtensions, Extension) < 0)
            {
                continue;
            }

            string RelativePath = Path.GetRelativePath(DirectoryPath, RuntimeModelFile);
            RuntimeDependencies.Add(
                Path.Combine(TargetRoot, RelativePath),
                RuntimeModelFile,
                StagedFileType.NonUFS);
        }
    }

    private void AddRuntimeDependencyOnce(string TargetPath, string SourcePath, StagedFileType StageType)
    {
        string NormalizedTargetPath = TargetPath.Replace('\\', '/');
        if (RuntimeDependencyTargets.Add(NormalizedTargetPath))
        {
            RuntimeDependencies.Add(TargetPath, SourcePath, StageType);
        }
    }

    private void LogInferenceDependencyHint(string DependencyName, string Hint)
    {
        Console.WriteLine("EagleEye: {0} not found; {1}.", DependencyName, Hint);
    }

    private void LogInferenceDependencyHintIfVerbose(string DependencyName, string Hint)
    {
        if (Environment.GetEnvironmentVariable("EAGLEEYE_VERBOSE_DEPS") == "1")
        {
            LogInferenceDependencyHint(DependencyName, Hint);
        }
    }

    private static string FindFirstExistingFile(List<string> Directories, string FileName)
    {
        foreach (string DirectoryPath in Directories)
        {
            if (String.IsNullOrEmpty(DirectoryPath))
            {
                continue;
            }

            string CandidatePath = Path.Combine(DirectoryPath, FileName);
            if (File.Exists(CandidatePath))
            {
                return CandidatePath;
            }
        }

        return null;
    }

    private static string FindFirstMatchingFile(List<string> Directories, string Pattern)
    {
        foreach (string DirectoryPath in Directories)
        {
            if (String.IsNullOrEmpty(DirectoryPath) || !Directory.Exists(DirectoryPath))
            {
                continue;
            }

            string[] Matches = Directory.GetFiles(DirectoryPath, Pattern, SearchOption.TopDirectoryOnly);
            if (Matches.Length > 0)
            {
                Array.Sort(Matches, StringComparer.Ordinal);
                return Matches[0];
            }
        }

        return null;
    }

    private static string FindVersionedThenUnversionedImportLibrary(string DirectoryPath, string BaseName)
    {
        if (String.IsNullOrEmpty(DirectoryPath) || !Directory.Exists(DirectoryPath))
        {
            return null;
        }

        List<KeyValuePair<int, string>> VersionedLibraries = new List<KeyValuePair<int, string>>();
        foreach (string CandidatePath in Directory.GetFiles(DirectoryPath, BaseName + "_*.lib", SearchOption.TopDirectoryOnly))
        {
            string CandidateName = Path.GetFileNameWithoutExtension(CandidatePath);
            string Prefix = BaseName + "_";
            if (!CandidateName.StartsWith(Prefix, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            string VersionText = CandidateName.Substring(Prefix.Length);
            int Version = 0;
            if (Int32.TryParse(VersionText, out Version))
            {
                VersionedLibraries.Add(new KeyValuePair<int, string>(Version, CandidatePath));
            }
        }

        VersionedLibraries.Sort((Left, Right) => Right.Key.CompareTo(Left.Key));
        if (VersionedLibraries.Count > 0)
        {
            return VersionedLibraries[0].Value;
        }

        string UnversionedPath = Path.Combine(DirectoryPath, BaseName + ".lib");
        return File.Exists(UnversionedPath) ? UnversionedPath : null;
    }

    private static string FindFirstExistingDirectory(List<string> Directories)
    {
        foreach (string DirectoryPath in Directories)
        {
            if (Directory.Exists(DirectoryPath))
            {
                return DirectoryPath;
            }
        }

        return null;
    }
}
