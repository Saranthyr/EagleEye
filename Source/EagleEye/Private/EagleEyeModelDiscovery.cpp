#include "EagleEyeModelDiscovery.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

namespace
{
    const TArray<FString>& SupportedModelExtensions()
    {
        static const TArray<FString> Extensions = {
            TEXT("plan"),
            TEXT("onnx")
        };
        return Extensions;
    }

    bool IsSupportedModelExtension(const FString& Extension)
    {
        for (const FString& SupportedExtension : SupportedModelExtensions())
        {
            if (Extension.Equals(SupportedExtension, ESearchCase::IgnoreCase))
            {
                return true;
            }
        }
        return false;
    }

    void AddUniqueNormalizedDirectory(TArray<FString>& Directories, const FString& Directory)
    {
        if (Directory.IsEmpty())
        {
            return;
        }

        FString Normalized = FPaths::ConvertRelativePathToFull(Directory);
        FPaths::NormalizeDirectoryName(Normalized);
        Directories.AddUnique(Normalized);
    }

    FString StripPlatformSuffix(FString Name)
    {
        static const TArray<FString> Suffixes = {
            TEXT("_linux_x64"),
            TEXT("-linux-x64"),
            TEXT("_linux"),
            TEXT("-linux"),
            TEXT("_windows"),
            TEXT("-windows"),
            TEXT("_win64"),
            TEXT("-win64")
        };

        for (const FString& Suffix : Suffixes)
        {
            if (Name.EndsWith(Suffix, ESearchCase::IgnoreCase))
            {
                return Name.LeftChop(Suffix.Len());
            }
        }

        return Name;
    }

    bool PathIsUnderDirectory(FString Path, FString Directory)
    {
        FPaths::NormalizeFilename(Path);
        FPaths::NormalizeFilename(Directory);
        if (!Directory.EndsWith(TEXT("/")))
        {
            Directory += TEXT("/");
        }
        return Path.StartsWith(Directory, ESearchCase::IgnoreCase);
    }

    bool IsDirectChildOfSearchDir(const FString& FilePath, const TArray<FString>& SearchDirs)
    {
        const FString ParentDir = FPaths::GetPath(FilePath);
        for (const FString& SearchDir : SearchDirs)
        {
            if (ParentDir.Equals(SearchDir, ESearchCase::IgnoreCase))
            {
                return true;
            }
        }
        return false;
    }

    bool DirectoryContainsSupportedModelFile(const FString& Directory)
    {
        if (!IFileManager::Get().DirectoryExists(*Directory))
        {
            return false;
        }

        for (const FString& Extension : SupportedModelExtensions())
        {
            TArray<FString> FoundFiles;
            IFileManager::Get().FindFiles(
                FoundFiles,
                *FPaths::Combine(Directory, FString::Printf(TEXT("*.%s"), *Extension)),
                true,
                false);
            if (FoundFiles.Num() > 0)
            {
                return true;
            }
        }

        return false;
    }

    void AddCandidatePath(TArray<FString>& Candidates, const FString& CandidatePath)
    {
        if (CandidatePath.IsEmpty())
        {
            return;
        }

        FString Normalized = FPaths::ConvertRelativePathToFull(CandidatePath);
        FPaths::NormalizeFilename(Normalized);
        Candidates.AddUnique(Normalized);
    }

    void AddPlanCandidates(TArray<FString>& Candidates, const FString& Directory, const FString& ModelName)
    {
#if PLATFORM_WINDOWS
        static const TArray<FString> PlatformSuffixes = {
            TEXT("_windows"),
            TEXT("-windows"),
            TEXT("_win64"),
            TEXT("-win64")
        };
        static const TArray<FString> PlatformNames = {
            TEXT("windows"),
            TEXT("win64")
        };
#elif PLATFORM_LINUX
        static const TArray<FString> PlatformSuffixes = {
            TEXT("_linux"),
            TEXT("-linux"),
            TEXT("_linux_x64"),
            TEXT("-linux-x64")
        };
        static const TArray<FString> PlatformNames = {
            TEXT("linux"),
            TEXT("linux_x64")
        };
#else
        static const TArray<FString> PlatformSuffixes = {};
        static const TArray<FString> PlatformNames = {};
#endif

        for (const FString& Suffix : PlatformSuffixes)
        {
            AddCandidatePath(Candidates, FPaths::Combine(Directory, ModelName + Suffix + TEXT(".plan")));
        }
        for (const FString& PlatformName : PlatformNames)
        {
            AddCandidatePath(Candidates, FPaths::Combine(Directory, PlatformName + TEXT(".plan")));
        }

        AddCandidatePath(Candidates, FPaths::Combine(Directory, ModelName + TEXT(".plan")));
        AddCandidatePath(Candidates, FPaths::Combine(Directory, TEXT("model.plan")));
    }

    void AddOnnxCandidates(TArray<FString>& Candidates, const FString& Directory, const FString& ModelName)
    {
        AddCandidatePath(Candidates, FPaths::Combine(Directory, ModelName + TEXT(".onnx")));
        AddCandidatePath(Candidates, FPaths::Combine(Directory, TEXT("model.onnx")));
    }

    FString FirstExistingFile(const TArray<FString>& Candidates)
    {
        for (const FString& Candidate : Candidates)
        {
            if (IFileManager::Get().FileExists(*Candidate))
            {
                return Candidate;
            }
        }
        return FString();
    }
}

namespace EagleEyeModelDiscovery
{
    TArray<FString> GetRuntimeModelSearchDirectories()
    {
        TArray<FString> Directories;

        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("Models")));
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::LaunchDir(), TEXT("Models")));
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::ProjectDir(), TEXT("Models")));
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Models")));
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("EagleEye"), TEXT("Models")));
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("EagleEye")));

        return Directories;
    }

    TArray<FString> GetAvailableModelNames()
    {
        TArray<FString> ModelNames;
        const TArray<FString> SearchDirs = GetRuntimeModelSearchDirectories();

        for (const FString& SearchDir : SearchDirs)
        {
            if (!IFileManager::Get().DirectoryExists(*SearchDir))
            {
                continue;
            }

            for (const FString& Extension : SupportedModelExtensions())
            {
                TArray<FString> FoundFiles;
                IFileManager::Get().FindFilesRecursive(
                    FoundFiles,
                    *SearchDir,
                    *FString::Printf(TEXT("*.%s"), *Extension),
                    true,
                    false);

                for (const FString& FoundFile : FoundFiles)
                {
                    FString NormalizedFile = FPaths::ConvertRelativePathToFull(FoundFile);
                    FPaths::NormalizeFilename(NormalizedFile);

                    if (IsDirectChildOfSearchDir(NormalizedFile, SearchDirs))
                    {
                        ModelNames.AddUnique(StripPlatformSuffix(FPaths::GetBaseFilename(NormalizedFile)));
                        continue;
                    }

                    const FString ParentDir = FPaths::GetPath(NormalizedFile);
                    bool bParentIsKnownSearchRoot = false;
                    for (const FString& SearchRoot : SearchDirs)
                    {
                        if (ParentDir.Equals(SearchRoot, ESearchCase::IgnoreCase))
                        {
                            bParentIsKnownSearchRoot = true;
                            break;
                        }
                    }

                    ModelNames.AddUnique(bParentIsKnownSearchRoot
                        ? StripPlatformSuffix(FPaths::GetBaseFilename(NormalizedFile))
                        : FPaths::GetCleanFilename(ParentDir));
                }
            }
        }

        ModelNames.Sort();
        return ModelNames;
    }

    FString NormalizeModelSelection(const FString& RequestedModel)
    {
        FString Selection = RequestedModel.TrimStartAndEnd();
        Selection.ReplaceInline(TEXT("\\"), TEXT("/"));

        if (Selection.IsEmpty())
        {
            return FString();
        }

        const bool bLooksLikePath =
            FPaths::IsRelative(Selection) == false ||
            Selection.Contains(TEXT("/"));
        const FString Extension = FPaths::GetExtension(Selection);
        if (!bLooksLikePath && IsSupportedModelExtension(Extension))
        {
            return StripPlatformSuffix(FPaths::GetBaseFilename(Selection));
        }

        return Selection;
    }

    FString ResolveModelPathForBackend(const FString& RequestedModel, EDetectionInferenceBackend Backend)
    {
        const FString Selection = NormalizeModelSelection(RequestedModel);
        if (Selection.IsEmpty())
        {
            return FString();
        }

        if (IsSupportedModelExtension(FPaths::GetExtension(Selection)))
        {
            FString DirectPath = FPaths::ConvertRelativePathToFull(Selection);
            FPaths::NormalizeFilename(DirectPath);
            if (IFileManager::Get().FileExists(*DirectPath))
            {
                return DirectPath;
            }
        }

        TArray<FString> Candidates;
        TArray<FString> CandidateDirs;

        if (IFileManager::Get().DirectoryExists(*Selection))
        {
            AddCandidatePath(CandidateDirs, Selection);
        }

        for (const FString& SearchDir : GetRuntimeModelSearchDirectories())
        {
            const FString FolderCandidate = FPaths::Combine(SearchDir, Selection);
            if (DirectoryContainsSupportedModelFile(FolderCandidate))
            {
                AddCandidatePath(CandidateDirs, FolderCandidate);
            }
        }

        for (const FString& CandidateDir : CandidateDirs)
        {
            if (Backend == EDetectionInferenceBackend::TensorRT)
            {
                AddPlanCandidates(Candidates, CandidateDir, Selection);
            }
            else
            {
                AddOnnxCandidates(Candidates, CandidateDir, Selection);
            }
        }

        for (const FString& SearchDir : GetRuntimeModelSearchDirectories())
        {
            if (Backend == EDetectionInferenceBackend::TensorRT)
            {
                AddPlanCandidates(Candidates, SearchDir, Selection);
            }
            else
            {
                AddOnnxCandidates(Candidates, SearchDir, Selection);
            }
        }

        return FirstExistingFile(Candidates);
    }
}
