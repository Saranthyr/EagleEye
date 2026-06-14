// Fill out your copyright notice in the Description page of Project Settings.

#include "MyActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "AIController.h"
#include "AI/CrowDetectionShareSubsystem.h"
#include "AI/CrowVisionSubsystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "EagleEyeDetectionSettings.h"
#include "EagleEyeModelDiscovery.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "RHI.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Math/UnrealMathUtility.h"
#include "RenderingThread.h"
#include "ScreenCaptureComponent.h"
#include <algorithm>
#include <array>
#if PLATFORM_WINDOWS && WITH_ONNXRUNTIME_DML
#include "Windows/AllowWindowsPlatformTypes.h"
#include <dxgi1_6.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#if WITH_TENSORRT
#include <NvInfer.h>
#include <NvInferVersion.h>
#endif
#if WITH_ONNXRUNTIME_DML
#if __has_include(<onnxruntime/core/providers/dml/dml_provider_factory.h>)
#include <onnxruntime/core/providers/dml/dml_provider_factory.h>
#else
#include <dml_provider_factory.h>
#endif
#endif
namespace
{
    constexpr double MaxAsyncOwnerCameraReadbackLatencySeconds = 1.0;
    constexpr TCHAR OnnxRuntimeGpuCrashGuardFileName[] = TEXT("OnnxRuntimeGpuCrashGuard.txt");
    uint64 GOwnerCameraReadbackLastEnqueueFrame = MAX_uint64;
    uint64 GOwnerCameraReadbackLastLockFrame = MAX_uint64;

    void SetVideoPixelBGRA(TArray<uint8>& RawFrame, int32 Width, int32 Height, int32 X, int32 Y, const FColor& Color)
    {
        if (X < 0 || X >= Width || Y < 0 || Y >= Height)
        {
            return;
        }

        const int32 Offset = ((Y * Width) + X) * 4;
        RawFrame[Offset + 0] = Color.B;
        RawFrame[Offset + 1] = Color.G;
        RawFrame[Offset + 2] = Color.R;
        RawFrame[Offset + 3] = Color.A;
    }

    void DrawVideoPointBGRA(TArray<uint8>& RawFrame, int32 Width, int32 Height, int32 X, int32 Y, const FColor& Color, int32 Radius)
    {
        for (int32 OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
        {
            for (int32 OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
            {
                SetVideoPixelBGRA(RawFrame, Width, Height, X + OffsetX, Y + OffsetY, Color);
            }
        }
    }

    void DrawVideoLineBGRA(TArray<uint8>& RawFrame, int32 Width, int32 Height, FVector2D Start, FVector2D End, const FColor& Color, int32 Thickness)
    {
        const float DeltaX = End.X - Start.X;
        const float DeltaY = End.Y - Start.Y;
        const int32 Steps = FMath::Max(1, FMath::RoundToInt(FMath::Max(FMath::Abs(DeltaX), FMath::Abs(DeltaY))));
        const int32 Radius = FMath::Max(0, Thickness / 2);

        for (int32 Step = 0; Step <= Steps; ++Step)
        {
            const float Alpha = static_cast<float>(Step) / static_cast<float>(Steps);
            const int32 X = FMath::RoundToInt(FMath::Lerp(Start.X, End.X, Alpha));
            const int32 Y = FMath::RoundToInt(FMath::Lerp(Start.Y, End.Y, Alpha));
            DrawVideoPointBGRA(RawFrame, Width, Height, X, Y, Color, Radius);
        }
    }

    void FillVideoRectBGRA(TArray<uint8>& RawFrame, int32 Width, int32 Height, int32 X, int32 Y, int32 RectWidth, int32 RectHeight, const FColor& Color)
    {
        const int32 MinX = FMath::Clamp(X, 0, Width);
        const int32 MinY = FMath::Clamp(Y, 0, Height);
        const int32 MaxX = FMath::Clamp(X + RectWidth, 0, Width);
        const int32 MaxY = FMath::Clamp(Y + RectHeight, 0, Height);

        for (int32 PixelY = MinY; PixelY < MaxY; ++PixelY)
        {
            for (int32 PixelX = MinX; PixelX < MaxX; ++PixelX)
            {
                SetVideoPixelBGRA(RawFrame, Width, Height, PixelX, PixelY, Color);
            }
        }
    }

    const uint8* GetVideoGlyphRows(TCHAR Character)
    {
        static constexpr uint8 Space[7] = { 0, 0, 0, 0, 0, 0, 0 };
        static constexpr uint8 Unknown[7] = { 14, 17, 1, 2, 4, 0, 4 };
        static constexpr uint8 A[7] = { 14, 17, 17, 31, 17, 17, 17 };
        static constexpr uint8 B[7] = { 30, 17, 17, 30, 17, 17, 30 };
        static constexpr uint8 C[7] = { 14, 17, 16, 16, 16, 17, 14 };
        static constexpr uint8 D[7] = { 30, 17, 17, 17, 17, 17, 30 };
        static constexpr uint8 E[7] = { 31, 16, 16, 30, 16, 16, 31 };
        static constexpr uint8 F[7] = { 31, 16, 16, 30, 16, 16, 16 };
        static constexpr uint8 G[7] = { 14, 17, 16, 23, 17, 17, 15 };
        static constexpr uint8 H[7] = { 17, 17, 17, 31, 17, 17, 17 };
        static constexpr uint8 I[7] = { 14, 4, 4, 4, 4, 4, 14 };
        static constexpr uint8 J[7] = { 1, 1, 1, 1, 17, 17, 14 };
        static constexpr uint8 K[7] = { 17, 18, 20, 24, 20, 18, 17 };
        static constexpr uint8 L[7] = { 16, 16, 16, 16, 16, 16, 31 };
        static constexpr uint8 M[7] = { 17, 27, 21, 21, 17, 17, 17 };
        static constexpr uint8 N[7] = { 17, 25, 21, 19, 17, 17, 17 };
        static constexpr uint8 O[7] = { 14, 17, 17, 17, 17, 17, 14 };
        static constexpr uint8 P[7] = { 30, 17, 17, 30, 16, 16, 16 };
        static constexpr uint8 Q[7] = { 14, 17, 17, 17, 21, 18, 13 };
        static constexpr uint8 R[7] = { 30, 17, 17, 30, 20, 18, 17 };
        static constexpr uint8 S[7] = { 15, 16, 16, 14, 1, 1, 30 };
        static constexpr uint8 T[7] = { 31, 4, 4, 4, 4, 4, 4 };
        static constexpr uint8 U[7] = { 17, 17, 17, 17, 17, 17, 14 };
        static constexpr uint8 V[7] = { 17, 17, 17, 17, 17, 10, 4 };
        static constexpr uint8 W[7] = { 17, 17, 17, 21, 21, 21, 10 };
        static constexpr uint8 X[7] = { 17, 17, 10, 4, 10, 17, 17 };
        static constexpr uint8 Y[7] = { 17, 17, 10, 4, 4, 4, 4 };
        static constexpr uint8 Z[7] = { 31, 1, 2, 4, 8, 16, 31 };
        static constexpr uint8 Zero[7] = { 14, 17, 19, 21, 25, 17, 14 };
        static constexpr uint8 One[7] = { 4, 12, 4, 4, 4, 4, 14 };
        static constexpr uint8 Two[7] = { 14, 17, 1, 2, 4, 8, 31 };
        static constexpr uint8 Three[7] = { 30, 1, 1, 14, 1, 1, 30 };
        static constexpr uint8 Four[7] = { 2, 6, 10, 18, 31, 2, 2 };
        static constexpr uint8 Five[7] = { 31, 16, 16, 30, 1, 1, 30 };
        static constexpr uint8 Six[7] = { 14, 16, 16, 30, 17, 17, 14 };
        static constexpr uint8 Seven[7] = { 31, 1, 2, 4, 8, 8, 8 };
        static constexpr uint8 Eight[7] = { 14, 17, 17, 14, 17, 17, 14 };
        static constexpr uint8 Nine[7] = { 14, 17, 17, 15, 1, 1, 14 };
        static constexpr uint8 Colon[7] = { 0, 4, 4, 0, 4, 4, 0 };
        static constexpr uint8 Dot[7] = { 0, 0, 0, 0, 0, 12, 12 };
        static constexpr uint8 Dash[7] = { 0, 0, 0, 14, 0, 0, 0 };
        static constexpr uint8 Slash[7] = { 1, 1, 2, 4, 8, 16, 16 };
        static constexpr uint8 Underscore[7] = { 0, 0, 0, 0, 0, 0, 31 };

        switch (FChar::ToUpper(Character))
        {
        case TEXT('A'): return A;
        case TEXT('B'): return B;
        case TEXT('C'): return C;
        case TEXT('D'): return D;
        case TEXT('E'): return E;
        case TEXT('F'): return F;
        case TEXT('G'): return G;
        case TEXT('H'): return H;
        case TEXT('I'): return I;
        case TEXT('J'): return J;
        case TEXT('K'): return K;
        case TEXT('L'): return L;
        case TEXT('M'): return M;
        case TEXT('N'): return N;
        case TEXT('O'): return O;
        case TEXT('P'): return P;
        case TEXT('Q'): return Q;
        case TEXT('R'): return R;
        case TEXT('S'): return S;
        case TEXT('T'): return T;
        case TEXT('U'): return U;
        case TEXT('V'): return V;
        case TEXT('W'): return W;
        case TEXT('X'): return X;
        case TEXT('Y'): return Y;
        case TEXT('Z'): return Z;
        case TEXT('0'): return Zero;
        case TEXT('1'): return One;
        case TEXT('2'): return Two;
        case TEXT('3'): return Three;
        case TEXT('4'): return Four;
        case TEXT('5'): return Five;
        case TEXT('6'): return Six;
        case TEXT('7'): return Seven;
        case TEXT('8'): return Eight;
        case TEXT('9'): return Nine;
        case TEXT(':'): return Colon;
        case TEXT('.'): return Dot;
        case TEXT('-'): return Dash;
        case TEXT('/'): return Slash;
        case TEXT('_'): return Underscore;
        case TEXT(' '): return Space;
        default: return Unknown;
        }
    }

    void DrawVideoTextBGRA(TArray<uint8>& RawFrame, int32 Width, int32 Height, int32 X, int32 Y, const FString& Text, const FColor& Color, int32 Scale)
    {
        const int32 SafeScale = FMath::Max(1, Scale);
        int32 CursorX = X;

        for (int32 CharIndex = 0; CharIndex < Text.Len(); ++CharIndex)
        {
            const uint8* Rows = GetVideoGlyphRows(Text[CharIndex]);
            for (int32 Row = 0; Row < 7; ++Row)
            {
                for (int32 Col = 0; Col < 5; ++Col)
                {
                    if ((Rows[Row] & (1 << (4 - Col))) == 0)
                    {
                        continue;
                    }

                    FillVideoRectBGRA(
                        RawFrame,
                        Width,
                        Height,
                        CursorX + Col * SafeScale,
                        Y + Row * SafeScale,
                        SafeScale,
                        SafeScale,
                        Color);
                }
            }

            CursorX += 6 * SafeScale;
        }
    }

    void DrawDetectionsOnVideoFrameBGRA(
        TArray<uint8>& RawFrame,
        int32 Width,
        int32 Height,
        const TArray<FDetectionResult>& Detections,
        int32 DetectionSourceWidth,
        int32 DetectionSourceHeight)
    {
        if (Width <= 0 || Height <= 0 || DetectionSourceWidth <= 0 || DetectionSourceHeight <= 0 || Detections.IsEmpty())
        {
            return;
        }

        const float ScaleX = static_cast<float>(Width) / static_cast<float>(DetectionSourceWidth);
        const float ScaleY = static_cast<float>(Height) / static_cast<float>(DetectionSourceHeight);
        const FColor BoxColor(255, 0, 0, 255);
        const FColor LabelColor(255, 255, 0, 255);
        const FColor LabelBackgroundColor(0, 0, 0, 220);
        constexpr int32 BoxThickness = 3;
        constexpr int32 TextScale = 2;

        for (const FDetectionResult& Detection : Detections)
        {
            if (Detection.Corners.Num() < 2)
            {
                continue;
            }

            float MinX = TNumericLimits<float>::Max();
            float MinY = TNumericLimits<float>::Max();
            for (int32 CornerIndex = 0; CornerIndex < Detection.Corners.Num(); ++CornerIndex)
            {
                FVector2D Start = Detection.Corners[CornerIndex];
                FVector2D End = Detection.Corners[(CornerIndex + 1) % Detection.Corners.Num()];
                Start.X *= ScaleX;
                Start.Y *= ScaleY;
                End.X *= ScaleX;
                End.Y *= ScaleY;
                MinX = FMath::Min(MinX, Start.X);
                MinY = FMath::Min(MinY, Start.Y);
                DrawVideoLineBGRA(RawFrame, Width, Height, Start, End, BoxColor, BoxThickness);
            }

            const FString Label = Detection.Label.IsEmpty()
                ? FString::Printf(TEXT("CLASS %d: %.2f"), Detection.ClassId, Detection.Confidence)
                : Detection.Label;
            const FString TrimmedLabel = Label.Left(32);
            const int32 LabelWidth = FMath::Max(1, TrimmedLabel.Len()) * 6 * TextScale + 4;
            constexpr int32 LabelHeight = 7 * TextScale + 4;
            const int32 LabelX = FMath::Clamp(FMath::FloorToInt(MinX), 0, FMath::Max(0, Width - LabelWidth));
            const int32 LabelY = FMath::Clamp(FMath::FloorToInt(MinY) - LabelHeight, 0, FMath::Max(0, Height - LabelHeight));

            FillVideoRectBGRA(RawFrame, Width, Height, LabelX, LabelY, LabelWidth, LabelHeight, LabelBackgroundColor);
            DrawVideoTextBGRA(RawFrame, Width, Height, LabelX + 2, LabelY + 2, TrimmedLabel, LabelColor, TextScale);
        }
    }

    FString GetOnnxRuntimeGpuCrashGuardPath()
    {
        return FPaths::Combine(FPaths::ProjectSavedDir(), OnnxRuntimeGpuCrashGuardFileName);
    }

#if PLATFORM_WINDOWS && WITH_ONNXRUNTIME_DML
    FString DxgiAdapterDescToString(const DXGI_ADAPTER_DESC1& Desc)
    {
        return FString::Printf(TEXT("%s vendor=0x%04x device=0x%04x dedicated_vram=%.1fMB flags=0x%08x"),
            Desc.Description,
            Desc.VendorId,
            Desc.DeviceId,
            static_cast<double>(Desc.DedicatedVideoMemory) / (1024.0 * 1024.0),
            Desc.Flags);
    }

    void LogDirectMLAdapterCandidates(int32 RequestedDeviceId, const FString& RhiAdapterName)
    {
        UE_LOG(LogTemp, Log, TEXT("ONNX Runtime DirectML requested device_id=%d; UE RHI adapter=%s"),
            RequestedDeviceId,
            *RhiAdapterName);

        IDXGIFactory1* Factory = nullptr;
        const HRESULT FactoryResult = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&Factory));
        if (FAILED(FactoryResult) || !Factory)
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to enumerate DXGI adapters for DirectML logging: HRESULT=0x%08x"),
                static_cast<uint32>(FactoryResult));
            return;
        }

        for (uint32 AdapterIndex = 0;; ++AdapterIndex)
        {
            IDXGIAdapter1* Adapter = nullptr;
            const HRESULT AdapterResult = Factory->EnumAdapters1(AdapterIndex, &Adapter);
            if (AdapterResult == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }
            if (FAILED(AdapterResult) || !Adapter)
            {
                UE_LOG(LogTemp, Warning, TEXT("Failed to enumerate DXGI adapter %u: HRESULT=0x%08x"),
                    AdapterIndex,
                    static_cast<uint32>(AdapterResult));
                continue;
            }

            DXGI_ADAPTER_DESC1 Desc = {};
            if (SUCCEEDED(Adapter->GetDesc1(&Desc)))
            {
                UE_LOG(LogTemp, Log, TEXT("DXGI adapter[%u]%s: %s"),
                    AdapterIndex,
                    AdapterIndex == static_cast<uint32>(RequestedDeviceId) ? TEXT(" <- DirectML device_id") : TEXT(""),
                    *DxgiAdapterDescToString(Desc));
            }
            Adapter->Release();
        }

        Factory->Release();
    }
#endif

#if WITH_TENSORRT
    class FTrtLogger : public nvinfer1::ILogger
    {
    public:
        void log(Severity severity, const char* msg) noexcept override
        {
            if (severity > Severity::kWARNING)
            {
                return;
            }
            const TCHAR* Prefix = (severity == Severity::kERROR || severity == Severity::kINTERNAL_ERROR)
                ? TEXT("TensorRT Error")
                : TEXT("TensorRT");
            UE_LOG(LogTemp, Warning, TEXT("%s: %s"), Prefix, *FString(UTF8_TO_TCHAR(msg)));
        }
    };

    FTrtLogger GTrtLogger;
    constexpr const char* kTrtInputName = "images";
    constexpr const char* kTrtOutputName = "output0";

    template <typename TObject>
    void DestroyTrtObject(TObject*& Obj)
    {
        if (!Obj)
        {
            return;
        }
#if NV_TENSORRT_MAJOR >= 10
        delete Obj;
#else
        Obj->destroy();
#endif
        Obj = nullptr;
    }

    int64 CalcTrtVolume(const nvinfer1::Dims& Dims)
    {
        int64 Volume = 1;
        for (int i = 0; i < Dims.nbDims; ++i)
        {
            if (Dims.d[i] <= 0)
            {
                return -1;
            }
            Volume *= Dims.d[i];
        }
        return Volume;
    }

    FString TrtDimsToString(const nvinfer1::Dims& Dims)
    {
        FString Out = TEXT("[");
        for (int i = 0; i < Dims.nbDims; ++i)
        {
            if (i > 0)
            {
                Out += TEXT(", ");
            }
            Out += FString::FromInt(Dims.d[i]);
        }
        Out += TEXT("]");
        return Out;
    }
#endif

    float CalcIoU(const cv::Rect& A, const cv::Rect& B)
    {
        const int x1 = (std::max)(A.x, B.x);
        const int y1 = (std::max)(A.y, B.y);
        const int x2 = (std::min)(A.x + A.width, B.x + B.width);
        const int y2 = (std::min)(A.y + A.height, B.y + B.height);
        const int w = (std::max)(0, x2 - x1);
        const int h = (std::max)(0, y2 - y1);
        const float inter = static_cast<float>(w * h);
        const float unionArea = static_cast<float>(A.area() + B.area()) - inter;
        return unionArea > 0.0f ? (inter / unionArea) : 0.0f;
    }

    void RectToCorners(const cv::Rect& Rect, TArray<FVector2D>& OutCorners)
    {
        OutCorners.Reset();
        OutCorners.Add(FVector2D(Rect.x, Rect.y));
        OutCorners.Add(FVector2D(Rect.x + Rect.width, Rect.y));
        OutCorners.Add(FVector2D(Rect.x + Rect.width, Rect.y + Rect.height));
        OutCorners.Add(FVector2D(Rect.x, Rect.y + Rect.height));
    }

    void ApplyNms(const std::vector<cv::Rect>& Boxes, const std::vector<float>& Scores, float ScoreThreshold, float NmsThreshold, std::vector<int>& Indices)
    {
        Indices.clear();
        if (Boxes.empty() || Scores.empty())
        {
            return;
        }

        std::vector<int> Order;
        Order.reserve(Scores.size());
        for (int i = 0; i < static_cast<int>(Scores.size()); ++i)
        {
            if (Scores[i] >= ScoreThreshold)
            {
                Order.push_back(i);
            }
        }

        std::sort(Order.begin(), Order.end(), [&](int A, int B)
        {
            return Scores[A] > Scores[B];
        });

        for (int idx : Order)
        {
            bool bKeep = true;
            for (int kept : Indices)
            {
                if (CalcIoU(Boxes[idx], Boxes[kept]) > NmsThreshold)
                {
                    bKeep = false;
                    break;
                }
            }
            if (bKeep)
            {
                Indices.push_back(idx);
            }
        }
    }

    FORCEINLINE float Sigmoidf(float V)
    {
        const float Clamped = FMath::Clamp(V, -60.0f, 60.0f);
        return 1.0f / (1.0f + FMath::Exp(-Clamped));
    }

    FORCEINLINE bool IsClassIdLike(float V, int32 NumClassesHint)
    {
        if (!FMath::IsFinite(V))
        {
            return false;
        }

        const int32 Rounded = FMath::RoundToInt(V);
        if (FMath::Abs(V - static_cast<float>(Rounded)) > 0.01f || Rounded < 0)
        {
            return false;
        }

        return NumClassesHint <= 0 || Rounded < NumClassesHint;
    }

    FORCEINLINE float NormalizeConfidence(float Score)
    {
        if (!FMath::IsFinite(Score))
        {
            return -1.0f;
        }
        return (Score < 0.0f || Score > 1.0f) ? Sigmoidf(Score) : Score;
    }

    FString MatShapeToString(const cv::Mat& M)
    {
        FString Out = TEXT("[");
        for (int32 i = 0; i < M.dims; ++i)
        {
            if (i > 0)
            {
                Out += TEXT(", ");
            }
            Out += FString::FromInt(M.size[i]);
        }
        Out += TEXT("]");
        return Out;
    }

    FString TensorShapeToString(const std::vector<int64_t>& Shape)
    {
        FString Out = TEXT("[");
        for (size_t i = 0; i < Shape.size(); ++i)
        {
            if (i > 0)
            {
                Out += TEXT(", ");
            }
            Out += FString::Printf(TEXT("%lld"), static_cast<long long>(Shape[i]));
        }
        Out += TEXT("]");
        return Out;
    }

    bool CopyRgbFloatMatToNchw(const cv::Mat& ImageRgbFloat, float* OutData, int32 Width, int32 Height)
    {
        if (!OutData ||
            ImageRgbFloat.empty() ||
            ImageRgbFloat.cols != Width ||
            ImageRgbFloat.rows != Height ||
            ImageRgbFloat.type() != CV_32FC3)
        {
            return false;
        }

        const int32 PlaneSize = Width * Height;
        float* R = OutData;
        float* G = OutData + PlaneSize;
        float* B = OutData + (PlaneSize * 2);

        for (int32 Y = 0; Y < Height; ++Y)
        {
            const cv::Vec3f* Row = ImageRgbFloat.ptr<cv::Vec3f>(Y);
            const int32 RowOffset = Y * Width;
            for (int32 X = 0; X < Width; ++X)
            {
                const int32 Index = RowOffset + X;
                R[Index] = Row[X][0];
                G[Index] = Row[X][1];
                B[Index] = Row[X][2];
            }
        }

        return true;
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

    TArray<FString> GetRuntimeModelSearchDirectories()
    {
        TArray<FString> Directories;

        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("Models")));
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::LaunchDir(), TEXT("Models")));
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::ProjectDir(), TEXT("Models")));
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Models")));

        // Editor/development fallback. Packaged builds receive these files through RuntimeDependencies.
        AddUniqueNormalizedDirectory(Directories, FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("EagleEye")));

        return Directories;
    }

    bool RuntimeFileExists(const FString& Path)
    {
        return !Path.IsEmpty() && FPlatformFileManager::Get().GetPlatformFile().FileExists(*Path);
    }

    bool IsActorComponentDetectionMetricLoggingEnabled()
    {
        UEagleEyeDetectionSettings::LoadRuntimeConfig();
        const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
        return !Settings || Settings->bEnableDetectionMetricLogs;
    }

    FString NormalizeRuntimeFilePath(const FString& Path)
    {
        FString Normalized = FPaths::ConvertRelativePathToFull(Path);
        FPaths::NormalizeFilename(Normalized);
        return Normalized;
    }

    FString ResolveRuntimeFilePath(const FString& RequestedPath)
    {
        if (RequestedPath.IsEmpty())
        {
            return FString();
        }

        if (!FPaths::IsRelative(RequestedPath))
        {
            return NormalizeRuntimeFilePath(RequestedPath);
        }

        const FString DirectPath = NormalizeRuntimeFilePath(RequestedPath);
        if (RuntimeFileExists(DirectPath))
        {
            return DirectPath;
        }

        const FString CleanRequestedPath = RequestedPath.Replace(TEXT("\\"), TEXT("/"));
        for (const FString& Directory : GetRuntimeModelSearchDirectories())
        {
            const FString CandidatePath = NormalizeRuntimeFilePath(FPaths::Combine(Directory, CleanRequestedPath));
            if (RuntimeFileExists(CandidatePath))
            {
                return CandidatePath;
            }
        }

        const FString FileName = FPaths::GetCleanFilename(CleanRequestedPath);
        if (!FileName.Equals(CleanRequestedPath, ESearchCase::IgnoreCase))
        {
            for (const FString& Directory : GetRuntimeModelSearchDirectories())
            {
                const FString CandidatePath = NormalizeRuntimeFilePath(FPaths::Combine(Directory, FileName));
                if (RuntimeFileExists(CandidatePath))
                {
                    return CandidatePath;
                }
            }
        }

        return DirectPath;
    }

    std::string ToUtf8Path(const FString& Path)
    {
        return std::string(TCHAR_TO_UTF8(*Path));
    }

    const TCHAR* BackendToString(EDetectionInferenceBackend Backend)
    {
        switch (Backend)
        {
        case EDetectionInferenceBackend::Auto:
            return TEXT("Auto");
        case EDetectionInferenceBackend::TensorRT:
            return TEXT("TensorRT");
        case EDetectionInferenceBackend::ONNXRuntime:
            return TEXT("ONNX Runtime");
        case EDetectionInferenceBackend::OpenCVDNN:
            return TEXT("OpenCV DNN");
        default:
            return TEXT("Unknown");
        }
    }

    const TCHAR* OnnxProviderToString(EOnnxRuntimeExecutionProvider Provider)
    {
        switch (Provider)
        {
        case EOnnxRuntimeExecutionProvider::Auto:
            return TEXT("Auto");
        case EOnnxRuntimeExecutionProvider::DirectML:
            return TEXT("DirectML");
        case EOnnxRuntimeExecutionProvider::MIGraphX:
            return TEXT("MIGraphX");
        case EOnnxRuntimeExecutionProvider::CPU:
            return TEXT("CPU");
        default:
            return TEXT("Unknown");
        }
    }

    bool IsOnnxRuntimeProviderCompiled(EOnnxRuntimeExecutionProvider Provider)
    {
        switch (Provider)
        {
        case EOnnxRuntimeExecutionProvider::CPU:
            return true;
        case EOnnxRuntimeExecutionProvider::DirectML:
#if WITH_ONNXRUNTIME_DML
            return true;
#else
            return false;
#endif
        case EOnnxRuntimeExecutionProvider::MIGraphX:
#if WITH_ONNXRUNTIME_MIGRAPHX
            return true;
#else
            return false;
#endif
        default:
            return false;
        }
    }

    bool TryLoadRuntimeLibrary(const TArray<FString>& Candidates, FString& OutReason, void*& OutHandle)
    {
        if (OutHandle)
        {
            return true;
        }

        for (const FString& Candidate : Candidates)
        {
            if (Candidate.IsEmpty())
            {
                continue;
            }

            void* Handle = FPlatformProcess::GetDllHandle(*Candidate);
            if (Handle)
            {
                OutHandle = Handle;
                return true;
            }
        }

        OutReason = FString::Printf(TEXT("failed to load any of: %s"), *FString::Join(Candidates, TEXT(", ")));
        return false;
    }

#if PLATFORM_WINDOWS && WITH_ONNXRUNTIME_DML
    void* GDirectMLRuntimeHandle = nullptr;
#endif

#if PLATFORM_LINUX && WITH_ONNXRUNTIME_MIGRAPHX
    void* GMIGraphXRuntimeHandle = nullptr;
    void* GMIGraphXGpuRuntimeHandle = nullptr;
    void* GMIGraphXOnnxRuntimeHandle = nullptr;
    void* GMIGraphXTfRuntimeHandle = nullptr;
    void* GMIGraphXCRuntimeHandle = nullptr;
#endif

    bool IsOnnxRuntimeProviderRuntimeAvailable(EOnnxRuntimeExecutionProvider Provider, FString& OutReason)
    {
        switch (Provider)
        {
        case EOnnxRuntimeExecutionProvider::CPU:
            return true;
        case EOnnxRuntimeExecutionProvider::DirectML:
#if PLATFORM_WINDOWS && WITH_ONNXRUNTIME_DML
            return TryLoadRuntimeLibrary(
                {
                    FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Win64"), TEXT("DirectML.dll")),
                    FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("DirectML.dll")),
                    TEXT("DirectML.dll")
                },
                OutReason,
                GDirectMLRuntimeHandle);
#else
            OutReason = TEXT("DirectML runtime check is not available on this platform/build");
            return false;
#endif
        case EOnnxRuntimeExecutionProvider::MIGraphX:
#if PLATFORM_LINUX && WITH_ONNXRUNTIME_MIGRAPHX
            if (!TryLoadRuntimeLibrary(
                {
                    FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Linux"), TEXT("libmigraphx.so.2015000")),
                    FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Linux"), TEXT("libmigraphx.so")),
                    FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("libmigraphx.so.2015000")),
                    FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("libmigraphx.so")),
                    TEXT("libmigraphx.so.2015000"),
                    TEXT("libmigraphx.so")
                },
                OutReason,
                GMIGraphXRuntimeHandle))
            {
                return false;
            }
            if (!TryLoadRuntimeLibrary(
                {
                    FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Linux"), TEXT("libmigraphx_gpu.so.2015000")),
                    FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Linux"), TEXT("libmigraphx_gpu.so")),
                    FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("libmigraphx_gpu.so.2015000")),
                    FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("libmigraphx_gpu.so")),
                    TEXT("libmigraphx_gpu.so.2015000"),
                    TEXT("libmigraphx_gpu.so")
                },
                OutReason,
                GMIGraphXGpuRuntimeHandle))
            {
                return false;
            }
            if (!TryLoadRuntimeLibrary(
                {
                    FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Linux"), TEXT("libmigraphx_onnx.so.2015000")),
                    FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Linux"), TEXT("libmigraphx_onnx.so")),
                    FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("libmigraphx_onnx.so.2015000")),
                    FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("libmigraphx_onnx.so")),
                    TEXT("libmigraphx_onnx.so.2015000"),
                    TEXT("libmigraphx_onnx.so")
                },
                OutReason,
                GMIGraphXOnnxRuntimeHandle))
            {
                return false;
            }
            if (!TryLoadRuntimeLibrary(
                {
                    FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Linux"), TEXT("libmigraphx_tf.so.2015000")),
                    FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Linux"), TEXT("libmigraphx_tf.so")),
                    FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("libmigraphx_tf.so.2015000")),
                    FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("libmigraphx_tf.so")),
                    TEXT("libmigraphx_tf.so.2015000"),
                    TEXT("libmigraphx_tf.so")
                },
                OutReason,
                GMIGraphXTfRuntimeHandle))
            {
                return false;
            }
            if (!TryLoadRuntimeLibrary(
                {
                    FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Linux"), TEXT("libmigraphx_c.so.3")),
                    FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Linux"), TEXT("libmigraphx_c.so")),
                    FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("libmigraphx_c.so.3")),
                    FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("libmigraphx_c.so")),
                    TEXT("libmigraphx_c.so.3"),
                    TEXT("libmigraphx_c.so")
                },
                OutReason,
                GMIGraphXCRuntimeHandle))
            {
                return false;
            }
            return true;
#else
            OutReason = TEXT("MIGraphX runtime check is not available on this platform/build");
            return false;
#endif
        default:
            OutReason = TEXT("unknown ONNX Runtime provider");
            return false;
        }
    }

    enum class EDetectedGpuVendor : uint8
    {
        Nvidia,
        AMD,
        Other
    };

    enum class EDetectedOperatingSystem : uint8
    {
        Windows,
        Linux,
        Mac,
        Other
    };

    EDetectedGpuVendor DetectGpuVendor(const FString& AdapterName)
    {
        if (AdapterName.Contains(TEXT("NVIDIA"), ESearchCase::IgnoreCase) ||
            AdapterName.Contains(TEXT("GeForce"), ESearchCase::IgnoreCase) ||
            AdapterName.Contains(TEXT("RTX"), ESearchCase::IgnoreCase) ||
            AdapterName.Contains(TEXT("Quadro"), ESearchCase::IgnoreCase) ||
            AdapterName.Contains(TEXT("Tesla"), ESearchCase::IgnoreCase))
        {
            return EDetectedGpuVendor::Nvidia;
        }

        if (AdapterName.Contains(TEXT("AMD"), ESearchCase::IgnoreCase) ||
            AdapterName.Contains(TEXT("Radeon"), ESearchCase::IgnoreCase) ||
            AdapterName.Contains(TEXT("Advanced Micro Devices"), ESearchCase::IgnoreCase))
        {
            return EDetectedGpuVendor::AMD;
        }

        return EDetectedGpuVendor::Other;
    }

    EDetectedOperatingSystem DetectOperatingSystem()
    {
#if PLATFORM_WINDOWS
        return EDetectedOperatingSystem::Windows;
#elif PLATFORM_LINUX
        return EDetectedOperatingSystem::Linux;
#elif PLATFORM_MAC
        return EDetectedOperatingSystem::Mac;
#else
        return EDetectedOperatingSystem::Other;
#endif
    }

    const TCHAR* GpuVendorToString(EDetectedGpuVendor Vendor)
    {
        switch (Vendor)
        {
        case EDetectedGpuVendor::Nvidia:
            return TEXT("NVIDIA");
        case EDetectedGpuVendor::AMD:
            return TEXT("AMD");
        case EDetectedGpuVendor::Other:
        default:
            return TEXT("Other");
        }
    }

    const TCHAR* OperatingSystemToString(EDetectedOperatingSystem OperatingSystem)
    {
        switch (OperatingSystem)
        {
        case EDetectedOperatingSystem::Windows:
            return TEXT("Windows");
        case EDetectedOperatingSystem::Linux:
            return TEXT("Linux");
        case EDetectedOperatingSystem::Mac:
            return TEXT("macOS");
        case EDetectedOperatingSystem::Other:
        default:
            return TEXT("Other");
        }
    }

    void ConfigureOpenCVDNNForCPU(cv::dnn::Net& Net)
    {
        Net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        Net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    }

    bool HasOpenCVDNNCudaDevice(int32& OutDeviceCount, FString& OutReason)
    {
        OutDeviceCount = 0;
        try
        {
            OutDeviceCount = cv::cuda::getCudaEnabledDeviceCount();
        }
        catch (const cv::Exception& e)
        {
            OutReason = FString(e.what());
            return false;
        }

        if (OutDeviceCount <= 0)
        {
            OutReason = TEXT("no CUDA-capable OpenCV device found");
            return false;
        }

        return true;
    }

    bool ConfigureOpenCVDNNBackend(cv::dnn::Net& Net, bool bPreferCUDA, bool bUseFP16, FString* OutExecutionProviderLabel = nullptr)
    {
        if (bPreferCUDA)
        {
            int32 CudaDeviceCount = 0;
            FString CudaUnavailableReason;
            if (HasOpenCVDNNCudaDevice(CudaDeviceCount, CudaUnavailableReason))
            {
                try
                {
                    Net.setPreferableTarget(bUseFP16 ? cv::dnn::DNN_TARGET_CUDA_FP16 : cv::dnn::DNN_TARGET_CUDA);
                    Net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                    if (OutExecutionProviderLabel)
                    {
                        *OutExecutionProviderLabel = bUseFP16 ? TEXT("CUDA_FP16") : TEXT("CUDA");
                    }
                    UE_LOG(LogTemp, Log, TEXT("OpenCV DNN backend configured: CUDA%s (devices=%d)"),
                        bUseFP16 ? TEXT(" FP16") : TEXT(""),
                        CudaDeviceCount);
                    return true;
                }
                catch (const cv::Exception& e)
                {
                    CudaUnavailableReason = FString(e.what());
                }
            }

            UE_LOG(LogTemp, Warning, TEXT("OpenCV DNN CUDA requested but unavailable (%s). Using CPU backend."),
                *CudaUnavailableReason);
        }

        try
        {
            ConfigureOpenCVDNNForCPU(Net);
            if (OutExecutionProviderLabel)
            {
                *OutExecutionProviderLabel = TEXT("CPU");
            }
            UE_LOG(LogTemp, Log, TEXT("OpenCV DNN backend configured: CPU"));
            return true;
        }
        catch (const cv::Exception& e)
        {
            UE_LOG(LogTemp, Error, TEXT("OpenCV DNN CPU backend setup failed: %s"), *FString(e.what()));
            return false;
        }
    }

    constexpr double FfmpegStopTimeoutSeconds = 3.0;
    constexpr double FfmpegFastShutdownTimeoutSeconds = 1.0;
    constexpr double FfmpegTerminateTimeoutSeconds = 0.5;
    TSet<FString> ResetFrameTimingLogsThisRun;
    TSet<FString> ResetFovDetectionMetricsTablesThisRun;

    FString EscapeActorComponentCsvField(const FString& Value)
    {
        if (!Value.Contains(TEXT(",")) && !Value.Contains(TEXT("\"")) && !Value.Contains(TEXT("\n")) && !Value.Contains(TEXT("\r")))
        {
            return Value;
        }

        FString Escaped = Value;
        Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
        return FString::Printf(TEXT("\"%s\""), *Escaped);
    }

    FString SanitizeFrameTimingFileFragment(const FString& Value)
    {
        FString Sanitized;
        Sanitized.Reserve(Value.Len());
        for (const TCHAR Character : Value)
        {
            Sanitized.AppendChar(FChar::IsAlnum(Character) ? Character : TEXT('_'));
        }

        while (Sanitized.Contains(TEXT("__")))
        {
            Sanitized.ReplaceInline(TEXT("__"), TEXT("_"));
        }

        Sanitized.TrimStartAndEndInline();
        while (Sanitized.StartsWith(TEXT("_")))
        {
            Sanitized.RemoveAt(0, 1, EAllowShrinking::No);
        }
        while (Sanitized.EndsWith(TEXT("_")))
        {
            Sanitized.RemoveAt(Sanitized.Len() - 1, 1, EAllowShrinking::No);
        }
        return Sanitized.IsEmpty() ? TEXT("Unknown") : Sanitized;
    }
}

UMyActorComponent::UMyActorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}

void UMyActorComponent::SetCaptureResolution(int32 InWidth, int32 InHeight)
{
    CaptureWidth = FMath::Max(160, InWidth);
    CaptureHeight = FMath::Max(160, InHeight);

    if (OwnerCaptureRenderTarget &&
        (OwnerCaptureRenderTarget->SizeX != CaptureWidth || OwnerCaptureRenderTarget->SizeY != CaptureHeight))
    {
        PendingOwnerCameraReadback.Reset();
        PendingOwnerCameraReadbackWidth = 0;
        PendingOwnerCameraReadbackHeight = 0;
        PendingOwnerCameraReadbackSubmitTimeSeconds = 0.0;

        OwnerCaptureRenderTarget->ResizeTarget(CaptureWidth, CaptureHeight);
        OwnerCaptureRenderTarget->UpdateResourceImmediate(true);
    }
}

void UMyActorComponent::SetCaptureFPS(float InCaptureFPS)
{
    CaptureFPS = FMath::Clamp(InCaptureFPS, 1.0f, 120.0f);

    UWorld* World = GetWorld();
    if (!World || !TimerHandle_Capture.IsValid())
    {
        return;
    }

    const float CaptureInterval = 1.0f / CaptureFPS;
    World->GetTimerManager().SetTimer(
        TimerHandle_Capture,
        this,
        &UMyActorComponent::TickCapture,
        CaptureInterval,
        true,
        CaptureInterval);
}

void UMyActorComponent::SetUseOwnerCameraCapture(bool bEnabled)
{
    if (bUseOwnerCameraCapture == bEnabled)
    {
        return;
    }

    bUseOwnerCameraCapture = bEnabled;
    if (!bUseOwnerCameraCapture)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
            {
                ShareSubsystem->UnregisterDetector(GetOwner());
            }
        }
        ReleaseOwnerCameraCaptureResources();
    }
    else if (UWorld* World = GetWorld())
    {
        if (UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
        {
            ShareSubsystem->RegisterDetector(GetOwner());
        }
    }
}

void UMyActorComponent::SetRecordOwnerCameraCaptureVideo(bool bEnabled)
{
    if (bRecordOwnerCameraCaptureVideo == bEnabled)
    {
        return;
    }

    bRecordOwnerCameraCaptureVideo = bEnabled;

    if (!bRecordOwnerCameraCaptureVideo)
    {
        CloseOwnerCameraVideoWriter();
    }
}

void UMyActorComponent::SetLogFovDetectionMetrics(bool bEnabled)
{
    bLogFovDetectionMetrics = bEnabled;
    if (bLogFovDetectionMetrics)
    {
        InitFovDetectionMetricsTable();
    }
}

bool UMyActorComponent::ShouldLogFrameTimings() const
{
    UEagleEyeDetectionSettings::LoadRuntimeConfig();
    const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
    return bLogFrameTimings && (!Settings || Settings->bEnableDetectionPerformanceLogs);
}

FString UMyActorComponent::GetFrameTimingRuntimeFileSuffix() const
{
    switch (EffectiveInferenceBackend)
    {
    case EDetectionInferenceBackend::TensorRT:
        return TEXT("TensorRT");
    case EDetectionInferenceBackend::ONNXRuntime:
#if WITH_ONNXRUNTIME
        switch (EffectiveOnnxRuntimeExecutionProvider)
        {
        case EOnnxRuntimeExecutionProvider::DirectML:
            return TEXT("ONNXRuntime_DirectML");
        case EOnnxRuntimeExecutionProvider::MIGraphX:
            return TEXT("ONNXRuntime_MIGraphX");
        case EOnnxRuntimeExecutionProvider::CPU:
            return TEXT("ONNXRuntime_CPU");
        case EOnnxRuntimeExecutionProvider::Auto:
        default:
            return TEXT("ONNXRuntime_Auto");
        }
#else
        return TEXT("ONNXRuntime");
#endif
    case EDetectionInferenceBackend::OpenCVDNN:
        return TEXT("OpenCVDNN");
    case EDetectionInferenceBackend::Auto:
    default:
        return TEXT("Auto");
    }
}

FString UMyActorComponent::GetFrameTimingRuntimeLabel() const
{
    if (EffectiveInferenceBackend == EDetectionInferenceBackend::ONNXRuntime)
    {
#if WITH_ONNXRUNTIME
        return FString::Printf(TEXT("ONNX Runtime %s"), OnnxProviderToString(EffectiveOnnxRuntimeExecutionProvider));
#else
        return TEXT("ONNX Runtime");
#endif
    }

    return BackendToString(EffectiveInferenceBackend);
}

FString UMyActorComponent::GetFrameTimingExecutionProviderLabel() const
{
    switch (EffectiveInferenceBackend)
    {
    case EDetectionInferenceBackend::ONNXRuntime:
#if WITH_ONNXRUNTIME
        return OnnxProviderToString(EffectiveOnnxRuntimeExecutionProvider);
#else
        return TEXT("Unavailable");
#endif
    case EDetectionInferenceBackend::OpenCVDNN:
        return OpenCVDNNExecutionProviderLabel.IsEmpty() ? TEXT("CPU") : OpenCVDNNExecutionProviderLabel;
    case EDetectionInferenceBackend::TensorRT:
        return TEXT("TensorRT");
    case EDetectionInferenceBackend::Auto:
    default:
        return TEXT("Auto");
    }
}

FString UMyActorComponent::GetFrameTimingOperatingSystemLabel() const
{
    return OperatingSystemToString(DetectOperatingSystem());
}

FString UMyActorComponent::GetFrameTimingDetectionModelLabel() const
{
    if (!ActiveDetectionModelPath.IsEmpty())
    {
        return ActiveDetectionModelPath;
    }

    if (!WeightsPath.empty())
    {
        return FString(WeightsPath.c_str());
    }

    return ModelPathOverride.IsEmpty() ? TEXT("unknown") : ModelPathOverride;
}

FString UMyActorComponent::ResolveFrameTimeCsvPathForCurrentRuntime() const
{
    if (!FrameTimeCsvPath.IsEmpty())
    {
        return FrameTimeCsvPath;
    }

    const FString BackendFragment = SanitizeFrameTimingFileFragment(GetFrameTimingRuntimeLabel());
    const FString ProviderFragment = SanitizeFrameTimingFileFragment(GetFrameTimingExecutionProviderLabel());
    const FString OsFragment = SanitizeFrameTimingFileFragment(GetFrameTimingOperatingSystemLabel());
    return FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("Profiling"),
        FString::Printf(TEXT("%s_%s_%s.csv"), *BackendFragment, *ProviderFragment, *OsFragment));
}

void UMyActorComponent::InitFrameTimingLog()
{
    if (!bRecordFrameTimes || !bIsModelLoaded)
    {
        return;
    }

    FScopeLock Lock(&FrameTimeLogMutex);
    const FString DesiredFrameTimeCsvPath = ResolveFrameTimeCsvPathForCurrentRuntime();
    if (bFrameTimingLogInitialized && ResolvedFrameTimeCsvPath == DesiredFrameTimeCsvPath)
    {
        return;
    }

    if (bFrameTimingLogInitialized && FrameTimeLogBuffer.Num() > 0 && !ResolvedFrameTimeCsvPath.IsEmpty())
    {
        FString Batch;
        Batch.Reserve(FrameTimeLogBuffer.Num() * 96);
        for (const FString& Line : FrameTimeLogBuffer)
        {
            Batch += Line;
        }

        FFileHelper::SaveStringToFile(
            Batch,
            *ResolvedFrameTimeCsvPath,
            FFileHelper::EEncodingOptions::AutoDetect,
            &IFileManager::Get(),
            FILEWRITE_Append);
        FrameTimeLogBuffer.Reset();
    }

    ResolvedFrameTimeCsvPath = DesiredFrameTimeCsvPath;

    const FString Directory = FPaths::GetPath(ResolvedFrameTimeCsvPath);
    if (!Directory.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*Directory, true);
    }

    if (bResetFrameTimeLogOnBeginPlay &&
        !ResetFrameTimingLogsThisRun.Contains(ResolvedFrameTimeCsvPath) &&
        IFileManager::Get().FileExists(*ResolvedFrameTimeCsvPath))
    {
        IFileManager::Get().Delete(*ResolvedFrameTimeCsvPath, false, true, true);
    }
    ResetFrameTimingLogsThisRun.Add(ResolvedFrameTimeCsvPath);

    if (!IFileManager::Get().FileExists(*ResolvedFrameTimeCsvPath))
    {
        const FString Header = TEXT("frame_sequence,backend,execution_provider,os,detection_model,source_width,source_height,total_ms,inference_ms,detections,detection_status,detection_correctness\n");
        FFileHelper::SaveStringToFile(Header, *ResolvedFrameTimeCsvPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get());
    }

    FrameTimeLogBuffer.Reset();
    bFrameTimingLogInitialized = true;
    if (ShouldLogFrameTimings())
    {
        UE_LOG(LogTemp, Log, TEXT("Frame timing log enabled: %s"), *ResolvedFrameTimeCsvPath);
    }
}

void UMyActorComponent::InitFovDetectionMetricsTable()
{
    if (!bLogFovDetectionMetrics || bFovDetectionMetricsTableInitialized)
    {
        return;
    }

    ResolvedFovDetectionMetricsCsvPath = FovDetectionMetricsCsvPath;
    if (ResolvedFovDetectionMetricsCsvPath.IsEmpty())
    {
        ResolvedFovDetectionMetricsCsvPath = FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("Profiling"),
            TEXT("FovDetectionMetrics.csv"));
    }

    const FString Directory = FPaths::GetPath(ResolvedFovDetectionMetricsCsvPath);
    if (!Directory.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*Directory, true);
    }

    if (bResetFovDetectionMetricsTableOnBeginPlay &&
        !ResetFovDetectionMetricsTablesThisRun.Contains(ResolvedFovDetectionMetricsCsvPath) &&
        IFileManager::Get().FileExists(*ResolvedFovDetectionMetricsCsvPath))
    {
        IFileManager::Get().Delete(*ResolvedFovDetectionMetricsCsvPath, false, true, true);
    }
    ResetFovDetectionMetricsTablesThisRun.Add(ResolvedFovDetectionMetricsCsvPath);

    if (!IFileManager::Get().FileExists(*ResolvedFovDetectionMetricsCsvPath))
    {
        const FString Header = TEXT("timestamp,world_seconds,owner,source,status,outcome,expected_in_fov,actual_person_detected,distance,horizontal_angle,vertical_angle,expected_pixel_x,expected_pixel_y,source_width,source_height,detection_count,tp,tn,fp,fn,unavailable\n");
        FFileHelper::SaveStringToFile(
            Header,
            *ResolvedFovDetectionMetricsCsvPath,
            FFileHelper::EEncodingOptions::AutoDetect,
            &IFileManager::Get());
    }

    bFovDetectionMetricsTableInitialized = true;
    if (IsActorComponentDetectionMetricLoggingEnabled())
    {
        UE_LOG(LogTemp, Log, TEXT("FOV detection metrics table enabled: %s"), *ResolvedFovDetectionMetricsCsvPath);
    }
}

void UMyActorComponent::AppendFovDetectionMetricsTableRow(
    const TCHAR* SourceLabel,
    const TCHAR* Status,
    const TCHAR* Outcome,
    bool bExpectedInFov,
    bool bActualPersonDetected,
    float Distance,
    float HorizontalAngleDegrees,
    float VerticalAngleDegrees,
    const FVector2D& ExpectedPixel,
    int32 SourceWidth,
    int32 SourceHeight,
    int32 DetectionCount)
{
    if (!bLogFovDetectionMetrics)
    {
        return;
    }

    InitFovDetectionMetricsTable();
    if (!bFovDetectionMetricsTableInitialized)
    {
        return;
    }

    const FString Line = FString::Printf(
        TEXT("%s,%.4f,%s,%s,%s,%s,%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d\n"),
        *FDateTime::Now().ToIso8601(),
        GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f,
        *EscapeActorComponentCsvField(GetNameSafe(GetOwner())),
        *EscapeActorComponentCsvField(FString(SourceLabel)),
        *EscapeActorComponentCsvField(Status),
        *EscapeActorComponentCsvField(Outcome),
        bExpectedInFov ? TEXT("true") : TEXT("false"),
        bActualPersonDetected ? TEXT("true") : TEXT("false"),
        Distance,
        HorizontalAngleDegrees,
        VerticalAngleDegrees,
        ExpectedPixel.X,
        ExpectedPixel.Y,
        SourceWidth,
        SourceHeight,
        DetectionCount,
        FovDetectionTruePositiveCount,
        FovDetectionTrueNegativeCount,
        FovDetectionFalsePositiveCount,
        FovDetectionFalseNegativeCount,
        FovDetectionUnavailableSampleCount);

    FFileHelper::SaveStringToFile(
        Line,
        *ResolvedFovDetectionMetricsCsvPath,
        FFileHelper::EEncodingOptions::AutoDetect,
        &IFileManager::Get(),
        FILEWRITE_Append);
}

void UMyActorComponent::FlushFrameTimingLog(bool bForce)
{
    if (!bRecordFrameTimes)
    {
        return;
    }

    FScopeLock Lock(&FrameTimeLogMutex);
    if (!bFrameTimingLogInitialized || FrameTimeLogBuffer.Num() == 0)
    {
        return;
    }

    const int32 FlushEvery = FMath::Max(1, FrameTimeFlushInterval);
    if (!bForce && FrameTimeLogBuffer.Num() < FlushEvery)
    {
        return;
    }

    FString Batch;
    Batch.Reserve(FrameTimeLogBuffer.Num() * 96);
    for (const FString& Line : FrameTimeLogBuffer)
    {
        Batch += Line;
    }

    FFileHelper::SaveStringToFile(
        Batch,
        *ResolvedFrameTimeCsvPath,
        FFileHelper::EEncodingOptions::AutoDetect,
        &IFileManager::Get(),
        FILEWRITE_Append);

    FrameTimeLogBuffer.Reset();
}

void UMyActorComponent::AppendFrameTimingLogLine(
    int32 Sequence,
    int32 Width,
    int32 Height,
    int32 DetectionCount,
    double TotalMs,
    double InferMs,
    bool bDetectionSucceeded,
    bool bHasDetectionEvaluation,
    bool bExpectedInFov)
{
    if (!bRecordFrameTimes)
    {
        return;
    }

    InitFrameTimingLog();

    const FString BackendLabel = GetFrameTimingRuntimeLabel();
    const FString ExecutionProviderLabel = GetFrameTimingExecutionProviderLabel();
    const FString OperatingSystemLabel = GetFrameTimingOperatingSystemLabel();
    const FString DetectionModelLabel = GetFrameTimingDetectionModelLabel();
    const TCHAR* DetectionStatus = bDetectionSucceeded ? TEXT("success") : TEXT("fail");
    const TCHAR* DetectionCorrectness = bHasDetectionEvaluation
        ? (bDetectionSucceeded == bExpectedInFov ? TEXT("correct") : TEXT("incorrect"))
        : TEXT("unavailable");
    const FString Line = FString::Printf(
        TEXT("%d,%s,%s,%s,%s,%d,%d,%.4f,%.4f,%d,%s,%s\n"),
        Sequence,
        *EscapeActorComponentCsvField(BackendLabel),
        *EscapeActorComponentCsvField(ExecutionProviderLabel),
        *EscapeActorComponentCsvField(OperatingSystemLabel),
        *EscapeActorComponentCsvField(DetectionModelLabel),
        Width,
        Height,
        TotalMs,
        InferMs,
        DetectionCount,
        DetectionStatus,
        DetectionCorrectness);

    {
        FScopeLock Lock(&FrameTimeLogMutex);
        if (!bFrameTimingLogInitialized)
        {
            return;
        }
        FrameTimeLogBuffer.Add(Line);
    }

    FlushFrameTimingLog(false);
}

void UMyActorComponent::get_class_names() {
    ClassNames.clear();

    std::ifstream file(NamesPath);
    if (!file.is_open())
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to open class names file: %s"), *FString(NamesPath.c_str()));
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        ClassNames.push_back(line);
    }
}

void UMyActorComponent::BeginPlay() {
    Super::BeginPlay();
    bIsEndingPlay.store(false);
    bInferenceShutdownRequested.store(false);
    AdvanceOwnerCameraReadbackGeneration();
    ResetOwnerCameraReadback();
    OwnerCameraReadbackWarmupEndSeconds = FPlatformTime::Seconds() + 0.5;

    ApplyProjectDetectionSettings();
    ApplySharedVisionFrameSourceSettings();

    const bool bNeedsModelInitialization = !bUseFovOnlyPersonDetection && (!bUseSharedVisionModel || bSharedVisionModelHost);
    if (bNeedsModelInitialization)
    {
        const EDetectedOperatingSystem DetectedOS = DetectOperatingSystem();
        const EDetectedGpuVendor DetectedGpu = DetectGpuVendor(GRHIAdapterName);
        UE_LOG(LogTemp, Log, TEXT("Inference backend requested: %s (ONNX provider: %s, OS: %s, GPU: %s, RHI adapter: %s, host=%s)"),
            BackendToString(InferenceBackend),
            OnnxProviderToString(OnnxRuntimeExecutionProvider),
            OperatingSystemToString(DetectedOS),
            GpuVendorToString(DetectedGpu),
            *GRHIAdapterName,
            bSharedVisionModelHost ? TEXT("true") : TEXT("false"));

        ResolveConfiguredRuntimePaths();
        get_class_names();
        InitFrameTimingLog();
        if (!bUseSharedVisionModel && !bSharedVisionModelHost)
        {
            StartWorker();
        }
    }

    InitFovDetectionMetricsTable();

    if (bSharedVisionModelHost)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowVisionSubsystem* VisionSubsystem = World->GetSubsystem<UCrowVisionSubsystem>())
            {
                VisionSubsystem->RegisterModelHost(this);
            }
        }
        return;
    }

    if (bUseOwnerCameraCapture)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
            {
                ShareSubsystem->RegisterDetector(GetOwner());
            }
        }
    }

    const float ClampedCaptureFPS = FMath::Clamp(CaptureFPS, 1.0f, 120.0f);
    const float CaptureInterval = 1.0f / ClampedCaptureFPS;
    float InitialDelay = 0.f;
    if (bStaggerInitialCapture)
    {
        FRandomStream DelayStream(static_cast<int32>(GetUniqueID()));
        InitialDelay = DelayStream.FRandRange(0.f, FMath::Max(CaptureInterval, MaxInitialCaptureDelay));
    }

    GetWorld()->GetTimerManager().SetTimer(
        TimerHandle_Capture, this, &UMyActorComponent::TickCapture,
        CaptureInterval, true, InitialDelay
    );
}

void UMyActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    bIsEndingPlay.store(true);
    bInferenceShutdownRequested.store(true);
    RequestOnnxRuntimeInferenceTerminate();
    ++SharedVisionRequestSerial;
    AdvanceOwnerCameraReadbackGeneration();

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimerHandle_Capture);
    }

    ResetOwnerCameraReadback();

    CloseOwnerCameraVideoWriter(false);
    ReleaseOwnerCameraCaptureResources();

    if (bSharedVisionModelHost)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowVisionSubsystem* VisionSubsystem = World->GetSubsystem<UCrowVisionSubsystem>())
            {
                VisionSubsystem->UnregisterModelHost(this);
            }
        }
        StopWorker();
        Super::EndPlay(EndPlayReason);
        return;
    }

    if (bUseOwnerCameraCapture)
    {
        if (UWorld* World = GetWorld())
        {
            if (UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
            {
                ShareSubsystem->UnregisterDetector(GetOwner());
            }
        }
    }

    if (!bUseSharedVisionModel && !bSharedVisionModelHost)
    {
        StopWorker();
    }
    else
    {
        FlushFrameTimingLog(true);
    }
    Super::EndPlay(EndPlayReason);
}

void UMyActorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bUseFovOnlyPersonDetection || bUseSharedVisionModel || bSharedVisionModelHost)
    {
        return;
    }

    // Pull latest detections every frame to minimize overlay latency.
    CopyResultsFromWorker();
}

void UMyActorComponent::TickCapture() {
    if (bIsEndingPlay.load())
    {
        return;
    }

    ApplySharedVisionFrameSourceSettings();

    if (bUseFovOnlyPersonDetection)
    {
        PublishFovOnlyDetectionFrame();
        return;
    }

    const bool bSkipOwnerCameraDetection = bUseOwnerCameraCapture && ShouldSkipOwnerCameraCapture();
    if (bSkipOwnerCameraDetection && !ShouldRecordOwnerCameraVideo())
    {
        ClearPublishedResults();
        return;
    }

    // If the worker hasn't consumed the last captured frame yet, don't capture another.
    {
        FScopeLock Lock(&FrameMutex);
        if (LatestFrame.IsValid())
        {
            return;
        }
    }

    CaptureAndEnqueue(!bSkipOwnerCameraDetection);
}

bool UMyActorComponent::CaptureViewportToPixels(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
    OutWidth = 0;
    OutHeight = 0;

    if (!GEngine || !GEngine->GameViewport)
    {
        return false;
    }

    FViewport* Viewport = GEngine->GameViewport->Viewport;
    if (!Viewport)
    {
        return false;
    }

    const FIntPoint Size = Viewport->GetSizeXY();
    if (Size.X <= 0 || Size.Y <= 0)
    {
        return false;
    }

    OutPixels.Reset();
    if (!Viewport->ReadPixels(OutPixels))
    {
        return false;
    }

    if (OutPixels.Num() != Size.X * Size.Y)
    {
        return false;
    }

    OutWidth = Size.X;
    OutHeight = Size.Y;
    return true;
}

bool UMyActorComponent::EnsureOwnerCameraCapture()
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return false;
    }

    UCameraComponent* OwnerCamera = Owner->FindComponentByClass<UCameraComponent>();
    if (!OwnerCamera)
    {
        return false;
    }

    const int32 DesiredCaptureWidth = FMath::Max(160, CaptureWidth);
    const int32 DesiredCaptureHeight = FMath::Max(160, CaptureHeight);

    if (!OwnerCaptureRenderTarget)
    {
        OwnerCaptureRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("OwnerCameraDetectionRenderTarget"));
        OwnerCaptureRenderTarget->RenderTargetFormat = RTF_RGBA8;
        OwnerCaptureRenderTarget->ClearColor = FLinearColor::Black;
        OwnerCaptureRenderTarget->InitAutoFormat(DesiredCaptureWidth, DesiredCaptureHeight);
        OwnerCaptureRenderTarget->UpdateResourceImmediate(true);
    }
    else if (OwnerCaptureRenderTarget->SizeX != DesiredCaptureWidth || OwnerCaptureRenderTarget->SizeY != DesiredCaptureHeight)
    {
        PendingOwnerCameraReadback.Reset();
        PendingOwnerCameraReadbackWidth = 0;
        PendingOwnerCameraReadbackHeight = 0;
        PendingOwnerCameraReadbackSubmitTimeSeconds = 0.0;
        OwnerCaptureRenderTarget->ResizeTarget(DesiredCaptureWidth, DesiredCaptureHeight);
        OwnerCaptureRenderTarget->UpdateResourceImmediate(true);
    }

    constexpr ETextureRenderTargetFormat DesiredDepthRenderTargetFormat = RTF_RGBA16f;
    if (!OwnerDepthRenderTarget)
    {
        OwnerDepthRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("OwnerCameraDepthRenderTarget"));
        OwnerDepthRenderTarget->RenderTargetFormat = DesiredDepthRenderTargetFormat;
        OwnerDepthRenderTarget->ClearColor = FLinearColor::Black;
        OwnerDepthRenderTarget->InitAutoFormat(DesiredCaptureWidth, DesiredCaptureHeight);
        OwnerDepthRenderTarget->UpdateResourceImmediate(true);
    }
    else if (OwnerDepthRenderTarget->RenderTargetFormat != DesiredDepthRenderTargetFormat ||
        OwnerDepthRenderTarget->SizeX != DesiredCaptureWidth ||
        OwnerDepthRenderTarget->SizeY != DesiredCaptureHeight)
    {
        PendingOwnerCameraReadbackDepth.Reset();
        PendingOwnerCameraReadbackDepthWidth = 0;
        PendingOwnerCameraReadbackDepthHeight = 0;
        ClearLastFrameSceneDepth();
        OwnerDepthRenderTarget->RenderTargetFormat = DesiredDepthRenderTargetFormat;
        OwnerDepthRenderTarget->InitAutoFormat(DesiredCaptureWidth, DesiredCaptureHeight);
        OwnerDepthRenderTarget->UpdateResourceImmediate(true);
    }

    if (!OwnerSceneCapture)
    {
        OwnerSceneCapture = NewObject<UScreenCaptureComponent>(Owner, TEXT("OwnerCameraDetectionCapture"));
        OwnerSceneCapture->SetupAttachment(OwnerCamera);
        OwnerSceneCapture->RegisterComponent();
        OwnerSceneCapture->SetRelativeTransform(FTransform::Identity);
        OwnerSceneCapture->TextureTarget = OwnerCaptureRenderTarget;
        OwnerSceneCapture->bCaptureEveryFrame = false;
        OwnerSceneCapture->bCaptureOnMovement = false;
        OwnerSceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    }

    if (!OwnerDepthSceneCapture)
    {
        OwnerDepthSceneCapture = NewObject<UScreenCaptureComponent>(Owner, TEXT("OwnerCameraDepthCapture"));
        OwnerDepthSceneCapture->SetupAttachment(OwnerCamera);
        OwnerDepthSceneCapture->RegisterComponent();
        OwnerDepthSceneCapture->SetRelativeTransform(FTransform::Identity);
        OwnerDepthSceneCapture->TextureTarget = OwnerDepthRenderTarget;
        OwnerDepthSceneCapture->bCaptureEveryFrame = false;
        OwnerDepthSceneCapture->bCaptureOnMovement = false;
        OwnerDepthSceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
    }

    OwnerSceneCapture->FOVAngle = OwnerCamera->FieldOfView;
    OwnerSceneCapture->SetWorldLocationAndRotation(OwnerCamera->GetComponentLocation(), OwnerCamera->GetComponentRotation());
    OwnerSceneCapture->TextureTarget = OwnerCaptureRenderTarget;
    OwnerSceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    OwnerDepthSceneCapture->FOVAngle = OwnerCamera->FieldOfView;
    OwnerDepthSceneCapture->SetWorldLocationAndRotation(OwnerCamera->GetComponentLocation(), OwnerCamera->GetComponentRotation());
    OwnerDepthSceneCapture->TextureTarget = OwnerDepthRenderTarget;
    OwnerDepthSceneCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
    ConfigureOwnerCaptureVisibility(Owner);
    return true;
}

void UMyActorComponent::ReleaseOwnerCameraCaptureResources()
{
    ResetOwnerCameraReadback();
    ClearLastFrameSceneDepth();

    if (OwnerSceneCapture)
    {
        OwnerSceneCapture->TextureTarget = nullptr;
        OwnerSceneCapture->DestroyComponent();
        OwnerSceneCapture = nullptr;
    }

    if (OwnerDepthSceneCapture)
    {
        OwnerDepthSceneCapture->TextureTarget = nullptr;
        OwnerDepthSceneCapture->DestroyComponent();
        OwnerDepthSceneCapture = nullptr;
    }

    if (OwnerCaptureRenderTarget)
    {
        OwnerCaptureRenderTarget->ReleaseResource();
        OwnerCaptureRenderTarget = nullptr;
    }

    if (OwnerDepthRenderTarget)
    {
        OwnerDepthRenderTarget->ReleaseResource();
        OwnerDepthRenderTarget = nullptr;
    }
}

void UMyActorComponent::ConfigureOwnerCaptureVisibility(AActor* Owner)
{
    if (!Owner)
    {
        return;
    }

    if (bHideOwnerFromOwnerCameraCapture)
    {
        if (OwnerSceneCapture)
        {
            OwnerSceneCapture->HiddenActors.AddUnique(Owner);
        }
        if (OwnerDepthSceneCapture)
        {
            OwnerDepthSceneCapture->HiddenActors.AddUnique(Owner);
        }
    }
    else
    {
        if (OwnerSceneCapture)
        {
            OwnerSceneCapture->HiddenActors.Remove(Owner);
        }
        if (OwnerDepthSceneCapture)
        {
            OwnerDepthSceneCapture->HiddenActors.Remove(Owner);
        }
    }
}

void UMyActorComponent::ApplyProjectDetectionSettings()
{
    UEagleEyeDetectionSettings::LoadRuntimeConfig();
    const UEagleEyeDetectionSettings* Settings = GetDefault<UEagleEyeDetectionSettings>();
    if (!Settings)
    {
        return;
    }

    InferenceBackend = Settings->InferenceBackend;
    OnnxRuntimeExecutionProvider = Settings->OnnxRuntimeExecutionProvider;
    ModelPathOverride = Settings->ModelPathOverride;
    NamesPathOverride = Settings->NamesPathOverride;
    bOpenCVDNNPreferCUDA = Settings->bOpenCVDNNPreferCUDA;
    bOpenCVDNNUseFP16 = Settings->bOpenCVDNNUseFP16;
    OnnxInputSize = FMath::Clamp(Settings->OnnxInputSize, 160, 1280);
    bUseLetterbox = Settings->bUseLetterbox;
    LetterboxValue = FMath::Clamp(Settings->LetterboxValue, 0, 255);
    ConfidenceThreshold = FMath::Clamp(Settings->ConfidenceThreshold, 0.01f, 0.99f);
    NmsThreshold = FMath::Clamp(Settings->NmsThreshold, 0.01f, 0.99f);
    bRecordFrameTimes = Settings->bRecordFrameTimes;
    FrameTimeCsvPath = Settings->FrameTimeCsvPath;
    bResetFrameTimeLogOnBeginPlay = Settings->bResetFrameTimeLogOnBeginPlay;
    FrameTimeFlushInterval = FMath::Clamp(Settings->FrameTimeFlushInterval, 1, 600);
}

void UMyActorComponent::ApplySharedVisionFrameSourceSettings()
{
    if (!bUseSharedVisionModel || bSharedVisionModelHost)
    {
        return;
    }

    UWorld* World = GetWorld();
    const UCrowVisionSubsystem* VisionSubsystem = World ? World->GetSubsystem<UCrowVisionSubsystem>() : nullptr;
    if (!VisionSubsystem)
    {
        return;
    }

    const float PreviousCaptureFPS = CaptureFPS;
    CaptureFPS = VisionSubsystem->GetFrameSourceCaptureFPS();
    CaptureWidth = VisionSubsystem->GetFrameSourceCaptureWidth();
    CaptureHeight = VisionSubsystem->GetFrameSourceCaptureHeight();
    MaxOwnerCameraCaptureDistance = VisionSubsystem->GetFrameSourceMaxDistanceToPlayer();
    bStaggerInitialCapture = VisionSubsystem->ShouldStaggerInitialCapture();
    MaxInitialCaptureDelay = VisionSubsystem->GetMaxInitialCaptureDelay();

    if (!FMath::IsNearlyEqual(PreviousCaptureFPS, CaptureFPS) && World->GetTimerManager().IsTimerActive(TimerHandle_Capture))
    {
        World->GetTimerManager().SetTimer(
            TimerHandle_Capture,
            this,
            &UMyActorComponent::TickCapture,
            1.0f / FMath::Clamp(CaptureFPS, 1.0f, 120.0f),
            true);
    }
}

void UMyActorComponent::ResolveConfiguredRuntimePaths()
{
    const FString ResolvedNamesPath = ResolveRuntimeFilePath(
        NamesPathOverride.IsEmpty() ? TEXT("coco.names") : NamesPathOverride);
    const FString ResolvedModelSelection = EagleEyeModelDiscovery::NormalizeModelSelection(
        ModelPathOverride.IsEmpty() ? TEXT("yolo26x") : ModelPathOverride);

    NamesPath = ToUtf8Path(ResolvedNamesPath);
    WeightsPath = ToUtf8Path(ResolvedModelSelection);

    UE_LOG(LogTemp, Log, TEXT("Detection model selection: %s"), *ResolvedModelSelection);
    UE_LOG(LogTemp, Log, TEXT("Detection names path: %s"), *ResolvedNamesPath);
}

void UMyActorComponent::ApplyRuntimeDetectionSettingsFromConfig(bool bReloadModel)
{
    const bool bRestartLocalWorker = bWorkerRunning.load();
    if (bRestartLocalWorker)
    {
        StopWorker();
    }

    ApplyProjectDetectionSettings();
    ApplySharedVisionFrameSourceSettings();
    ResolveConfiguredRuntimePaths();

    {
        FScopeLock Lock(&InferenceMutex);
        get_class_names();

        if (bReloadModel)
        {
            ReleaseTensorRT();
            ReleaseOnnxRuntime();
            OpenCVDnnNet = cv::dnn::Net();
            bIsModelLoaded = false;
            ResetInferenceOutputState();
        }
    }

    if (bRestartLocalWorker)
    {
        StartWorker();
    }
}

bool UMyActorComponent::EnsureModelLoaded()
{
    if (bUseFovOnlyPersonDetection || (bUseSharedVisionModel && !bSharedVisionModelHost))
    {
        return false;
    }

    ApplyProjectDetectionSettings();
    ResolveConfiguredRuntimePaths();

    FScopeLock InferenceLock(&InferenceMutex);
    if (!bIsModelLoaded)
    {
        get_class_names();
        if (!LoadYOLO())
        {
            return false;
        }
    }

    InitFrameTimingLog();
    return true;
}

FString UMyActorComponent::ResolveOwnerCameraVideoPath() const
{
    if (!OwnerCameraVideoOutputPath.IsEmpty())
    {
        FString NormalizedPath = NormalizeRuntimeFilePath(OwnerCameraVideoOutputPath);
        if (FPaths::GetExtension(NormalizedPath).IsEmpty())
        {
            NormalizedPath += TEXT(".ts");
        }
        return NormalizedPath;
    }

    FString OwnerName = GetNameSafe(GetOwner());
    const TCHAR* InvalidChars[] = { TEXT(" "), TEXT(":"), TEXT("/"), TEXT("\\"), TEXT("."), TEXT("'"), TEXT("\"") };
    for (const TCHAR* InvalidChar : InvalidChars)
    {
        OwnerName.ReplaceInline(InvalidChar, TEXT("_"));
    }

    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    return FPaths::Combine(
        FPaths::VideoCaptureDir(),
        FString::Printf(TEXT("BotViewport_%s_%s.ts"), *OwnerName, *Timestamp));
}

bool UMyActorComponent::EnsureOwnerCameraVideoWriter(int32 Width, int32 Height)
{
    if (Width <= 0 || Height <= 0 || bOwnerCameraVideoWriterFailed)
    {
        return false;
    }

    if (OwnerCameraVideoProcess.IsValid() && OwnerCameraVideoPipeWrite &&
        OwnerCameraVideoWidth == Width && OwnerCameraVideoHeight == Height)
    {
        return true;
    }

    FinalizeOwnerCameraVideoWriter(!bOwnerCameraVideoFastShutdown.load());

    ActiveOwnerCameraVideoPath = ResolveOwnerCameraVideoPath();
    if (ActiveOwnerCameraVideoPath.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to resolve bot viewport video path."));
        bOwnerCameraVideoWriterFailed = true;
        return false;
    }

    const FString OutputDirectory = FPaths::GetPath(ActiveOwnerCameraVideoPath);
    if (!OutputDirectory.IsEmpty() && !IFileManager::Get().MakeDirectory(*OutputDirectory, true))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create bot viewport video directory: %s"), *OutputDirectory);
        ActiveOwnerCameraVideoPath.Reset();
        bOwnerCameraVideoWriterFailed = true;
        return false;
    }

    void* ChildStdInReadPipe = nullptr;
    void* LocalStdInWritePipe = nullptr;
    if (!FPlatformProcess::CreatePipe(ChildStdInReadPipe, LocalStdInWritePipe, true))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create FFmpeg stdin pipe for bot viewport recording."));
        ActiveOwnerCameraVideoPath.Reset();
        bOwnerCameraVideoWriterFailed = true;
        return false;
    }

    OwnerCameraVideoWidth = Width;
    OwnerCameraVideoHeight = Height;
    OwnerCameraVideoFPS = FMath::Clamp(FMath::RoundToInt(CaptureFPS), 1, 120);
    OwnerCameraVideoFrameCount = 0;

    const FString FfmpegArguments = FString::Printf(
        TEXT("-y -nostats -loglevel error -f rawvideo -pix_fmt bgra -s %dx%d -r %d -i - -an -c:v libx264 -preset ultrafast -tune zerolatency -g %d -keyint_min %d -sc_threshold 0 -pix_fmt yuv420p -f mpegts \"%s\""),
        Width,
        Height,
        OwnerCameraVideoFPS,
        OwnerCameraVideoFPS,
        OwnerCameraVideoFPS,
        *ActiveOwnerCameraVideoPath);

    uint32 ProcessId = 0;
    OwnerCameraVideoProcess = FPlatformProcess::CreateProc(
        *OwnerCameraVideoEncoderPath,
        *FfmpegArguments,
        false,
        true,
        true,
        &ProcessId,
        0,
        nullptr,
        nullptr,
        ChildStdInReadPipe,
        nullptr);

    FPlatformProcess::ClosePipe(ChildStdInReadPipe, nullptr);
    ChildStdInReadPipe = nullptr;

    if (!OwnerCameraVideoProcess.IsValid())
    {
        FPlatformProcess::ClosePipe(nullptr, LocalStdInWritePipe);
        UE_LOG(LogTemp, Error,
            TEXT("Failed to start FFmpeg for bot viewport recording. Install ffmpeg or set OwnerCameraVideoEncoderPath. Command: %s %s"),
            *OwnerCameraVideoEncoderPath,
            *FfmpegArguments);
        ActiveOwnerCameraVideoPath.Reset();
        bOwnerCameraVideoWriterFailed = true;
        return false;
    }

    OwnerCameraVideoPipeWrite = LocalStdInWritePipe;

    UE_LOG(LogTemp, Log, TEXT("Recording bot viewport video through FFmpeg: %s (%dx%d @ %d fps)"),
        *ActiveOwnerCameraVideoPath,
        Width,
        Height,
        OwnerCameraVideoFPS);
    return true;
}

void UMyActorComponent::RecordOwnerCameraVideoFrame(const TArray<FColor>& Pixels, int32 Width, int32 Height)
{
    if (!bRecordOwnerCameraCaptureVideo || !bUseOwnerCameraCapture || Pixels.Num() != Width * Height)
    {
        return;
    }

    if (PendingOwnerCameraVideoFrames.load() >= FMath::Max(1, MaxQueuedOwnerCameraVideoFrames))
    {
        ++DroppedOwnerCameraVideoFrames;
        return;
    }

    StartOwnerCameraVideoWorker();

    TSharedPtr<FOwnerCameraVideoFrame> Frame = MakeShared<FOwnerCameraVideoFrame>();
    Frame->Pixels = Pixels;
    Frame->Detections = LastFrameDetections;
    Frame->Width = Width;
    Frame->Height = Height;
    Frame->DetectionSourceWidth = LastFrameSourceWidth;
    Frame->DetectionSourceHeight = LastFrameSourceHeight;
    PendingOwnerCameraVideoFrames.fetch_add(1);
    OwnerCameraVideoQueue.Enqueue(Frame);
}

void UMyActorComponent::StartOwnerCameraVideoWorker()
{
    if (bOwnerCameraVideoWorkerRunning.load())
    {
        return;
    }

    if (OwnerCameraVideoThread)
    {
        if (OwnerCameraVideoThread->joinable())
        {
            OwnerCameraVideoThread->join();
        }
        delete OwnerCameraVideoThread;
        OwnerCameraVideoThread = nullptr;
    }

    bOwnerCameraVideoWorkerRunning.store(true);
    OwnerCameraVideoThread = new std::thread([this]()
    {
        OwnerCameraVideoWorkerLoop();
    });
}

void UMyActorComponent::OwnerCameraVideoWorkerLoop()
{
    while (bOwnerCameraVideoWorkerRunning.load() || PendingOwnerCameraVideoFrames.load() > 0)
    {
        TSharedPtr<FOwnerCameraVideoFrame> Frame;
        if (!OwnerCameraVideoQueue.Dequeue(Frame))
        {
            FPlatformProcess::Sleep(0.002f);
            continue;
        }

        PendingOwnerCameraVideoFrames.fetch_sub(1);
        if (Frame.IsValid())
        {
            if (!bOwnerCameraVideoWorkerRunning.load())
            {
                ++DroppedOwnerCameraVideoFrames;
                continue;
            }

            WriteOwnerCameraVideoFrameSync(
                Frame->Pixels,
                Frame->Width,
                Frame->Height,
                Frame->Detections,
                Frame->DetectionSourceWidth,
                Frame->DetectionSourceHeight);
        }
    }

    FinalizeOwnerCameraVideoWriter(!bOwnerCameraVideoFastShutdown.load());
}

void UMyActorComponent::WriteOwnerCameraVideoFrameSync(
    const TArray<FColor>& Pixels,
    int32 Width,
    int32 Height,
    const TArray<FDetectionResult>& Detections,
    int32 DetectionSourceWidth,
    int32 DetectionSourceHeight)
{
    if (Pixels.Num() != Width * Height)
    {
        return;
    }

    if (!EnsureOwnerCameraVideoWriter(Width, Height))
    {
        return;
    }

    TArray<uint8> RawFrame;
    RawFrame.SetNumUninitialized(Width * Height * 4);

    uint8* Dest = RawFrame.GetData();
    for (int32 Y = 0; Y < Height; ++Y)
    {
        const FColor* Source = Pixels.GetData() + (Y * Width);
        for (int32 X = 0; X < Width; ++X)
        {
            FColor VideoColor = Source[X];
            if (bApplyOwnerCameraVideoGammaCorrection)
            {
                const FLinearColor LinearColor(
                    static_cast<float>(Source[X].R) / 255.0f,
                    static_cast<float>(Source[X].G) / 255.0f,
                    static_cast<float>(Source[X].B) / 255.0f,
                    static_cast<float>(Source[X].A) / 255.0f);
                VideoColor = LinearColor.ToFColorSRGB();
            }

            *Dest++ = VideoColor.B;
            *Dest++ = VideoColor.G;
            *Dest++ = VideoColor.R;
            *Dest++ = VideoColor.A;
        }
    }

    DrawDetectionsOnVideoFrameBGRA(
        RawFrame,
        Width,
        Height,
        Detections,
        DetectionSourceWidth,
        DetectionSourceHeight);

    int32 BytesWrittenTotal = 0;
    while (BytesWrittenTotal < RawFrame.Num())
    {
        int32 BytesWritten = 0;
        const int32 BytesRemaining = RawFrame.Num() - BytesWrittenTotal;
        if (!FPlatformProcess::WritePipe(
            OwnerCameraVideoPipeWrite,
            RawFrame.GetData() + BytesWrittenTotal,
            BytesRemaining,
            &BytesWritten) || BytesWritten <= 0)
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to write bot viewport frame to FFmpeg stdin."));
            bOwnerCameraVideoWriterFailed = true;
            return;
        }

        BytesWrittenTotal += BytesWritten;
    }

    ++OwnerCameraVideoFrameCount;
}

void UMyActorComponent::CloseOwnerCameraVideoWriter(bool bWaitForEncoder)
{
    bOwnerCameraVideoFastShutdown.store(!bWaitForEncoder);
    bOwnerCameraVideoWorkerRunning.store(false);

    int32 DroppedOnClose = 0;
    TSharedPtr<FOwnerCameraVideoFrame> DroppedFrame;
    while (OwnerCameraVideoQueue.Dequeue(DroppedFrame))
    {
        ++DroppedOnClose;
    }
    if (DroppedOnClose > 0)
    {
        DroppedOwnerCameraVideoFrames += DroppedOnClose;
    }
    PendingOwnerCameraVideoFrames.store(0);

    if (OwnerCameraVideoThread)
    {
        if (OwnerCameraVideoThread->joinable())
        {
            if (OwnerCameraVideoThread->get_id() == std::this_thread::get_id())
            {
                OwnerCameraVideoThread->detach();
            }
            else
            {
                OwnerCameraVideoThread->join();
            }
        }
        delete OwnerCameraVideoThread;
        OwnerCameraVideoThread = nullptr;
    }
    else
    {
        FinalizeOwnerCameraVideoWriter(bWaitForEncoder);
    }

    if (DroppedOwnerCameraVideoFrames > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Dropped %d bot viewport recording frames because the queue was full or recording stopped."),
            DroppedOwnerCameraVideoFrames);
        DroppedOwnerCameraVideoFrames = 0;
    }
}

void UMyActorComponent::FinalizeOwnerCameraVideoWriter(bool bWaitForEncoder)
{
    if (bOwnerCameraVideoFinalizing.exchange(true))
    {
        return;
    }

    if (OwnerCameraVideoPipeWrite)
    {
        void* PipeToClose = OwnerCameraVideoPipeWrite;
        OwnerCameraVideoPipeWrite = nullptr;
        FPlatformProcess::ClosePipe(nullptr, PipeToClose);
    }

    if (OwnerCameraVideoProcess.IsValid())
    {
        const double ShutdownTimeoutSeconds = bWaitForEncoder
            ? FfmpegStopTimeoutSeconds
            : FfmpegFastShutdownTimeoutSeconds;
        const double WaitStart = FPlatformTime::Seconds();
        while (FPlatformProcess::IsProcRunning(OwnerCameraVideoProcess) &&
            (FPlatformTime::Seconds() - WaitStart) < ShutdownTimeoutSeconds)
        {
            FPlatformProcess::Sleep(0.02f);
        }

        if (FPlatformProcess::IsProcRunning(OwnerCameraVideoProcess))
        {
            UE_LOG(LogTemp, Warning, TEXT("FFmpeg did not finish within %.1f seconds; terminating bot viewport recording process."),
                ShutdownTimeoutSeconds);
            FPlatformProcess::TerminateProc(OwnerCameraVideoProcess, false);

            const double TerminateWaitStart = FPlatformTime::Seconds();
            while (FPlatformProcess::IsProcRunning(OwnerCameraVideoProcess) &&
                (FPlatformTime::Seconds() - TerminateWaitStart) < FfmpegTerminateTimeoutSeconds)
            {
                FPlatformProcess::Sleep(0.02f);
            }
        }

        if (FPlatformProcess::IsProcRunning(OwnerCameraVideoProcess))
        {
            UE_LOG(LogTemp, Warning, TEXT("FFmpeg process is still running after terminate request; releasing handle without blocking PIE shutdown."));
            OwnerCameraVideoProcess.Reset();
        }
        else
        {
            FPlatformProcess::CloseProc(OwnerCameraVideoProcess);
            OwnerCameraVideoProcess.Reset();
        }
    }

    if (!ActiveOwnerCameraVideoPath.IsEmpty() && OwnerCameraVideoFrameCount > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("Finished bot viewport video: %s (%d frames)"),
            *ActiveOwnerCameraVideoPath,
            OwnerCameraVideoFrameCount);
    }

    ActiveOwnerCameraVideoPath.Reset();
    OwnerCameraVideoWidth = 0;
    OwnerCameraVideoHeight = 0;
    OwnerCameraVideoFPS = 0;
    OwnerCameraVideoFrameCount = 0;
    bOwnerCameraVideoFastShutdown.store(false);
    bOwnerCameraVideoWriterFailed = false;
    bOwnerCameraVideoFinalizing.store(false);
}

bool UMyActorComponent::ShouldSkipOwnerCameraCapture() const
{
    const AActor* Owner = GetOwner();
    if (!Owner)
    {
        return false;
    }

    if (const UWorld* World = GetWorld())
    {
        if (const UCrowDetectionShareSubsystem* ShareSubsystem = World->GetSubsystem<UCrowDetectionShareSubsystem>())
        {
            return !ShareSubsystem->ShouldRunDetector(const_cast<AActor*>(Owner));
        }
    }

    if (MaxOwnerCameraCaptureDistance <= 0.f)
    {
        return false;
    }

    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }
    const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
    const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
    if (!PlayerPawn)
    {
        return false;
    }

    return FVector::DistSquared(Owner->GetActorLocation(), PlayerPawn->GetActorLocation())
        > FMath::Square(MaxOwnerCameraCaptureDistance);
}

bool UMyActorComponent::ShouldRecordOwnerCameraVideo() const
{
    return bRecordOwnerCameraCaptureVideo && bUseOwnerCameraCapture &&
        (!ShouldSkipOwnerCameraCapture() || bRecordOwnerCameraWhenDetectionSkipped);
}

bool UMyActorComponent::HasPersonDetection(const TArray<FDetectionResult>& Detections) const
{
    for (const FDetectionResult& Detection : Detections)
    {
        if (Detection.ClassId == 0 || Detection.Label.StartsWith(TEXT("person"), ESearchCase::IgnoreCase))
        {
            return true;
        }
    }

    return false;
}

bool UMyActorComponent::EvaluatePlayerInOwnerCameraFov(
    int32 SourceWidth,
    int32 SourceHeight,
    FVector2D& OutPixel,
    float& OutDistance,
    float& OutHorizontalAngleDegrees,
    float& OutVerticalAngleDegrees,
    bool& bOutHasEvaluation) const
{
    bOutHasEvaluation = false;
    OutPixel = FVector2D::ZeroVector;
    OutDistance = 0.f;
    OutHorizontalAngleDegrees = 0.f;
    OutVerticalAngleDegrees = 0.f;

    const AActor* Owner = GetOwner();
    const UCameraComponent* OwnerCamera = Owner ? Owner->FindComponentByClass<UCameraComponent>() : nullptr;
    const UWorld* World = GetWorld();
    const APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
    const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
    if (!Owner || !OwnerCamera || !PlayerPawn || PlayerPawn == Owner)
    {
        return false;
    }

    const FVector CameraLocation = OwnerCamera->GetComponentLocation();
    const FVector TargetLocation = PlayerPawn->GetPawnViewLocation();
    const FVector ToTarget = TargetLocation - CameraLocation;
    OutDistance = ToTarget.Size();
    if (OutDistance <= KINDA_SMALL_NUMBER)
    {
        bOutHasEvaluation = true;
        OutPixel = FVector2D(SourceWidth * 0.5f, SourceHeight * 0.5f);
        return true;
    }

    const FVector Direction = ToTarget / OutDistance;
    const FTransform CameraTransform = OwnerCamera->GetComponentTransform();
    const float ForwardAmount = FVector::DotProduct(Direction, CameraTransform.GetUnitAxis(EAxis::X));
    const float RightAmount = FVector::DotProduct(Direction, CameraTransform.GetUnitAxis(EAxis::Y));
    const float UpAmount = FVector::DotProduct(Direction, CameraTransform.GetUnitAxis(EAxis::Z));

    bOutHasEvaluation = true;
    if (ForwardAmount <= KINDA_SMALL_NUMBER)
    {
        OutHorizontalAngleDegrees = RightAmount >= 0.f ? 180.f : -180.f;
        OutVerticalAngleDegrees = UpAmount >= 0.f ? 90.f : -90.f;
        return false;
    }

    const float SafeWidth = FMath::Max(static_cast<float>(SourceWidth), 1.f);
    const float SafeHeight = FMath::Max(static_cast<float>(SourceHeight), 1.f);
    const float Aspect = SafeWidth / SafeHeight;
    const float HalfHorizontalFovRad = FMath::DegreesToRadians(OwnerCamera->FieldOfView) * 0.5f;
    const float HalfVerticalFovRad = FMath::Atan(FMath::Tan(HalfHorizontalFovRad) / FMath::Max(Aspect, KINDA_SMALL_NUMBER));
    const float HorizontalAngleRad = FMath::Atan2(RightAmount, ForwardAmount);
    const float VerticalAngleRad = FMath::Atan2(UpAmount, ForwardAmount);

    OutHorizontalAngleDegrees = FMath::RadiansToDegrees(HorizontalAngleRad);
    OutVerticalAngleDegrees = FMath::RadiansToDegrees(VerticalAngleRad);

    const bool bWithinDistance = FovOnlyDetectionMaxDistance <= 0.f || OutDistance <= FovOnlyDetectionMaxDistance;
    const bool bWithinFov =
        FMath::Abs(HorizontalAngleRad) <= HalfHorizontalFovRad &&
        FMath::Abs(VerticalAngleRad) <= HalfVerticalFovRad;

    const float NdcX = FMath::Tan(HorizontalAngleRad) / FMath::Max(FMath::Tan(HalfHorizontalFovRad), KINDA_SMALL_NUMBER);
    const float NdcY = FMath::Tan(VerticalAngleRad) / FMath::Max(FMath::Tan(HalfVerticalFovRad), KINDA_SMALL_NUMBER);
    OutPixel.X = (NdcX + 1.f) * 0.5f * SafeWidth;
    OutPixel.Y = (1.f - NdcY) * 0.5f * SafeHeight;

    return bWithinDistance && bWithinFov;
}

const AActor* UMyActorComponent::ResolveDetectionTimingTargetActor() const
{
    const APawn* OwnerPawn = Cast<APawn>(GetOwner());
    const AAIController* AIController = OwnerPawn ? Cast<AAIController>(OwnerPawn->GetController()) : nullptr;
    const UBlackboardComponent* Blackboard = AIController ? AIController->GetBlackboardComponent() : nullptr;
    if (Blackboard)
    {
        if (const UObject* TargetObject = Blackboard->GetValueAsObject(TEXT("TargetActor")))
        {
            if (const AActor* TargetActor = Cast<AActor>(TargetObject))
            {
                return TargetActor;
            }
        }
    }

    const UWorld* World = GetWorld();
    const APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
    return PlayerController ? PlayerController->GetPawn() : nullptr;
}

bool UMyActorComponent::EvaluateTargetActorInOwnerCameraFov(
    const AActor* TargetActor,
    int32 SourceWidth,
    int32 SourceHeight,
    FVector2D& OutPixel,
    float& OutDistance,
    float& OutHorizontalAngleDegrees,
    float& OutVerticalAngleDegrees,
    bool& bOutHasEvaluation) const
{
    bOutHasEvaluation = false;
    OutPixel = FVector2D::ZeroVector;
    OutDistance = 0.f;
    OutHorizontalAngleDegrees = 0.f;
    OutVerticalAngleDegrees = 0.f;

    const AActor* Owner = GetOwner();
    const UCameraComponent* OwnerCamera = Owner ? Owner->FindComponentByClass<UCameraComponent>() : nullptr;
    if (!Owner || !OwnerCamera || !IsValid(TargetActor) || TargetActor == Owner)
    {
        return false;
    }

    const APawn* TargetPawn = Cast<APawn>(TargetActor);
    const FVector TargetLocation = TargetPawn ? TargetPawn->GetPawnViewLocation() : TargetActor->GetActorLocation();
    const FVector ToTarget = TargetLocation - OwnerCamera->GetComponentLocation();
    OutDistance = ToTarget.Size();
    if (OutDistance <= KINDA_SMALL_NUMBER)
    {
        bOutHasEvaluation = true;
        OutPixel = FVector2D(SourceWidth * 0.5f, SourceHeight * 0.5f);
        return true;
    }

    const FVector Direction = ToTarget / OutDistance;
    const FTransform CameraTransform = OwnerCamera->GetComponentTransform();
    const float ForwardAmount = FVector::DotProduct(Direction, CameraTransform.GetUnitAxis(EAxis::X));
    const float RightAmount = FVector::DotProduct(Direction, CameraTransform.GetUnitAxis(EAxis::Y));
    const float UpAmount = FVector::DotProduct(Direction, CameraTransform.GetUnitAxis(EAxis::Z));

    bOutHasEvaluation = true;
    if (ForwardAmount <= KINDA_SMALL_NUMBER)
    {
        OutHorizontalAngleDegrees = RightAmount >= 0.f ? 180.f : -180.f;
        OutVerticalAngleDegrees = UpAmount >= 0.f ? 90.f : -90.f;
        return false;
    }

    const float SafeWidth = FMath::Max(static_cast<float>(SourceWidth), 1.f);
    const float SafeHeight = FMath::Max(static_cast<float>(SourceHeight), 1.f);
    const float Aspect = SafeWidth / SafeHeight;
    const float HalfHorizontalFovRad = FMath::DegreesToRadians(OwnerCamera->FieldOfView) * 0.5f;
    const float HalfVerticalFovRad = FMath::Atan(FMath::Tan(HalfHorizontalFovRad) / FMath::Max(Aspect, KINDA_SMALL_NUMBER));
    const float HorizontalAngleRad = FMath::Atan2(RightAmount, ForwardAmount);
    const float VerticalAngleRad = FMath::Atan2(UpAmount, ForwardAmount);

    OutHorizontalAngleDegrees = FMath::RadiansToDegrees(HorizontalAngleRad);
    OutVerticalAngleDegrees = FMath::RadiansToDegrees(VerticalAngleRad);

    const bool bWithinDistance = FovOnlyDetectionMaxDistance <= 0.f || OutDistance <= FovOnlyDetectionMaxDistance;
    const bool bWithinFov =
        FMath::Abs(HorizontalAngleRad) <= HalfHorizontalFovRad &&
        FMath::Abs(VerticalAngleRad) <= HalfVerticalFovRad;

    const float NdcX = FMath::Tan(HorizontalAngleRad) / FMath::Max(FMath::Tan(HalfHorizontalFovRad), KINDA_SMALL_NUMBER);
    const float NdcY = FMath::Tan(VerticalAngleRad) / FMath::Max(FMath::Tan(HalfVerticalFovRad), KINDA_SMALL_NUMBER);
    OutPixel.X = (NdcX + 1.f) * 0.5f * SafeWidth;
    OutPixel.Y = (1.f - NdcY) * 0.5f * SafeHeight;

    return bWithinDistance && bWithinFov;
}

bool UMyActorComponent::EvaluateDetectionTimingTarget(
    int32 SourceWidth,
    int32 SourceHeight,
    FVector2D& OutPixel,
    bool& bOutHasEvaluation) const
{
    float Distance = 0.f;
    float HorizontalAngleDegrees = 0.f;
    float VerticalAngleDegrees = 0.f;
    return EvaluateTargetActorInOwnerCameraFov(
        ResolveDetectionTimingTargetActor(),
        SourceWidth,
        SourceHeight,
        OutPixel,
        Distance,
        HorizontalAngleDegrees,
        VerticalAngleDegrees,
        bOutHasEvaluation);
}

bool UMyActorComponent::HasDetectionContainingPixel(const TArray<FDetectionResult>& Detections, const FVector2D& Pixel) const
{
    for (const FDetectionResult& Detection : Detections)
    {
        if (Detection.Corners.Num() == 0)
        {
            continue;
        }

        float MinX = Detection.Corners[0].X;
        float MinY = Detection.Corners[0].Y;
        float MaxX = Detection.Corners[0].X;
        float MaxY = Detection.Corners[0].Y;
        for (const FVector2D& Corner : Detection.Corners)
        {
            MinX = FMath::Min(MinX, Corner.X);
            MinY = FMath::Min(MinY, Corner.Y);
            MaxX = FMath::Max(MaxX, Corner.X);
            MaxY = FMath::Max(MaxY, Corner.Y);
        }

        if (Pixel.X >= MinX && Pixel.X <= MaxX && Pixel.Y >= MinY && Pixel.Y <= MaxY)
        {
            return true;
        }
    }

    return false;
}

bool UMyActorComponent::BuildFovOnlyPersonDetection(
    TArray<FDetectionResult>& OutDetections,
    int32& OutSourceWidth,
    int32& OutSourceHeight,
    float& OutDistance,
    float& OutHorizontalAngleDegrees,
    float& OutVerticalAngleDegrees)
{
    OutDetections.Reset();
    OutSourceWidth = FMath::Max(CaptureWidth, 1);
    OutSourceHeight = FMath::Max(CaptureHeight, 1);

    FVector2D TargetPixel = FVector2D::ZeroVector;
    bool bHasEvaluation = false;
    const bool bPlayerInFov = EvaluatePlayerInOwnerCameraFov(
        OutSourceWidth,
        OutSourceHeight,
        TargetPixel,
        OutDistance,
        OutHorizontalAngleDegrees,
        OutVerticalAngleDegrees,
        bHasEvaluation);
    if (!bHasEvaluation || !bPlayerInFov)
    {
        return false;
    }

    const float HalfBoxSize = FMath::Clamp(
        static_cast<float>(FovOnlySyntheticBoxSizePixels),
        1.f,
        static_cast<float>(FMath::Max(FMath::Min(OutSourceWidth, OutSourceHeight), 1))) * 0.5f;

    const float MinX = FMath::Clamp(TargetPixel.X - HalfBoxSize, 0.f, static_cast<float>(OutSourceWidth - 1));
    const float MinY = FMath::Clamp(TargetPixel.Y - HalfBoxSize, 0.f, static_cast<float>(OutSourceHeight - 1));
    const float MaxX = FMath::Clamp(TargetPixel.X + HalfBoxSize, 0.f, static_cast<float>(OutSourceWidth - 1));
    const float MaxY = FMath::Clamp(TargetPixel.Y + HalfBoxSize, 0.f, static_cast<float>(OutSourceHeight - 1));

    FDetectionResult Detection;
    Detection.ClassId = 0;
    Detection.Confidence = 1.f;
    Detection.Label = TEXT("person: 1.00 (fov)");
    Detection.Corners.Add(FVector2D(MinX, MinY));
    Detection.Corners.Add(FVector2D(MaxX, MinY));
    Detection.Corners.Add(FVector2D(MaxX, MaxY));
    Detection.Corners.Add(FVector2D(MinX, MaxY));
    OutDetections.Add(MoveTemp(Detection));
    return true;
}

void UMyActorComponent::LogFovDetectionMetricSample(
    const TCHAR* SourceLabel,
    const TArray<FDetectionResult>& Detections,
    int32 SourceWidth,
    int32 SourceHeight)
{
    if (!bLogFovDetectionMetrics)
    {
        return;
    }

    FVector2D ExpectedPixel = FVector2D::ZeroVector;
    float Distance = 0.f;
    float HorizontalAngleDegrees = 0.f;
    float VerticalAngleDegrees = 0.f;
    bool bHasEvaluation = false;
    const bool bExpectedInFov = EvaluatePlayerInOwnerCameraFov(
        SourceWidth,
        SourceHeight,
        ExpectedPixel,
        Distance,
        HorizontalAngleDegrees,
        VerticalAngleDegrees,
        bHasEvaluation);
    if (!bHasEvaluation)
    {
        ++FovDetectionUnavailableSampleCount;
        AppendFovDetectionMetricsTableRow(
            SourceLabel,
            TEXT("unavailable"),
            TEXT("unavailable"),
            false,
            HasPersonDetection(Detections),
            Distance,
            HorizontalAngleDegrees,
            VerticalAngleDegrees,
            ExpectedPixel,
            SourceWidth,
            SourceHeight,
            Detections.Num());
        if (IsActorComponentDetectionMetricLoggingEnabled())
        {
            UE_LOG(LogTemp, Warning, TEXT("FovDetectionMetric[%s]: unavailable owner=%s source=%dx%d detections=%d unavailable=%d"),
                SourceLabel,
                *GetNameSafe(GetOwner()),
                SourceWidth,
                SourceHeight,
                Detections.Num(),
                FovDetectionUnavailableSampleCount);
        }
        return;
    }

    const bool bActualPersonDetected = HasPersonDetection(Detections);
    const TCHAR* Outcome = TEXT("true_negative");
    const TCHAR* Status = TEXT("success");
    if (bExpectedInFov && bActualPersonDetected)
    {
        ++FovDetectionTruePositiveCount;
        Outcome = TEXT("true_positive");
    }
    else if (!bExpectedInFov && !bActualPersonDetected)
    {
        ++FovDetectionTrueNegativeCount;
        Outcome = TEXT("true_negative");
    }
    else if (!bExpectedInFov && bActualPersonDetected)
    {
        ++FovDetectionFalsePositiveCount;
        Outcome = TEXT("false_positive");
        Status = TEXT("fault");
    }
    else
    {
        ++FovDetectionFalseNegativeCount;
        Outcome = TEXT("false_negative");
        Status = TEXT("fault");
    }

    AppendFovDetectionMetricsTableRow(
        SourceLabel,
        Status,
        Outcome,
        bExpectedInFov,
        bActualPersonDetected,
        Distance,
        HorizontalAngleDegrees,
        VerticalAngleDegrees,
        ExpectedPixel,
        SourceWidth,
        SourceHeight,
        Detections.Num());

    if (IsActorComponentDetectionMetricLoggingEnabled())
    {
        UE_LOG(LogTemp, Log, TEXT("FovDetectionMetric[%s]: %s outcome=%s owner=%s expected_in_fov=%s actual_person=%s distance=%.1f h_angle=%.1f v_angle=%.1f expected_pixel=%s source=%dx%d detections=%d tp=%d tn=%d fp=%d fn=%d"),
            SourceLabel,
            Status,
            Outcome,
            *GetNameSafe(GetOwner()),
            bExpectedInFov ? TEXT("true") : TEXT("false"),
            bActualPersonDetected ? TEXT("true") : TEXT("false"),
            Distance,
            HorizontalAngleDegrees,
            VerticalAngleDegrees,
            *ExpectedPixel.ToString(),
            SourceWidth,
            SourceHeight,
            Detections.Num(),
            FovDetectionTruePositiveCount,
            FovDetectionTrueNegativeCount,
            FovDetectionFalsePositiveCount,
            FovDetectionFalseNegativeCount);
    }
}

void UMyActorComponent::PublishFovOnlyDetectionFrame()
{
    TArray<FDetectionResult> Detections;
    int32 SourceWidth = 0;
    int32 SourceHeight = 0;
    float Distance = 0.f;
    float HorizontalAngleDegrees = 0.f;
    float VerticalAngleDegrees = 0.f;
    BuildFovOnlyPersonDetection(
        Detections,
        SourceWidth,
        SourceHeight,
        Distance,
        HorizontalAngleDegrees,
        VerticalAngleDegrees);

    LastFrameDetections = MoveTemp(Detections);
    LastFrameSourceWidth = SourceWidth;
    LastFrameSourceHeight = SourceHeight;
    ++LastFrameSequence;
    LastFrameTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

    LogFovDetectionMetricSample(TEXT("fov_only"), LastFrameDetections, SourceWidth, SourceHeight);

    if (ShouldLogFrameTimings())
    {
        UE_LOG(LogTemp, Log, TEXT("FovOnlyDetection[%s]: detections=%d size=%dx%d seq=%d distance=%.1f h_angle=%.1f v_angle=%.1f"),
            *GetNameSafe(GetOwner()),
            LastFrameDetections.Num(),
            SourceWidth,
            SourceHeight,
            LastFrameSequence,
            Distance,
            HorizontalAngleDegrees,
            VerticalAngleDegrees);
    }
}

void UMyActorComponent::ClearPublishedResults()
{
    {
        FScopeLock ResultsLock(&ResultsMutex);
        ResultsShared.Reset();
        ResultsSourceWidth = 0;
        ResultsSourceHeight = 0;
        ++ResultsSequence;
        ResultsTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    }

    LastFrameDetections.Reset();
    LastFrameSourceWidth = 0;
    LastFrameSourceHeight = 0;
    ++LastFrameSequence;
    LastFrameTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    ClearLastFrameSceneDepth();
}

bool UMyActorComponent::CaptureSceneToPixels(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
    const double CaptureStartSeconds = FPlatformTime::Seconds();
    OutWidth = 0;
    OutHeight = 0;

    double EnsureMs = 0.0;
    double SceneCaptureMs = 0.0;
    double ReadPixelsMs = 0.0;

    const double EnsureStartSeconds = FPlatformTime::Seconds();
    if (!EnsureOwnerCameraCapture() || !OwnerSceneCapture || !OwnerCaptureRenderTarget)
    {
        return false;
    }
    EnsureMs = (FPlatformTime::Seconds() - EnsureStartSeconds) * 1000.0;

    const double SceneCaptureStartSeconds = FPlatformTime::Seconds();
    OwnerSceneCapture->CaptureScene();
    SceneCaptureMs = (FPlatformTime::Seconds() - SceneCaptureStartSeconds) * 1000.0;

    FTextureRenderTargetResource* Resource = OwnerCaptureRenderTarget->GameThread_GetRenderTargetResource();
    if (!Resource)
    {
        return false;
    }

    TArray<FColor> LocalPixels;
    FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
    ReadFlags.SetLinearToGamma(false);
    const double ReadPixelsStartSeconds = FPlatformTime::Seconds();
    if (!Resource->ReadPixels(LocalPixels, ReadFlags))
    {
        return false;
    }
    ReadPixelsMs = (FPlatformTime::Seconds() - ReadPixelsStartSeconds) * 1000.0;

    if (LocalPixels.Num() != OwnerCaptureRenderTarget->SizeX * OwnerCaptureRenderTarget->SizeY)
    {
        return false;
    }

    OutWidth = OwnerCaptureRenderTarget->SizeX;
    OutHeight = OwnerCaptureRenderTarget->SizeY;
    OutPixels = MoveTemp(LocalPixels);

    if (ShouldLogFrameTimings())
    {
        const double TotalMs = (FPlatformTime::Seconds() - CaptureStartSeconds) * 1000.0;
        UE_LOG(LogTemp, Log, TEXT("DetectionCapture[%s]: total=%.2fms ensure=%.2fms capture=%.2fms readback=%.2fms size=%dx%d"),
            *GetNameSafe(GetOwner()),
            TotalMs,
            EnsureMs,
            SceneCaptureMs,
            ReadPixelsMs,
            OutWidth,
            OutHeight);
    }

    return true;
}

bool UMyActorComponent::CaptureOwnerSceneDepthToBuffer(TArray<float>& OutDepth, int32& OutWidth, int32& OutHeight)
{
    OutDepth.Reset();
    OutWidth = 0;
    OutHeight = 0;

    if (!EnsureOwnerCameraCapture() || !OwnerDepthSceneCapture || !OwnerDepthRenderTarget)
    {
        return false;
    }

    const double DepthCaptureStartSeconds = FPlatformTime::Seconds();
    OwnerDepthSceneCapture->CaptureScene();

    FTextureRenderTargetResource* Resource = OwnerDepthRenderTarget->GameThread_GetRenderTargetResource();
    if (!Resource)
    {
        return false;
    }

    TArray<FFloat16Color> DepthPixels;
    FReadSurfaceDataFlags ReadFlags(RCM_MinMax);
    ReadFlags.SetLinearToGamma(false);
    if (!Resource->ReadFloat16Pixels(DepthPixels, ReadFlags))
    {
        return false;
    }

    const int32 DepthWidth = OwnerDepthRenderTarget->SizeX;
    const int32 DepthHeight = OwnerDepthRenderTarget->SizeY;
    if (DepthPixels.Num() != DepthWidth * DepthHeight)
    {
        return false;
    }

    OutDepth.SetNumUninitialized(DepthPixels.Num());
    for (int32 Index = 0; Index < DepthPixels.Num(); ++Index)
    {
        const FFloat16Color& DepthPixel = DepthPixels[Index];
        OutDepth[Index] = DepthPixel.R.GetFloat();
    }

    OutWidth = DepthWidth;
    OutHeight = DepthHeight;

    if (ShouldLogFrameTimings())
    {
        UE_LOG(LogTemp, Log, TEXT("SceneDepthCapture[%s]: total=%.2fms size=%dx%d"),
            *GetNameSafe(GetOwner()),
            (FPlatformTime::Seconds() - DepthCaptureStartSeconds) * 1000.0,
            OutWidth,
            OutHeight);
    }

    return true;
}

void UMyActorComponent::PublishOwnerSceneDepth(TArray<float>&& Depth, int32 Width, int32 Height)
{
    if (Depth.Num() != Width * Height || Width <= 0 || Height <= 0)
    {
        ClearLastFrameSceneDepth();
        return;
    }

    LastFrameSceneDepth = MoveTemp(Depth);
    LastFrameSceneDepthWidth = Width;
    LastFrameSceneDepthHeight = Height;
    LastFrameSceneDepthTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void UMyActorComponent::ClearLastFrameSceneDepth()
{
    LastFrameSceneDepth.Reset();
    LastFrameSceneDepthWidth = 0;
    LastFrameSceneDepthHeight = 0;
    LastFrameSceneDepthTimeSeconds = -FLT_MAX;
}

void UMyActorComponent::ResetOwnerCameraReadback()
{
    PendingOwnerCameraReadback.Reset();
    PendingOwnerCameraReadbackWorld.Reset();
    PendingOwnerCameraReadbackGeneration = 0;
    PendingOwnerCameraReadbackFrameNumber = 0;
    PendingOwnerCameraReadbackWidth = 0;
    PendingOwnerCameraReadbackHeight = 0;
    PendingOwnerCameraReadbackSubmitTimeSeconds = 0.0;
    PendingOwnerCameraReadbackDepth.Reset();
    PendingOwnerCameraReadbackDepthWidth = 0;
    PendingOwnerCameraReadbackDepthHeight = 0;
}

void UMyActorComponent::AdvanceOwnerCameraReadbackGeneration()
{
    ++OwnerCameraReadbackGeneration;
    if (OwnerCameraReadbackGeneration == 0)
    {
        OwnerCameraReadbackGeneration = 1;
    }
}

bool UMyActorComponent::PollAsyncOwnerCameraReadback(TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
    OutWidth = 0;
    OutHeight = 0;

    UWorld* World = GetWorld();
    if (bIsEndingPlay.load() || !World || World->bIsTearingDown)
    {
        ResetOwnerCameraReadback();
        return false;
    }

    const double NowSeconds = FPlatformTime::Seconds();
    if (NowSeconds < OwnerCameraReadbackWarmupEndSeconds)
    {
        return false;
    }

    if (PendingOwnerCameraReadback.IsValid() &&
        (PendingOwnerCameraReadbackGeneration != OwnerCameraReadbackGeneration ||
            PendingOwnerCameraReadbackWorld.Get() != World))
    {
        UE_LOG(LogTemp, Warning, TEXT("Dropping async owner-camera readback for %s from stale PIE/world generation"),
            *GetNameSafe(GetOwner()));
        ResetOwnerCameraReadback();
        return false;
    }

    if (PendingOwnerCameraReadback.IsValid() &&
        PendingOwnerCameraReadbackSubmitTimeSeconds > 0.0 &&
        (NowSeconds - PendingOwnerCameraReadbackSubmitTimeSeconds) > MaxAsyncOwnerCameraReadbackLatencySeconds)
    {
        UE_LOG(LogTemp, Warning, TEXT("Dropping stale async owner-camera readback for %s before lock after %.2fms"),
            *GetNameSafe(GetOwner()),
            (NowSeconds - PendingOwnerCameraReadbackSubmitTimeSeconds) * 1000.0);
        ResetOwnerCameraReadback();
        return false;
    }

    if (!PendingOwnerCameraReadback.IsValid() || !PendingOwnerCameraReadback->IsReady())
    {
        return false;
    }

    if (PendingOwnerCameraReadbackWidth <= 0 || PendingOwnerCameraReadbackHeight <= 0)
    {
        ResetOwnerCameraReadback();
        return false;
    }

    if (GOwnerCameraReadbackLastLockFrame == GFrameCounter)
    {
        return false;
    }
    GOwnerCameraReadbackLastLockFrame = GFrameCounter;

    const double PollStartSeconds = FPlatformTime::Seconds();
    int32 RowPitchInPixels = 0;
    int32 BufferHeight = 0;
    const FColor* SourcePixels = static_cast<const FColor*>(
        PendingOwnerCameraReadback->Lock(RowPitchInPixels, &BufferHeight));
    if (!SourcePixels)
    {
        PendingOwnerCameraReadback->Unlock();
        ResetOwnerCameraReadback();
        return false;
    }

    OutWidth = PendingOwnerCameraReadbackWidth;
    OutHeight = PendingOwnerCameraReadbackHeight;
    if (RowPitchInPixels < OutWidth || BufferHeight < OutHeight)
    {
        UE_LOG(LogTemp, Warning, TEXT("Dropping async owner-camera readback for %s with invalid pitch/height row_pitch=%d buffer_h=%d size=%dx%d"),
            *GetNameSafe(GetOwner()),
            RowPitchInPixels,
            BufferHeight,
            OutWidth,
            OutHeight);
        PendingOwnerCameraReadback->Unlock();
        ResetOwnerCameraReadback();
        return false;
    }

    OutPixels.SetNumUninitialized(OutWidth * OutHeight);

    for (int32 Y = 0; Y < OutHeight; ++Y)
    {
        const FColor* SourceRow = SourcePixels + (Y * RowPitchInPixels);
        FColor* DestRow = OutPixels.GetData() + (Y * OutWidth);
        FMemory::Memcpy(DestRow, SourceRow, sizeof(FColor) * OutWidth);
    }

    PendingOwnerCameraReadback->Unlock();
    PendingOwnerCameraReadback.Reset();

    PublishOwnerSceneDepth(
        MoveTemp(PendingOwnerCameraReadbackDepth),
        PendingOwnerCameraReadbackDepthWidth,
        PendingOwnerCameraReadbackDepthHeight);

    if (ShouldLogFrameTimings())
    {
        const double LogNowSeconds = FPlatformTime::Seconds();
        UE_LOG(LogTemp, Log, TEXT("AsyncReadbackPoll[%s]: lock+copy=%.2fms latency=%.2fms row_pitch=%d buffer_h=%d size=%dx%d"),
            *GetNameSafe(GetOwner()),
            (LogNowSeconds - PollStartSeconds) * 1000.0,
            (LogNowSeconds - PendingOwnerCameraReadbackSubmitTimeSeconds) * 1000.0,
            RowPitchInPixels,
            BufferHeight,
            OutWidth,
            OutHeight);
    }

    ResetOwnerCameraReadback();
    return true;
}

bool UMyActorComponent::EnqueueAsyncOwnerCameraReadback()
{
    const double EnqueueStartSeconds = FPlatformTime::Seconds();
    if (PendingOwnerCameraReadback.IsValid())
    {
        return false;
    }

    UWorld* World = GetWorld();
    if (bIsEndingPlay.load() || !World || World->bIsTearingDown)
    {
        return false;
    }

    if (FPlatformTime::Seconds() < OwnerCameraReadbackWarmupEndSeconds)
    {
        return false;
    }

    if (GOwnerCameraReadbackLastEnqueueFrame == GFrameCounter)
    {
        return false;
    }

    if (!EnsureOwnerCameraCapture() || !OwnerSceneCapture || !OwnerCaptureRenderTarget)
    {
        return false;
    }

    const double SceneCaptureStartSeconds = FPlatformTime::Seconds();
    OwnerSceneCapture->CaptureScene();
    const double SceneCaptureMs = (FPlatformTime::Seconds() - SceneCaptureStartSeconds) * 1000.0;

    TArray<float> CapturedDepth;
    int32 CapturedDepthWidth = 0;
    int32 CapturedDepthHeight = 0;
    CaptureOwnerSceneDepthToBuffer(CapturedDepth, CapturedDepthWidth, CapturedDepthHeight);

    FTextureRenderTargetResource* Resource = OwnerCaptureRenderTarget->GameThread_GetRenderTargetResource();
    if (!Resource)
    {
        return false;
    }

    const FTextureRHIRef TextureRHI = Resource->GetRenderTargetTexture();
    if (!TextureRHI.IsValid())
    {
        return false;
    }

    PendingOwnerCameraReadback = MakeShared<FRHIGPUTextureReadback, ESPMode::ThreadSafe>(TEXT("CrowOwnerCameraReadback"));
    PendingOwnerCameraReadbackWorld = World;
    PendingOwnerCameraReadbackGeneration = OwnerCameraReadbackGeneration;
    PendingOwnerCameraReadbackFrameNumber = GFrameCounter;
    PendingOwnerCameraReadbackWidth = OwnerCaptureRenderTarget->SizeX;
    PendingOwnerCameraReadbackHeight = OwnerCaptureRenderTarget->SizeY;
    PendingOwnerCameraReadbackSubmitTimeSeconds = FPlatformTime::Seconds();
    PendingOwnerCameraReadbackDepth = MoveTemp(CapturedDepth);
    PendingOwnerCameraReadbackDepthWidth = CapturedDepthWidth;
    PendingOwnerCameraReadbackDepthHeight = CapturedDepthHeight;
    TSharedPtr<FRHIGPUTextureReadback, ESPMode::ThreadSafe> Readback = PendingOwnerCameraReadback;
    const FIntVector ReadbackSize(PendingOwnerCameraReadbackWidth, PendingOwnerCameraReadbackHeight, 1);

    ENQUEUE_RENDER_COMMAND(EnqueueCrowOwnerCameraReadback)(
        [Readback, TextureRHI, ReadbackSize](FRHICommandListImmediate& RHICmdList)
        {
            if (Readback.IsValid())
            {
                Readback->EnqueueCopy(RHICmdList, TextureRHI, FIntVector::ZeroValue, 0, ReadbackSize);
            }
        });
    GOwnerCameraReadbackLastEnqueueFrame = GFrameCounter;

    if (ShouldLogFrameTimings())
    {
        UE_LOG(LogTemp, Log, TEXT("AsyncReadbackEnqueue[%s]: total=%.2fms capture=%.2fms size=%dx%d"),
            *GetNameSafe(GetOwner()),
            (FPlatformTime::Seconds() - EnqueueStartSeconds) * 1000.0,
            SceneCaptureMs,
            PendingOwnerCameraReadbackWidth,
            PendingOwnerCameraReadbackHeight);
    }

    return true;
}

void UMyActorComponent::CaptureAndEnqueue(bool bSubmitDetection)
{
    if (bIsEndingPlay.load())
    {
        return;
    }

    const double CaptureAndEnqueueStartSeconds = FPlatformTime::Seconds();
    TArray<FColor> Pixels;
    int32 SourceWidth = 0;
    int32 SourceHeight = 0;

    bool bCaptured = false;
    if (bUseOwnerCameraCapture && bUseAsyncOwnerCameraReadback)
    {
        bCaptured = PollAsyncOwnerCameraReadback(Pixels, SourceWidth, SourceHeight);
        EnqueueAsyncOwnerCameraReadback();
    }
    else
    {
        bCaptured = bUseOwnerCameraCapture
            ? CaptureSceneToPixels(Pixels, SourceWidth, SourceHeight)
            : CaptureViewportToPixels(Pixels, SourceWidth, SourceHeight);
    }

    if (!bCaptured)
    {
        return;
    }

    if (bUseOwnerCameraCapture && !bUseAsyncOwnerCameraReadback)
    {
        TArray<float> CapturedDepth;
        int32 CapturedDepthWidth = 0;
        int32 CapturedDepthHeight = 0;
        if (CaptureOwnerSceneDepthToBuffer(CapturedDepth, CapturedDepthWidth, CapturedDepthHeight))
        {
            PublishOwnerSceneDepth(MoveTemp(CapturedDepth), CapturedDepthWidth, CapturedDepthHeight);
        }
        else
        {
            ClearLastFrameSceneDepth();
        }
    }
    else if (!bUseOwnerCameraCapture)
    {
        ClearLastFrameSceneDepth();
    }

    RecordOwnerCameraVideoFrame(Pixels, SourceWidth, SourceHeight);

    if (!bSubmitDetection)
    {
        ClearPublishedResults();
        return;
    }

    bool bHasDetectionEvaluation = false;
    bool bExpectedInFov = false;
    FVector2D DetectionTargetPixel = FVector2D::ZeroVector;
    bExpectedInFov = EvaluateDetectionTimingTarget(
        SourceWidth,
        SourceHeight,
        DetectionTargetPixel,
        bHasDetectionEvaluation);

    if (bUseSharedVisionModel)
    {
        if (bIsEndingPlay.load())
        {
            return;
        }

        if (UWorld* World = GetWorld())
        {
            if (UCrowVisionSubsystem* VisionSubsystem = World->GetSubsystem<UCrowVisionSubsystem>())
            {
                VisionSubsystem->SubmitFrame(
                    this,
                    MoveTemp(Pixels),
                    SourceWidth,
                    SourceHeight,
                    DetectionTargetPixel,
                    bHasDetectionEvaluation,
                    bExpectedInFov);
                if (ShouldLogFrameTimings())
                {
                    const double TotalMs = (FPlatformTime::Seconds() - CaptureAndEnqueueStartSeconds) * 1000.0;
                    UE_LOG(LogTemp, Log, TEXT("DetectionSubmit[%s]: capture+submit=%.2fms size=%dx%d"),
                        *GetNameSafe(GetOwner()),
                        TotalMs,
                        SourceWidth,
                        SourceHeight);
                }
            }
        }
        return;
    }

    TSharedPtr<FFrameData> Frame = MakeShared<FFrameData>();
    Frame->Pixels = MoveTemp(Pixels);
    Frame->Width = SourceWidth;
    Frame->Height = SourceHeight;
    Frame->DetectionTargetPixel = DetectionTargetPixel;
    Frame->bHasDetectionEvaluation = bHasDetectionEvaluation;
    Frame->bExpectedInFov = bExpectedInFov;

    {
        FScopeLock Lock(&FrameMutex);
        LatestFrame = Frame;
    }

    if (ShouldLogFrameTimings())
    {
        const double TotalMs = (FPlatformTime::Seconds() - CaptureAndEnqueueStartSeconds) * 1000.0;
        UE_LOG(LogTemp, Log, TEXT("DetectionEnqueue[%s]: capture+enqueue=%.2fms size=%dx%d"),
            *GetNameSafe(GetOwner()),
            TotalMs,
            SourceWidth,
            SourceHeight);
    }
}

void UMyActorComponent::StartWorker()
{
    if (bWorkerRunning.load()) return;

    if (WorkerThread)
    {
        if (WorkerThread->joinable())
        {
            WorkerThread->join();
        }
        delete WorkerThread;
        WorkerThread = nullptr;
    }

    bWorkerRunning.store(true);
    WorkerThread = new std::thread([this]()
    {
        try
        {
            while (bWorkerRunning.load())
            {
                TSharedPtr<FFrameData> Frame;
                {
                    FScopeLock Lock(&FrameMutex);
                    Frame = LatestFrame;
                    LatestFrame.Reset();
                }

                if (!Frame.IsValid())
                {
                    FPlatformProcess::Sleep(0.001f);
                    continue;
                }

                TArray<FDetectionResult> Det;
                double FrameInferMs = -1.0;
                const double FrameT0 = FPlatformTime::Seconds();
                try
                {
                    FScopeLock InferenceLock(&InferenceMutex);
                    if (!bIsModelLoaded && !LoadYOLO())
                    {
                        Det.Reset();
                    }
                    else
                    {
                        Det = ProcessWithOpenCV_BG(Frame->Pixels, Frame->Width, Frame->Height, &FrameInferMs);
                    }
                }
                catch (const cv::Exception& e)
                {
                    UE_LOG(LogTemp, Error, TEXT("OpenCV exception in worker: %s"), *FString(e.what()));
                    Det.Reset();
                }
                catch (const std::exception& e)
                {
                    UE_LOG(LogTemp, Error, TEXT("std::exception in worker: %s"), *FString(e.what()));
                    Det.Reset();
                }
                catch (...)
                {
                    UE_LOG(LogTemp, Error, TEXT("Unknown exception in worker"));
                    Det.Reset();
                }

                int32 LoggedSequence = 0;
                int32 LoggedDetectionCount = 0;
                const bool bDetectionSucceeded = Frame->bHasDetectionEvaluation &&
                    Frame->bExpectedInFov &&
                    HasDetectionContainingPixel(Det, Frame->DetectionTargetPixel);
                {
                    FScopeLock Lock(&ResultsMutex);
                    ResultsShared = MoveTemp(Det);
                    ResultsSourceWidth = Frame->Width;
                    ResultsSourceHeight = Frame->Height;
                    ++ResultsSequence;
                    ResultsTimeSeconds = 0.f;
                    LoggedSequence = ResultsSequence;
                    LoggedDetectionCount = ResultsShared.Num();
                }
                const double FrameTotalMs = (FPlatformTime::Seconds() - FrameT0) * 1000.0;
                AppendFrameTimingLogLine(
                    LoggedSequence,
                    Frame->Width,
                    Frame->Height,
                    LoggedDetectionCount,
                    FrameTotalMs,
                    FrameInferMs,
                    bDetectionSucceeded,
                    Frame->bHasDetectionEvaluation,
                    Frame->bExpectedInFov);
            }
        }
        catch (const cv::Exception& e)
        {
            UE_LOG(LogTemp, Error, TEXT("OpenCV exception in worker startup: %s"), *FString(e.what()));
        }
        catch (const std::exception& e)
        {
            UE_LOG(LogTemp, Error, TEXT("std::exception in worker startup: %s"), *FString(e.what()));
        }
        catch (...)
        {
            UE_LOG(LogTemp, Error, TEXT("Unknown exception in worker startup"));
        }

        bWorkerRunning.store(false);
    });

}

void UMyActorComponent::StopWorker()
{
    RequestOnnxRuntimeInferenceTerminate();
    bWorkerRunning.store(false);
    {
        FScopeLock Lock(&FrameMutex);
        LatestFrame.Reset();
    }
    if (WorkerThread)
    {
        if (WorkerThread->joinable())
        {
            if (WorkerThread->get_id() == std::this_thread::get_id())
            {
                WorkerThread->detach();
            }
            else
            {
                WorkerThread->join();
            }
        }
        delete WorkerThread;
        WorkerThread = nullptr;
    }

    FScopeLock InferenceLock(&InferenceMutex);
    FlushFrameTimingLog(true);
    {
        FScopeLock Lock(&ResultsMutex);
        ResultsShared.Reset();
        ResultsSourceWidth = 0;
        ResultsSourceHeight = 0;
        ResultsSequence = 0;
        ResultsTimeSeconds = -FLT_MAX;
    }
    LastFrameDetections.Reset();
    LastFrameSourceWidth = 0;
    LastFrameSourceHeight = 0;
    LastFrameSequence = 0;
    LastFrameTimeSeconds = -FLT_MAX;
    ReleaseTensorRT();
    ReleaseOnnxRuntime();
    OpenCVDnnNet = cv::dnn::Net();
}

void UMyActorComponent::RequestInferenceShutdown()
{
    bInferenceShutdownRequested.store(true);
    RequestOnnxRuntimeInferenceTerminate();
}

void UMyActorComponent::RequestOnnxRuntimeInferenceTerminate()
{
#if WITH_ONNXRUNTIME
    FScopeLock Lock(&OnnxRuntimeRunOptionsMutex);
    if (!OnnxRuntimeRunOptions)
    {
        return;
    }

    try
    {
        OnnxRuntimeRunOptions->SetTerminate();
    }
    catch (const Ort::Exception& e)
    {
        UE_LOG(LogTemp, Warning, TEXT("ONNX Runtime run termination request failed: %s"), *FString(e.what()));
    }
#endif
}

void UMyActorComponent::CopyResultsFromWorker()
{
    TArray<FDetectionResult> LocalResults;
    int32 SourceWidth = 0;
    int32 SourceHeight = 0;
    int32 Sequence = 0;
    float ResultTimeSeconds = -FLT_MAX;
    {
        FScopeLock Lock(&ResultsMutex);
        LocalResults = ResultsShared;
        SourceWidth = ResultsSourceWidth;
        SourceHeight = ResultsSourceHeight;
        Sequence = ResultsSequence;
        ResultTimeSeconds = ResultsTimeSeconds;
    }

    if (SourceWidth <= 0 || SourceHeight <= 0)
    {
        LastFrameDetections.Reset();
        LastFrameSourceWidth = 0;
        LastFrameSourceHeight = 0;
        LastFrameSequence = 0;
        LastFrameTimeSeconds = -FLT_MAX;
        return;
    }

    if (Sequence == LastFrameSequence)
    {
        return;
    }

    LastFrameDetections = MoveTemp(LocalResults);
    LastFrameSourceWidth = SourceWidth;
    LastFrameSourceHeight = SourceHeight;
    LastFrameSequence = Sequence;
    LastFrameTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : ResultTimeSeconds;

    LogFovDetectionMetricSample(TEXT("model_local"), LastFrameDetections, SourceWidth, SourceHeight);
}

TArray<FDetectionResult> UMyActorComponent::ProcessSharedVisionFrame(
    const TArray<FColor>& Bitmap,
    int32 Width,
    int32 Height,
    double* OutInferenceMs)
{
    if (bIsEndingPlay.load() || bInferenceShutdownRequested.load())
    {
        return {};
    }

    FScopeLock InferenceLock(&InferenceMutex);
    if (bIsEndingPlay.load() || bInferenceShutdownRequested.load())
    {
        return {};
    }

    if (!bIsModelLoaded && !LoadYOLO())
    {
        return {};
    }

    const double StartSeconds = FPlatformTime::Seconds();
    if (bIsEndingPlay.load() || bInferenceShutdownRequested.load())
    {
        return {};
    }

    TArray<FDetectionResult> Detections = ProcessWithOpenCV_BG(Bitmap, Width, Height, OutInferenceMs);

    if (ShouldLogFrameTimings())
    {
        UE_LOG(LogTemp, Log, TEXT("SharedInferenceHost: total=%.2fms infer=%.2fms detections=%d size=%dx%d"),
            (FPlatformTime::Seconds() - StartSeconds) * 1000.0,
            OutInferenceMs ? *OutInferenceMs : -1.0,
            Detections.Num(),
            Width,
            Height);
    }

    return Detections;
}

void UMyActorComponent::RecordDetectionModelFrameTiming(
    int32 Sequence,
    int32 Width,
    int32 Height,
    const TArray<FDetectionResult>& Detections,
    double TotalMs,
    double InferMs,
    const FVector2D& DetectionTargetPixel,
    bool bHasDetectionEvaluation,
    bool bExpectedInFov)
{
    const bool bDetectionSucceeded = bHasDetectionEvaluation &&
        bExpectedInFov &&
        HasDetectionContainingPixel(Detections, DetectionTargetPixel);
    AppendFrameTimingLogLine(
        Sequence,
        Width,
        Height,
        Detections.Num(),
        TotalMs,
        InferMs,
        bDetectionSucceeded,
        bHasDetectionEvaluation,
        bExpectedInFov);
}

int32 UMyActorComponent::BeginSharedVisionRequest()
{
    if (bIsEndingPlay.load())
    {
        return 0;
    }

    return ++SharedVisionRequestSerial;
}

void UMyActorComponent::ConsumeSharedVisionResult(
    TArray<FDetectionResult>&& Detections,
    int32 SourceWidth,
    int32 SourceHeight,
    int32 RequestSerial)
{
    UWorld* World = GetWorld();
    AActor* OwnerActor = GetOwner();
    if (bIsEndingPlay.load() ||
        RequestSerial <= 0 ||
        RequestSerial != SharedVisionRequestSerial.load() ||
        !World ||
        World->bIsTearingDown ||
        !IsValid(OwnerActor) ||
        !IsRegistered())
    {
        return;
    }

    if (SourceWidth <= 0 || SourceHeight <= 0)
    {
        LastFrameDetections.Reset();
        LastFrameSourceWidth = 0;
        LastFrameSourceHeight = 0;
        ++LastFrameSequence;
        LastFrameTimeSeconds = World->GetTimeSeconds();
        return;
    }

    LastFrameDetections = MoveTemp(Detections);
    LastFrameSourceWidth = SourceWidth;
    LastFrameSourceHeight = SourceHeight;
    ++LastFrameSequence;
    LastFrameTimeSeconds = World->GetTimeSeconds();

    if (ShouldLogFrameTimings())
    {
        UE_LOG(LogTemp, Log, TEXT("SharedResult[%s]: detections=%d size=%dx%d seq=%d"),
            *GetNameSafe(OwnerActor),
            LastFrameDetections.Num(),
            SourceWidth,
            SourceHeight,
            LastFrameSequence);
    }
}

void UMyActorComponent::ResetInferenceOutputState()
{
    TrtInputHost.Reset();
    TrtOutputHost.Reset();
    TrtInputElements = 0;
    TrtOutputElements = 0;
    TrtOutputChannels = 0;
    TrtOutputDetections = 0;
}

TArray<EDetectionInferenceBackend> UMyActorComponent::BuildAutomaticInferenceBackendCandidates(const FString& ModelPathUE) const
{
    TArray<EDetectionInferenceBackend> Candidates;
    auto AddCandidate = [&Candidates](EDetectionInferenceBackend Backend)
    {
        Candidates.AddUnique(Backend);
    };

    const FString ModelExtension = FPaths::GetExtension(ModelPathUE);
    const bool bTensorFamilyModel =
        ModelExtension.IsEmpty() ||
        ModelPathUE.EndsWith(TEXT(".onnx"), ESearchCase::IgnoreCase) ||
        ModelPathUE.EndsWith(TEXT(".plan"), ESearchCase::IgnoreCase);

    const EDetectedOperatingSystem DetectedOS = DetectOperatingSystem();
    const EDetectedGpuVendor DetectedGpu = DetectGpuVendor(GRHIAdapterName);

    if (bTensorFamilyModel &&
        (DetectedOS == EDetectedOperatingSystem::Windows || DetectedOS == EDetectedOperatingSystem::Linux) &&
        DetectedGpu == EDetectedGpuVendor::Nvidia)
    {
#if WITH_TENSORRT
        AddCandidate(EDetectionInferenceBackend::TensorRT);
#else
        UE_LOG(LogTemp, Warning, TEXT("NVIDIA adapter detected, but TensorRT is not compiled into this build. Skipping TensorRT Auto candidate."));
#endif
    }

    if (bTensorFamilyModel)
    {
#if WITH_ONNXRUNTIME
        AddCandidate(EDetectionInferenceBackend::ONNXRuntime);
#else
        UE_LOG(LogTemp, Warning, TEXT("ONNX Runtime is not compiled into this build. Auto will use OpenCV DNN CPU fallback."));
#endif
    }

    AddCandidate(EDetectionInferenceBackend::OpenCVDNN);
    return Candidates;
}

EDetectionInferenceBackend UMyActorComponent::DetectAutomaticInferenceBackend(const FString& ModelPathUE) const
{
    const TArray<EDetectionInferenceBackend> Candidates = BuildAutomaticInferenceBackendCandidates(ModelPathUE);
    if (Candidates.Num() > 0)
    {
        return Candidates[0];
    }

    return EDetectionInferenceBackend::OpenCVDNN;
}

EDetectionInferenceBackend UMyActorComponent::ResolveEffectiveInferenceBackend(const FString& ModelPathUE) const
{
    EDetectionInferenceBackend RequestedBackend = InferenceBackend;
    if (RequestedBackend == EDetectionInferenceBackend::Auto)
    {
        RequestedBackend = DetectAutomaticInferenceBackend(ModelPathUE);
    }

    if (RequestedBackend == EDetectionInferenceBackend::TensorRT)
    {
#if WITH_TENSORRT
        return EDetectionInferenceBackend::TensorRT;
#else
        UE_LOG(LogTemp, Warning, TEXT("TensorRT backend requested but this build was compiled without TensorRT. Falling back."));
        if (ModelPathUE.EndsWith(TEXT(".plan"), ESearchCase::IgnoreCase))
        {
            return EDetectionInferenceBackend::TensorRT;
        }
        RequestedBackend = EDetectionInferenceBackend::Auto;
#endif
    }

    if (RequestedBackend == EDetectionInferenceBackend::ONNXRuntime)
    {
#if WITH_ONNXRUNTIME
        return EDetectionInferenceBackend::ONNXRuntime;
#else
        UE_LOG(LogTemp, Warning, TEXT("ONNX Runtime backend requested but this build was compiled without ONNX Runtime. Falling back to OpenCV DNN."));
        return EDetectionInferenceBackend::OpenCVDNN;
#endif
    }

    if (RequestedBackend == EDetectionInferenceBackend::Auto)
    {
        return DetectAutomaticInferenceBackend(ModelPathUE);
    }

    return EDetectionInferenceBackend::OpenCVDNN;
}

EOnnxRuntimeExecutionProvider UMyActorComponent::ResolveEffectiveOnnxRuntimeProvider() const
{
    if (ShouldForceOnnxRuntimeCpuAfterGpuCrash())
    {
        return EOnnxRuntimeExecutionProvider::CPU;
    }

    if (OnnxRuntimeExecutionProvider != EOnnxRuntimeExecutionProvider::Auto)
    {
        return OnnxRuntimeExecutionProvider;
    }

    const EDetectedOperatingSystem DetectedOS = DetectOperatingSystem();
    const EDetectedGpuVendor DetectedGpu = DetectGpuVendor(GRHIAdapterName);

#if PLATFORM_WINDOWS
    if (DetectedOS == EDetectedOperatingSystem::Windows && DetectedGpu == EDetectedGpuVendor::AMD)
    {
#if WITH_ONNXRUNTIME_DML
        return EOnnxRuntimeExecutionProvider::DirectML;
#else
        UE_LOG(LogTemp, Warning, TEXT("AMD adapter detected, but ONNX Runtime DirectML provider is not compiled into this build. Using CPU provider."));
#endif
    }
#endif

#if PLATFORM_LINUX
    if (DetectedOS == EDetectedOperatingSystem::Linux && DetectedGpu == EDetectedGpuVendor::AMD)
    {
#if WITH_ONNXRUNTIME_MIGRAPHX
        return EOnnxRuntimeExecutionProvider::MIGraphX;
#else
        UE_LOG(LogTemp, Warning, TEXT("AMD adapter detected, but ONNX Runtime MIGraphX provider is not compiled into this build. Using CPU provider."));
#endif
    }
#endif

    return EOnnxRuntimeExecutionProvider::CPU;
}

bool UMyActorComponent::ShouldForceOnnxRuntimeCpuAfterGpuCrash() const
{
#if WITH_ONNXRUNTIME
    const bool bGpuProviderRequested =
        OnnxRuntimeExecutionProvider == EOnnxRuntimeExecutionProvider::Auto ||
        OnnxRuntimeExecutionProvider == EOnnxRuntimeExecutionProvider::DirectML ||
        OnnxRuntimeExecutionProvider == EOnnxRuntimeExecutionProvider::MIGraphX;
    if (!bGpuProviderRequested)
    {
        return false;
    }

    const FString GuardPath = GetOnnxRuntimeGpuCrashGuardPath();
    if (FPaths::FileExists(GuardPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("ONNX Runtime GPU crash guard found at %s. Forcing CPU provider for this run. Delete the file to retry GPU inference."),
            *GuardPath);
        return true;
    }
#endif

    return false;
}

void UMyActorComponent::MarkOnnxRuntimeGpuSessionActive() const
{
#if WITH_ONNXRUNTIME
    const FString GuardPath = GetOnnxRuntimeGpuCrashGuardPath();
    const FString GuardText = FString::Printf(
        TEXT("ONNX Runtime GPU inference session was active.\nProvider=%s\nAdapter=%s\n"),
        OnnxProviderToString(EffectiveOnnxRuntimeExecutionProvider),
        *GRHIAdapterName);
    if (!FFileHelper::SaveStringToFile(GuardText, *GuardPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to write ONNX Runtime GPU crash guard: %s"), *GuardPath);
    }
#endif
}

void UMyActorComponent::ClearOnnxRuntimeGpuSessionActive() const
{
#if WITH_ONNXRUNTIME
    const FString GuardPath = GetOnnxRuntimeGpuCrashGuardPath();
    if (FPaths::FileExists(GuardPath) && !IFileManager::Get().Delete(*GuardPath, false, true))
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to clear ONNX Runtime GPU crash guard: %s"), *GuardPath);
    }
#endif
}

FString UMyActorComponent::ResolveModelPathForBackend(const FString& ModelPathUE, EDetectionInferenceBackend Backend) const
{
    const FString ResolvedModelAssetPath = EagleEyeModelDiscovery::ResolveModelPathForBackend(ModelPathUE, Backend);
    if (!ResolvedModelAssetPath.IsEmpty())
    {
        return ResolvedModelAssetPath;
    }

    if (Backend == EDetectionInferenceBackend::TensorRT &&
        ModelPathUE.EndsWith(TEXT(".onnx"), ESearchCase::IgnoreCase))
    {
        const FString CandidatePlanPath = FPaths::ChangeExtension(ModelPathUE, TEXT("plan"));
        if (RuntimeFileExists(CandidatePlanPath))
        {
            return CandidatePlanPath;
        }
    }

    if ((Backend == EDetectionInferenceBackend::ONNXRuntime || Backend == EDetectionInferenceBackend::OpenCVDNN) &&
        ModelPathUE.EndsWith(TEXT(".plan"), ESearchCase::IgnoreCase))
    {
        const FString CandidateOnnxPath = FPaths::ChangeExtension(ModelPathUE, TEXT("onnx"));
        if (RuntimeFileExists(CandidateOnnxPath))
        {
            return CandidateOnnxPath;
        }

        FString BaseName = FPaths::GetBaseFilename(ModelPathUE);
        const FString DirPath = FPaths::GetPath(ModelPathUE);
        if (BaseName.EndsWith(TEXT("_fp16")) || BaseName.EndsWith(TEXT("_fp32")) || BaseName.EndsWith(TEXT("_int8")))
        {
            BaseName = BaseName.LeftChop(5);
        }

        const FString AltOnnxPath = FPaths::Combine(DirPath, BaseName + TEXT(".onnx"));
        if (RuntimeFileExists(AltOnnxPath))
        {
            return AltOnnxPath;
        }
    }

    return ModelPathUE;
}

		bool UMyActorComponent::LoadYOLO()
		{
            if (InferenceBackend == EDetectionInferenceBackend::Auto)
            {
                const FString ModelPathUE = FString(WeightsPath.c_str());
                const TArray<EDetectionInferenceBackend> BackendCandidates = BuildAutomaticInferenceBackendCandidates(ModelPathUE);
                const EDetectionInferenceBackend SavedInferenceBackend = InferenceBackend;
                const EOnnxRuntimeExecutionProvider SavedOnnxProvider = OnnxRuntimeExecutionProvider;
                const bool bSavedOpenCVDNNPreferCUDA = bOpenCVDNNPreferCUDA;

                auto RestoreAutoSettings = [&]()
                {
                    InferenceBackend = SavedInferenceBackend;
                    OnnxRuntimeExecutionProvider = SavedOnnxProvider;
                    bOpenCVDNNPreferCUDA = bSavedOpenCVDNNPreferCUDA;
                };

                const EDetectedOperatingSystem DetectedOS = DetectOperatingSystem();
                const EDetectedGpuVendor DetectedGpu = DetectGpuVendor(GRHIAdapterName);
                UE_LOG(LogTemp, Log, TEXT("Auto inference candidates (OS=%s, GPU=%s, adapter=%s, model=%s): %d"),
                    OperatingSystemToString(DetectedOS),
                    GpuVendorToString(DetectedGpu),
                    *GRHIAdapterName,
                    *ModelPathUE,
                    BackendCandidates.Num());

                for (EDetectionInferenceBackend CandidateBackend : BackendCandidates)
                {
                    InferenceBackend = CandidateBackend;
                    OnnxRuntimeExecutionProvider = SavedOnnxProvider;
                    bOpenCVDNNPreferCUDA = (CandidateBackend == EDetectionInferenceBackend::OpenCVDNN)
                        ? false
                        : bSavedOpenCVDNNPreferCUDA;

                    UE_LOG(LogTemp, Log, TEXT("Auto inference trying backend: %s%s"),
                        BackendToString(CandidateBackend),
                        CandidateBackend == EDetectionInferenceBackend::OpenCVDNN ? TEXT(" (CPU fallback)") : TEXT(""));

                    if (LoadYOLO())
                    {
                        RestoreAutoSettings();
                        return true;
                    }

                    ReleaseTensorRT();
                    ReleaseOnnxRuntime();
                    OpenCVDnnNet = cv::dnn::Net();
                    bIsModelLoaded = false;

                    if (CandidateBackend == EDetectionInferenceBackend::ONNXRuntime &&
                        SavedOnnxProvider == EOnnxRuntimeExecutionProvider::Auto)
                    {
                        InferenceBackend = EDetectionInferenceBackend::ONNXRuntime;
                        OnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::CPU;
                        UE_LOG(LogTemp, Warning, TEXT("Auto inference retrying ONNX Runtime with CPU provider."));

                        if (LoadYOLO())
                        {
                            RestoreAutoSettings();
                            return true;
                        }

                        ReleaseOnnxRuntime();
                        bIsModelLoaded = false;
                    }

                    UE_LOG(LogTemp, Warning, TEXT("Auto inference backend failed, trying next option: %s"),
                        BackendToString(CandidateBackend));
                }

                RestoreAutoSettings();
                UE_LOG(LogTemp, Error, TEXT("Auto inference failed: no backend could load model %s"), *ModelPathUE);
                return false;
            }

		    try
		    {
	        cv::setUseOptimized(true);
	        cv::setNumThreads(FPlatformMisc::NumberOfCoresIncludingHyperthreads());

	        ReleaseTensorRT();
            ReleaseOnnxRuntime();
	        OpenCVDnnNet = cv::dnn::Net();

		        FString ModelPathUE = FString(WeightsPath.c_str());
		        if (ModelPathUE.IsEmpty())
		        {
		            UE_LOG(LogTemp, Error, TEXT("Model selection is empty"));
		            return false;
		        }

            EffectiveInferenceBackend = ResolveEffectiveInferenceBackend(ModelPathUE);
            ModelPathUE = ResolveModelPathForBackend(ModelPathUE, EffectiveInferenceBackend);
            const EDetectedOperatingSystem DetectedOS = DetectOperatingSystem();
            const EDetectedGpuVendor DetectedGpu = DetectGpuVendor(GRHIAdapterName);
            UE_LOG(LogTemp, Log, TEXT("LoadYOLO selected backend: %s (requested=%s, ONNX provider=%s, OS=%s, GPU=%s, adapter=%s, model=%s)"),
                BackendToString(EffectiveInferenceBackend),
                BackendToString(InferenceBackend),
                OnnxProviderToString(OnnxRuntimeExecutionProvider),
                OperatingSystemToString(DetectedOS),
                GpuVendorToString(DetectedGpu),
                *GRHIAdapterName,
                *ModelPathUE);

            if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*ModelPathUE))
            {
                UE_LOG(LogTemp, Error, TEXT("Resolved model file not found: %s"), *ModelPathUE);
                return false;
            }
            ActiveDetectionModelPath = ModelPathUE;

            if (EffectiveInferenceBackend == EDetectionInferenceBackend::TensorRT)
            {
#if WITH_TENSORRT
                bIsOnnxModel = ModelPathUE.EndsWith(TEXT(".onnx")) || ModelPathUE.EndsWith(TEXT(".plan"));
                if (!ModelPathUE.EndsWith(TEXT(".plan")))
                {
                    UE_LOG(LogTemp, Error, TEXT("TensorRT requires a .plan engine file, got: %s"), *ModelPathUE);
                    return false;
                }

                TArray<uint8> PlanData;
                if (!FFileHelper::LoadFileToArray(PlanData, *ModelPathUE) || PlanData.Num() == 0)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to load TensorRT plan: %s"), *ModelPathUE);
                    return false;
                }

                TrtRuntime = nvinfer1::createInferRuntime(GTrtLogger);
                if (!TrtRuntime)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to create TensorRT runtime"));
                    return false;
                }

                TrtEngine = TrtRuntime->deserializeCudaEngine(PlanData.GetData(), PlanData.Num());
                if (!TrtEngine)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to deserialize TensorRT engine"));
                    ReleaseTensorRT();
                    return false;
                }

                TrtContext = TrtEngine->createExecutionContext();
                if (!TrtContext)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to create TensorRT execution context"));
                    ReleaseTensorRT();
                    return false;
                }

                const nvinfer1::Dims InputDims = TrtEngine->getTensorShape(kTrtInputName);
                const nvinfer1::Dims OutputDims = TrtEngine->getTensorShape(kTrtOutputName);

                const int64 InputVolume = CalcTrtVolume(InputDims);
                const int64 OutputVolume = CalcTrtVolume(OutputDims);
                if (InputVolume <= 0 || OutputVolume <= 0)
                {
                    UE_LOG(LogTemp, Error, TEXT("Invalid TensorRT tensor shapes (input=%d dims, output=%d dims)"),
                        InputDims.nbDims, OutputDims.nbDims);
                    ReleaseTensorRT();
                    return false;
                }

                if (InputDims.nbDims >= 4 && InputDims.d[2] > 0 && InputDims.d[3] > 0)
                {
                    ModelInputHeight = InputDims.d[2];
                    ModelInputWidth = InputDims.d[3];
                }
                else
                {
                    const int32 ClampedInputSize = FMath::Clamp(OnnxInputSize, 160, 1280);
                    ModelInputWidth = ClampedInputSize;
                    ModelInputHeight = ClampedInputSize;
                }

                TrtInputElements = static_cast<int32>(InputVolume);
                TrtOutputElements = static_cast<int32>(OutputVolume);
                TrtOutputChannels = (OutputDims.nbDims == 3) ? OutputDims.d[1] : 0;
                TrtOutputDetections = (OutputDims.nbDims == 3) ? OutputDims.d[2] : 0;
                TrtInputHost.SetNumUninitialized(TrtInputElements);
                TrtOutputHost.SetNumUninitialized(TrtOutputElements);

                const size_t InputBytes = static_cast<size_t>(TrtInputElements) * sizeof(float);
                const size_t OutputBytes = static_cast<size_t>(TrtOutputElements) * sizeof(float);

                const cudaError_t StreamErr = cudaStreamCreate(&TrtStream);
                if (StreamErr != cudaSuccess)
                {
                    UE_LOG(LogTemp, Error, TEXT("cudaStreamCreate failed: %s"), *FString(cudaGetErrorString(StreamErr)));
                    ReleaseTensorRT();
                    return false;
                }

                const cudaError_t InAllocErr = cudaMalloc(&TrtInputDevice, InputBytes);
                if (InAllocErr != cudaSuccess)
                {
                    UE_LOG(LogTemp, Error, TEXT("cudaMalloc input failed: %s"), *FString(cudaGetErrorString(InAllocErr)));
                    ReleaseTensorRT();
                    return false;
                }

                const cudaError_t OutAllocErr = cudaMalloc(&TrtOutputDevice, OutputBytes);
                if (OutAllocErr != cudaSuccess)
                {
                    UE_LOG(LogTemp, Error, TEXT("cudaMalloc output failed: %s"), *FString(cudaGetErrorString(OutAllocErr)));
                    ReleaseTensorRT();
                    return false;
                }

                UE_LOG(LogTemp, Log, TEXT("TensorRT engine loaded: input=%s, output=%s, elements=%d"),
                    *TrtDimsToString(InputDims), *TrtDimsToString(OutputDims), TrtOutputElements);
                bIsModelLoaded = true;
                return true;
#else
                UE_LOG(LogTemp, Error, TEXT("TensorRT backend selected but this build was compiled without TensorRT."));
                return false;
#endif
            }

            if (EffectiveInferenceBackend == EDetectionInferenceBackend::ONNXRuntime)
            {
                return LoadOnnxRuntime(ModelPathUE);
            }

            if (EffectiveInferenceBackend != EDetectionInferenceBackend::OpenCVDNN)
            {
                UE_LOG(LogTemp, Error, TEXT("Unsupported inference backend value: %d"), static_cast<int32>(EffectiveInferenceBackend));
                return false;
            }

            FString OpenCvModelPath = ResolveModelPathForBackend(ModelPathUE, EDetectionInferenceBackend::OpenCVDNN);
            if (OpenCvModelPath.EndsWith(TEXT(".plan")))
            {
                const FString CandidateOnnxPath = FPaths::ChangeExtension(OpenCvModelPath, TEXT("onnx"));
                if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*CandidateOnnxPath))
                {
                    OpenCvModelPath = CandidateOnnxPath;
                }
                else
                {
                    FString BaseName = FPaths::GetBaseFilename(OpenCvModelPath);
                    const FString DirPath = FPaths::GetPath(OpenCvModelPath);
                    if (BaseName.EndsWith(TEXT("_fp16")) || BaseName.EndsWith(TEXT("_fp32")) || BaseName.EndsWith(TEXT("_int8")))
                    {
                        BaseName = BaseName.LeftChop(5);
                    }
                    const FString AltOnnxPath = FPaths::Combine(DirPath, BaseName + TEXT(".onnx"));
                    if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*AltOnnxPath))
                    {
                        OpenCvModelPath = AltOnnxPath;
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("OpenCV DNN cannot load .plan. Tried %s and %s"),
                            *CandidateOnnxPath, *AltOnnxPath);
                        return false;
                    }
                }
            }

            if (OpenCvModelPath.EndsWith(TEXT(".onnx")))
            {
                OpenCVDnnNet = cv::dnn::readNetFromONNX(std::string(TCHAR_TO_UTF8(*OpenCvModelPath)));
                bIsOnnxModel = true;
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Unsupported OpenCV model format: %s (only .onnx is supported)"), *OpenCvModelPath);
                return false;
            }

            if (OpenCVDnnNet.empty())
            {
                UE_LOG(LogTemp, Error, TEXT("OpenCV DNN failed to load model: %s"), *OpenCvModelPath);
                return false;
            }

            if (!ConfigureOpenCVDNNBackend(OpenCVDnnNet, bOpenCVDNNPreferCUDA, bOpenCVDNNUseFP16, &OpenCVDNNExecutionProviderLabel))
            {
                return false;
            }
            ActiveDetectionModelPath = OpenCvModelPath;

            const int32 ClampedInputSize = FMath::Clamp(OnnxInputSize, 160, 1280);
            ModelInputWidth = ClampedInputSize;
            ModelInputHeight = ClampedInputSize;
            ResetInferenceOutputState();

            UE_LOG(LogTemp, Log, TEXT("OpenCV DNN model loaded: %s"), *OpenCvModelPath);
            bIsModelLoaded = true;
            return true;
	    }
	    catch (const std::exception& e)
	    {
	        UE_LOG(LogTemp, Error, TEXT("Model load failed: %s"), *FString(e.what()));
	        ReleaseTensorRT();
            ReleaseOnnxRuntime();
            OpenCVDnnNet = cv::dnn::Net();
	        return false;
	    }
	    catch (...)
	    {
	        UE_LOG(LogTemp, Error, TEXT("Model load failed (unknown exception)"));
	        ReleaseTensorRT();
            ReleaseOnnxRuntime();
            OpenCVDnnNet = cv::dnn::Net();
	        return false;
	    }
	}

void UMyActorComponent::ReleaseTensorRT()
{
#if WITH_TENSORRT
    if (TrtStream)
    {
        cudaStreamSynchronize(TrtStream);
        cudaStreamDestroy(TrtStream);
        TrtStream = nullptr;
    }

    if (TrtInputDevice)
    {
        cudaFree(TrtInputDevice);
        TrtInputDevice = nullptr;
    }

    if (TrtOutputDevice)
    {
        cudaFree(TrtOutputDevice);
        TrtOutputDevice = nullptr;
    }

    DestroyTrtObject(TrtContext);
    DestroyTrtObject(TrtEngine);
    DestroyTrtObject(TrtRuntime);
#endif

    ResetInferenceOutputState();
    bIsModelLoaded = false;
}

void UMyActorComponent::ReleaseOnnxRuntime()
{
#if WITH_ONNXRUNTIME
    const bool bWasUsingGpuProvider = bOnnxRuntimeUsingGpuProvider;
    {
        FScopeLock Lock(&OnnxRuntimeRunOptionsMutex);
        OnnxRuntimeRunOptions.Reset();
    }
    OnnxRuntimeSession.Reset();
    OnnxRuntimeSessionOptions.Reset();
    OnnxRuntimeEnv.Reset();
    OnnxRuntimeInputHost.Reset();
    OnnxRuntimeInputShape.Reset();
    OnnxRuntimeInputName.clear();
    OnnxRuntimeOutputNames.Reset();
    EffectiveOnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::CPU;
    bOnnxRuntimeUsingGpuProvider = false;
    if (bWasUsingGpuProvider)
    {
        ClearOnnxRuntimeGpuSessionActive();
    }
#endif
    ResetInferenceOutputState();
    bIsModelLoaded = false;
}

bool UMyActorComponent::RunTensorRT()
{
#if WITH_TENSORRT
    if (!TrtContext || !TrtInputDevice || !TrtOutputDevice || TrtInputElements <= 0 || TrtOutputElements <= 0)
    {
        return false;
    }

    const size_t InputBytes = static_cast<size_t>(TrtInputElements) * sizeof(float);
    const size_t OutputBytes = static_cast<size_t>(TrtOutputElements) * sizeof(float);

    auto LogCudaError = [](const TCHAR* Label, cudaError_t Err)
    {
        if (Err == cudaSuccess)
        {
            return true;
        }
        UE_LOG(LogTemp, Error, TEXT("%s: %s"), Label, *FString(UTF8_TO_TCHAR(cudaGetErrorString(Err))));
        return false;
    };

    if (!LogCudaError(TEXT("cudaMemcpyAsync H2D failed"), cudaMemcpyAsync(TrtInputDevice, TrtInputHost.GetData(), InputBytes, cudaMemcpyHostToDevice, TrtStream)))
    {
        return false;
    }

    if (!TrtContext->setTensorAddress(kTrtInputName, TrtInputDevice) ||
        !TrtContext->setTensorAddress(kTrtOutputName, TrtOutputDevice))
    {
        UE_LOG(LogTemp, Error, TEXT("TensorRT setTensorAddress failed"));
        return false;
    }

    if (!TrtContext->enqueueV3(TrtStream))
    {
        UE_LOG(LogTemp, Error, TEXT("TensorRT enqueueV3 failed"));
        return false;
    }

    if (!LogCudaError(TEXT("cudaMemcpyAsync D2H failed"), cudaMemcpyAsync(TrtOutputHost.GetData(), TrtOutputDevice, OutputBytes, cudaMemcpyDeviceToHost, TrtStream)))
    {
        return false;
    }

    if (!LogCudaError(TEXT("cudaStreamSynchronize failed"), cudaStreamSynchronize(TrtStream)))
    {
        return false;
    }

    return true;
#else
    UE_LOG(LogTemp, Error, TEXT("TensorRT inference requested but this build was compiled without TensorRT."));
    return false;
#endif
}

bool UMyActorComponent::RunTensorRTInference_BG(const cv::Mat& ModelInputBGR)
{
#if WITH_TENSORRT
    cv::Mat imgRGB;
    cv::cvtColor(ModelInputBGR, imgRGB, cv::COLOR_BGR2RGB);

    cv::Mat imgFloat;
    imgRGB.convertTo(imgFloat, CV_32F, 1.0f / 255.0f);

    const int32 PlaneSize = ModelInputWidth * ModelInputHeight;
    if (TrtInputElements != PlaneSize * 3)
    {
        UE_LOG(LogTemp, Error, TEXT("TensorRT input size mismatch (expected %d, got %d)"),
            PlaneSize * 3, TrtInputElements);
        return false;
    }

    if (TrtInputHost.Num() != TrtInputElements)
    {
        TrtInputHost.SetNumUninitialized(TrtInputElements);
    }

    if (!CopyRgbFloatMatToNchw(imgFloat, TrtInputHost.GetData(), ModelInputWidth, ModelInputHeight))
    {
        UE_LOG(LogTemp, Error, TEXT("TensorRT preprocessing produced unexpected image layout: %dx%d type=%d"),
            imgFloat.cols,
            imgFloat.rows,
            imgFloat.type());
        return false;
    }

    if (!RunTensorRT())
    {
        UE_LOG(LogTemp, Error, TEXT("TensorRT inference failed"));
        return false;
    }

    return true;
#else
    UE_LOG(LogTemp, Error, TEXT("TensorRT inference requested but this build was compiled without TensorRT."));
    return false;
#endif
}

bool UMyActorComponent::LoadOnnxRuntime(const FString& ModelPathUE, bool bForceCPUProvider)
{
#if WITH_ONNXRUNTIME
    if (!ModelPathUE.EndsWith(TEXT(".onnx"), ESearchCase::IgnoreCase))
    {
        UE_LOG(LogTemp, Error, TEXT("ONNX Runtime requires an .onnx model file, got: %s"), *ModelPathUE);
        return false;
    }

    try
    {
        OnnxRuntimeEnv = MakeUnique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "EagleEye");
        OnnxRuntimeSessionOptions = MakeUnique<Ort::SessionOptions>();
        auto ConfigureBaseSessionOptions = [this]()
        {
            OnnxRuntimeSessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            OnnxRuntimeSessionOptions->SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
            OnnxRuntimeSessionOptions->DisableMemPattern();
        };
        ConfigureBaseSessionOptions();

        EffectiveOnnxRuntimeExecutionProvider = bForceCPUProvider
            ? EOnnxRuntimeExecutionProvider::CPU
            : ResolveEffectiveOnnxRuntimeProvider();
        const bool bProviderAuto = OnnxRuntimeExecutionProvider == EOnnxRuntimeExecutionProvider::Auto;
        if (!IsOnnxRuntimeProviderCompiled(EffectiveOnnxRuntimeExecutionProvider))
        {
            UE_LOG(LogTemp, Error, TEXT("ONNX Runtime provider requested but not compiled into this build: %s"),
                OnnxProviderToString(EffectiveOnnxRuntimeExecutionProvider));
            ReleaseOnnxRuntime();
            return false;
        }

        FString ProviderRuntimeUnavailableReason;
        if (!IsOnnxRuntimeProviderRuntimeAvailable(EffectiveOnnxRuntimeExecutionProvider, ProviderRuntimeUnavailableReason))
        {
            if (!bProviderAuto)
            {
                UE_LOG(LogTemp, Error, TEXT("ONNX Runtime provider runtime unavailable: %s (%s)"),
                    OnnxProviderToString(EffectiveOnnxRuntimeExecutionProvider),
                    *ProviderRuntimeUnavailableReason);
                ReleaseOnnxRuntime();
                return false;
            }

            UE_LOG(LogTemp, Warning, TEXT("ONNX Runtime provider runtime unavailable: %s (%s). Falling back to CPU."),
                OnnxProviderToString(EffectiveOnnxRuntimeExecutionProvider),
                *ProviderRuntimeUnavailableReason);
            EffectiveOnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::CPU;
        }

        bool bProviderConfigured = false;

#if WITH_ONNXRUNTIME_DML
        if (EffectiveOnnxRuntimeExecutionProvider == EOnnxRuntimeExecutionProvider::DirectML)
        {
            LogDirectMLAdapterCandidates(0, GRHIAdapterName);
            if (OrtStatus* Status = OrtSessionOptionsAppendExecutionProvider_DML(*OnnxRuntimeSessionOptions, 0))
            {
                const FString ErrorMessage = UTF8_TO_TCHAR(Ort::GetApi().GetErrorMessage(Status));
                Ort::GetApi().ReleaseStatus(Status);
                if (!bProviderAuto)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to configure ONNX Runtime DirectML provider: %s"), *ErrorMessage);
                    return false;
                }
                UE_LOG(LogTemp, Warning, TEXT("ONNX Runtime DirectML provider unavailable, falling back to CPU: %s"), *ErrorMessage);
                EffectiveOnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::CPU;
                OnnxRuntimeSessionOptions = MakeUnique<Ort::SessionOptions>();
                ConfigureBaseSessionOptions();
            }
            else
            {
                bProviderConfigured = true;
                bOnnxRuntimeUsingGpuProvider = true;
                UE_LOG(LogTemp, Log, TEXT("ONNX Runtime provider configured: DirectML"));
            }
        }
#endif

#if WITH_ONNXRUNTIME_MIGRAPHX
        if (!bProviderConfigured && EffectiveOnnxRuntimeExecutionProvider == EOnnxRuntimeExecutionProvider::MIGraphX)
        {
            if (OrtStatus* Status = OrtSessionOptionsAppendExecutionProvider_MIGraphX(*OnnxRuntimeSessionOptions, 0))
            {
                const FString ErrorMessage = UTF8_TO_TCHAR(Ort::GetApi().GetErrorMessage(Status));
                Ort::GetApi().ReleaseStatus(Status);
                if (!bProviderAuto)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to configure ONNX Runtime MIGraphX provider: %s"), *ErrorMessage);
                    return false;
                }
                UE_LOG(LogTemp, Warning, TEXT("ONNX Runtime MIGraphX provider unavailable, falling back to CPU: %s"), *ErrorMessage);
                EffectiveOnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::CPU;
                OnnxRuntimeSessionOptions = MakeUnique<Ort::SessionOptions>();
                ConfigureBaseSessionOptions();
            }
            else
            {
                bProviderConfigured = true;
                bOnnxRuntimeUsingGpuProvider = true;
                UE_LOG(LogTemp, Log, TEXT("ONNX Runtime provider configured: MIGraphX"));
            }
        }
#endif

        if (!bProviderConfigured)
        {
            EffectiveOnnxRuntimeExecutionProvider = EOnnxRuntimeExecutionProvider::CPU;
            bOnnxRuntimeUsingGpuProvider = false;
            UE_LOG(LogTemp, Log, TEXT("ONNX Runtime provider configured: CPU"));
        }
        else if (bOnnxRuntimeUsingGpuProvider)
        {
            MarkOnnxRuntimeGpuSessionActive();
        }

#if PLATFORM_WINDOWS
        OnnxRuntimeSession = MakeUnique<Ort::Session>(*OnnxRuntimeEnv, *ModelPathUE, *OnnxRuntimeSessionOptions);
#else
        const std::string ModelPathUtf8 = ToUtf8Path(ModelPathUE);
        OnnxRuntimeSession = MakeUnique<Ort::Session>(*OnnxRuntimeEnv, ModelPathUtf8.c_str(), *OnnxRuntimeSessionOptions);
#endif
        {
            FScopeLock Lock(&OnnxRuntimeRunOptionsMutex);
            OnnxRuntimeRunOptions = MakeUnique<Ort::RunOptions>();
        }

        Ort::AllocatorWithDefaultOptions Allocator;
        if (OnnxRuntimeSession->GetInputCount() <= 0 || OnnxRuntimeSession->GetOutputCount() <= 0)
        {
            UE_LOG(LogTemp, Error, TEXT("ONNX Runtime model has no inputs or outputs: %s"), *ModelPathUE);
            ReleaseOnnxRuntime();
            return false;
        }

        Ort::AllocatedStringPtr InputName = OnnxRuntimeSession->GetInputNameAllocated(0, Allocator);
        OnnxRuntimeInputName = InputName.get();

        OnnxRuntimeOutputNames.Reset();
        const size_t OutputCount = OnnxRuntimeSession->GetOutputCount();
        for (size_t OutputIndex = 0; OutputIndex < OutputCount; ++OutputIndex)
        {
            Ort::AllocatedStringPtr OutputName = OnnxRuntimeSession->GetOutputNameAllocated(OutputIndex, Allocator);
            OnnxRuntimeOutputNames.Add(std::string(OutputName.get()));
            const Ort::TypeInfo OutputTypeInfo = OnnxRuntimeSession->GetOutputTypeInfo(OutputIndex);
            const auto OutputTensorInfo = OutputTypeInfo.GetTensorTypeAndShapeInfo();
            UE_LOG(LogTemp, Log, TEXT("ONNX Runtime output[%d]: name=%s shape=%s type=%d"),
                static_cast<int32>(OutputIndex),
                *FString(OutputName.get()),
                *TensorShapeToString(OutputTensorInfo.GetShape()),
                static_cast<int32>(OutputTensorInfo.GetElementType()));
        }

        const Ort::TypeInfo InputTypeInfo = OnnxRuntimeSession->GetInputTypeInfo(0);
        const auto InputTensorInfo = InputTypeInfo.GetTensorTypeAndShapeInfo();
        std::vector<int64_t> InputShape = InputTensorInfo.GetShape();
        if (InputShape.size() == 4)
        {
            if (InputShape[2] > 0)
            {
                ModelInputHeight = static_cast<int32>(InputShape[2]);
            }
            if (InputShape[3] > 0)
            {
                ModelInputWidth = static_cast<int32>(InputShape[3]);
            }
        }
        else
        {
            const int32 ClampedInputSize = FMath::Clamp(OnnxInputSize, 160, 1280);
            ModelInputWidth = ClampedInputSize;
            ModelInputHeight = ClampedInputSize;
        }

        ModelInputWidth = FMath::Clamp(ModelInputWidth, 160, 1280);
        ModelInputHeight = FMath::Clamp(ModelInputHeight, 160, 1280);
        OnnxRuntimeInputShape.Reset();
        OnnxRuntimeInputShape.Add(1);
        OnnxRuntimeInputShape.Add(3);
        OnnxRuntimeInputShape.Add(ModelInputHeight);
        OnnxRuntimeInputShape.Add(ModelInputWidth);
        TrtInputElements = ModelInputWidth * ModelInputHeight * 3;
        OnnxRuntimeInputHost.SetNumUninitialized(TrtInputElements);
        TrtOutputHost.Reset();
        TrtOutputElements = 0;
        TrtOutputChannels = 0;
        TrtOutputDetections = 0;
        bIsOnnxModel = true;
        bIsModelLoaded = true;
        ActiveDetectionModelPath = ModelPathUE;

        UE_LOG(LogTemp, Log, TEXT("ONNX Runtime model loaded: %s (input=%dx%d, provider=%s%s)"),
            *ModelPathUE,
            ModelInputWidth,
            ModelInputHeight,
            OnnxProviderToString(EffectiveOnnxRuntimeExecutionProvider),
            bOnnxRuntimeUsingGpuProvider ? TEXT(", GPU") : TEXT(", CPU"));
        return true;
    }
    catch (const Ort::Exception& e)
    {
        UE_LOG(LogTemp, Error, TEXT("ONNX Runtime model load failed: %s"), *FString(e.what()));
        ReleaseOnnxRuntime();
        return false;
    }
#else
    UE_LOG(LogTemp, Error, TEXT("ONNX Runtime backend selected but this build was compiled without ONNX Runtime."));
    return false;
#endif
}

bool UMyActorComponent::RunOnnxRuntimeInference_BG(const cv::Mat& ModelInputBGR)
{
#if WITH_ONNXRUNTIME
    if (!OnnxRuntimeSession || OnnxRuntimeInputName.empty() || OnnxRuntimeOutputNames.Num() == 0 || TrtInputElements <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("ONNX Runtime backend selected but session is not initialized"));
        return false;
    }

    cv::Mat imgRGB;
    cv::cvtColor(ModelInputBGR, imgRGB, cv::COLOR_BGR2RGB);

    cv::Mat imgFloat;
    imgRGB.convertTo(imgFloat, CV_32F, 1.0f / 255.0f);

    const int32 PlaneSize = ModelInputWidth * ModelInputHeight;
    if (OnnxRuntimeInputHost.Num() != PlaneSize * 3)
    {
        OnnxRuntimeInputHost.SetNumUninitialized(PlaneSize * 3);
        TrtInputElements = PlaneSize * 3;
    }

    if (!CopyRgbFloatMatToNchw(imgFloat, OnnxRuntimeInputHost.GetData(), ModelInputWidth, ModelInputHeight))
    {
        UE_LOG(LogTemp, Error, TEXT("ONNX Runtime preprocessing produced unexpected image layout: %dx%d type=%d"),
            imgFloat.cols,
            imgFloat.rows,
            imgFloat.type());
        return false;
    }

    try
    {
        Ort::MemoryInfo MemoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value InputTensor = Ort::Value::CreateTensor<float>(
            MemoryInfo,
            OnnxRuntimeInputHost.GetData(),
            OnnxRuntimeInputHost.Num(),
            OnnxRuntimeInputShape.GetData(),
            OnnxRuntimeInputShape.Num());

        const char* InputNames[] = { OnnxRuntimeInputName.c_str() };
        TArray<const char*> OutputNamePtrs;
        OutputNamePtrs.Reserve(OnnxRuntimeOutputNames.Num());
        for (const std::string& OutputName : OnnxRuntimeOutputNames)
        {
            OutputNamePtrs.Add(OutputName.c_str());
        }

        Ort::RunOptions* RunOptions = nullptr;
        {
            FScopeLock Lock(&OnnxRuntimeRunOptionsMutex);
            if (bInferenceShutdownRequested.load() || bIsEndingPlay.load())
            {
                return false;
            }
            if (!OnnxRuntimeRunOptions)
            {
                OnnxRuntimeRunOptions = MakeUnique<Ort::RunOptions>();
            }
            OnnxRuntimeRunOptions->UnsetTerminate();
            RunOptions = OnnxRuntimeRunOptions.Get();
        }

        std::vector<Ort::Value> Outputs = OnnxRuntimeSession->Run(
            *RunOptions,
            InputNames,
            &InputTensor,
            1,
            OutputNamePtrs.GetData(),
            OutputNamePtrs.Num());

        Ort::Value* ChosenOutput = nullptr;
        int64 BestElementCount = -1;
        int32 BestOutputScore = TNumericLimits<int32>::Min();
        std::vector<int64_t> ChosenOutputShape;
        const int32 NumClassesHint = static_cast<int32>(ClassNames.size());
        for (Ort::Value& Output : Outputs)
        {
            if (!Output.IsTensor())
            {
                continue;
            }

            Ort::TensorTypeAndShapeInfo TensorInfo = Output.GetTensorTypeAndShapeInfo();
            if (TensorInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            {
                continue;
            }

            const int64 ElementCount = static_cast<int64>(TensorInfo.GetElementCount());
            const std::vector<int64_t> Shape = TensorInfo.GetShape();
            TArray<int32> NonSingletonDims;
            for (int64_t Dim : Shape)
            {
                if (Dim > 1 && Dim <= TNumericLimits<int32>::Max())
                {
                    NonSingletonDims.Add(static_cast<int32>(Dim));
                }
            }

            int32 OutputScore = TNumericLimits<int32>::Min() / 2;
            if (NonSingletonDims.Num() == 2)
            {
                int32 Attrs = NonSingletonDims[NonSingletonDims.Num() - 2];
                int32 Dets = NonSingletonDims[NonSingletonDims.Num() - 1];
                if (Dets <= 256 && Attrs > Dets)
                {
                    Swap(Attrs, Dets);
                }

                if (Attrs >= 5 && Attrs <= 512 && Dets >= 16)
                {
                    OutputScore = 1000000 + FMath::Min(Dets, 100000);
                    if (Attrs == 6)
                    {
                        OutputScore += 500000;
                    }
                    if (NumClassesHint > 0 && (Attrs == (4 + NumClassesHint) || Attrs == (5 + NumClassesHint)))
                    {
                        OutputScore += 400000;
                    }
                    if (Attrs >= 64 && Attrs <= 128)
                    {
                        OutputScore += 100000;
                    }
                }
            }

            if (OutputScore > BestOutputScore ||
                (OutputScore == BestOutputScore && ElementCount > BestElementCount))
            {
                BestOutputScore = OutputScore;
                BestElementCount = ElementCount;
                ChosenOutput = &Output;
                ChosenOutputShape = Shape;
            }
        }

        if (!ChosenOutput || BestElementCount <= 0 || BestOutputScore < 0)
        {
            UE_LOG(LogTemp, Error, TEXT("ONNX Runtime produced no detection-like float tensor output"));
            return false;
        }

        constexpr int64 MaxSupportedOutputElements = 32ll * 1024ll * 1024ll;
        if (BestElementCount > TNumericLimits<int32>::Max() || BestElementCount > MaxSupportedOutputElements)
        {
            UE_LOG(LogTemp, Error, TEXT("ONNX Runtime output too large: %lld elements"), static_cast<long long>(BestElementCount));
            return false;
        }

        const int32 OutputElementCount = static_cast<int32>(BestElementCount);
        TrtOutputHost.SetNumUninitialized(OutputElementCount);
        if (TrtOutputHost.Num() != OutputElementCount)
        {
            UE_LOG(LogTemp, Error, TEXT("ONNX Runtime output buffer allocation failed: requested=%d actual=%d"),
                OutputElementCount,
                TrtOutputHost.Num());
            return false;
        }
        FMemory::Memcpy(
            TrtOutputHost.GetData(),
            ChosenOutput->GetTensorMutableData<float>(),
            static_cast<size_t>(BestElementCount) * sizeof(float));
        TrtOutputElements = OutputElementCount;

        TArray<int32> NonSingletonDims;
        for (int64_t Dim : ChosenOutputShape)
        {
            if (Dim > 1 && Dim <= TNumericLimits<int32>::Max())
            {
                NonSingletonDims.Add(static_cast<int32>(Dim));
            }
        }

        if (NonSingletonDims.Num() == 2)
        {
            TrtOutputChannels = NonSingletonDims[NonSingletonDims.Num() - 2];
            TrtOutputDetections = NonSingletonDims[NonSingletonDims.Num() - 1];
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Unsupported ONNX Runtime detection output shape: %s"), *TensorShapeToString(ChosenOutputShape));
            return false;
        }

        static int32 OnnxOutputShapeLogDecimator = 0;
        if ((OnnxOutputShapeLogDecimator++ % 120) == 0)
        {
            UE_LOG(LogTemp, Log, TEXT("ONNX Runtime selected output: shape=%s resolved=[%d,%d] elements=%d score=%d"),
                *TensorShapeToString(ChosenOutputShape),
                TrtOutputChannels,
                TrtOutputDetections,
                TrtOutputElements,
                BestOutputScore);
        }

        return true;
    }
    catch (const Ort::Exception& e)
    {
        if (bInferenceShutdownRequested.load() || bIsEndingPlay.load())
        {
            UE_LOG(LogTemp, Verbose, TEXT("ONNX Runtime inference stopped during shutdown: %s"), *FString(e.what()));
            return false;
        }
        UE_LOG(LogTemp, Error, TEXT("ONNX Runtime inference failed: %s"), *FString(e.what()));
        return false;
    }
#else
    UE_LOG(LogTemp, Error, TEXT("ONNX Runtime inference requested but this build was compiled without ONNX Runtime."));
    return false;
#endif
}

bool UMyActorComponent::RunOpenCVDNNInference_BG(const cv::Mat& ModelInputBGR)
{
    if (EffectiveInferenceBackend != EDetectionInferenceBackend::OpenCVDNN)
    {
        UE_LOG(LogTemp, Error, TEXT("RunOpenCVDNNInference_BG called while backend is not OpenCVDNN"));
        return false;
    }

    if (OpenCVDnnNet.empty())
    {
        UE_LOG(LogTemp, Error, TEXT("OpenCV DNN backend selected but net is not initialized"));
        return false;
    }

    cv::Mat Blob = cv::dnn::blobFromImage(
        ModelInputBGR,
        1.0 / 255.0,
        cv::Size(ModelInputWidth, ModelInputHeight),
        cv::Scalar(),
        true,
        false);

    cv::Mat ChosenOut;
    auto ForwardOnce = [&]() -> void
    {
        OpenCVDnnNet.setInput(Blob);
        ChosenOut = OpenCVDnnNet.forward();
    };

    try
    {
        ForwardOnce();
    }
    catch (const cv::Exception& e)
    {
        const FString Err = FString(e.what());
        const bool bBackendTargetMismatch =
            Err.Contains(TEXT("validateBackendAndTarget")) ||
            Err.Contains(TEXT("preferableBackend"));

        if (!bBackendTargetMismatch)
        {
            UE_LOG(LogTemp, Error, TEXT("OpenCV DNN forward failed: %s"), *Err);
            return false;
        }

        UE_LOG(LogTemp, Warning, TEXT("OpenCV DNN backend/target mismatch (%s). Falling back to CPU."),
            *Err);
        try
        {
            ConfigureOpenCVDNNForCPU(OpenCVDnnNet);
            ForwardOnce();
        }
        catch (const cv::Exception& RetryErr)
        {
            UE_LOG(LogTemp, Error, TEXT("OpenCV DNN CPU fallback failed: %s"), *FString(RetryErr.what()));
            return false;
        }
    }

    if (ChosenOut.empty())
    {
        UE_LOG(LogTemp, Error, TEXT("OpenCV DNN produced no outputs"));
        return false;
    }

    if (!ChosenOut.isContinuous())
    {
        ChosenOut = ChosenOut.clone();
    }
    if (ChosenOut.type() != CV_32F)
    {
        ChosenOut.convertTo(ChosenOut, CV_32F);
    }

    int32 OutputChannels = 0;
    int32 OutputDetections = 0;
    if (ChosenOut.dims == 2)
    {
        OutputChannels = ChosenOut.rows;
        OutputDetections = ChosenOut.cols;
    }
    else if (ChosenOut.dims == 3 && ChosenOut.size[0] == 1)
    {
        OutputChannels = ChosenOut.size[1];
        OutputDetections = ChosenOut.size[2];
    }
    else
    {
        TArray<int32> NonSingletonDims;
        for (int32 d = 0; d < ChosenOut.dims; ++d)
        {
            if (ChosenOut.size[d] > 1)
            {
                NonSingletonDims.Add(ChosenOut.size[d]);
            }
        }
        if (NonSingletonDims.Num() != 2)
        {
            UE_LOG(LogTemp, Error, TEXT("Unsupported OpenCV output dims: %d"), ChosenOut.dims);
            return false;
        }
        OutputChannels = NonSingletonDims[0];
        OutputDetections = NonSingletonDims[1];
    }

    const int32 TotalValues = OutputChannels * OutputDetections;
    if (TotalValues <= 0 || ChosenOut.total() < static_cast<size_t>(TotalValues))
    {
        UE_LOG(LogTemp, Error, TEXT("OpenCV output size mismatch: channels=%d, dets=%d, total=%lld"),
            OutputChannels, OutputDetections, static_cast<long long>(ChosenOut.total()));
        return false;
    }

    TrtOutputHost.SetNumUninitialized(TotalValues);
    FMemory::Memcpy(TrtOutputHost.GetData(), ChosenOut.ptr<float>(), static_cast<size_t>(TotalValues) * sizeof(float));
    TrtOutputElements = TotalValues;
    TrtOutputChannels = OutputChannels;
    TrtOutputDetections = OutputDetections;

    if (bLogPerf)
    {
        UE_LOG(LogTemp, Log, TEXT("OpenCV output shape: raw=%s resolved=[%d, %d] total=%d"),
            *MatShapeToString(ChosenOut), TrtOutputChannels, TrtOutputDetections, TrtOutputElements);
    }

    return true;
}

TArray<FDetectionResult> UMyActorComponent::ProcessWithOpenCV_BG(const TArray<FColor>& Bitmap, int32 Width, int32 Height, double* OutInferenceMs)
{
    if (OutInferenceMs)
    {
        *OutInferenceMs = -1.0;
    }

    if (!bIsModelLoaded)
    {
        return {};
    }

    if (Width <= 0 || Height <= 0 || Bitmap.Num() != Width * Height || !Bitmap.GetData())
    {
        UE_LOG(LogTemp, Warning, TEXT("Skipping inference for invalid bitmap: size=%dx%d pixels=%d"),
            Width,
            Height,
            Bitmap.Num());
        return {};
    }

    // Wrap UE's pixel buffer directly (BGRA8) to avoid per-pixel copies.
    cv::Mat ImageBGRA(Height, Width, CV_8UC4, const_cast<FColor*>(Bitmap.GetData()));
    cv::Mat imgBGR;
    cv::cvtColor(ImageBGRA, imgBGR, cv::COLOR_BGRA2BGR);

    cv::Mat modelInputBGR;
    float InputScale = 1.0f;
    float PadX = 0.0f;
    float PadY = 0.0f;
    if (bUseLetterbox)
    {
        const float RatioW = static_cast<float>(ModelInputWidth) / static_cast<float>(Width);
        const float RatioH = static_cast<float>(ModelInputHeight) / static_cast<float>(Height);
        InputScale = FMath::Min(RatioW, RatioH);

        const int32 NewW = FMath::Clamp(FMath::RoundToInt(static_cast<float>(Width) * InputScale), 1, ModelInputWidth);
        const int32 NewH = FMath::Clamp(FMath::RoundToInt(static_cast<float>(Height) * InputScale), 1, ModelInputHeight);

        cv::Mat resized;
        cv::resize(imgBGR, resized, cv::Size(NewW, NewH), 0.0, 0.0, cv::INTER_LINEAR);

        const int32 PadLeft = (ModelInputWidth - NewW) / 2;
        const int32 PadTop = (ModelInputHeight - NewH) / 2;
        const uint8 PadValue = static_cast<uint8>(FMath::Clamp(LetterboxValue, 0, 255));

        modelInputBGR = cv::Mat(ModelInputHeight, ModelInputWidth, CV_8UC3, cv::Scalar(PadValue, PadValue, PadValue));
        resized.copyTo(modelInputBGR(cv::Rect(PadLeft, PadTop, NewW, NewH)));

        PadX = static_cast<float>(PadLeft);
        PadY = static_cast<float>(PadTop);
    }
    else
    {
        cv::resize(imgBGR, modelInputBGR, cv::Size(ModelInputWidth, ModelInputHeight), 0.0, 0.0, cv::INTER_LINEAR);
    }

    const double T0 = FPlatformTime::Seconds();
    bool bInferOk = false;
    if (EffectiveInferenceBackend == EDetectionInferenceBackend::TensorRT)
    {
        bInferOk = RunTensorRTInference_BG(modelInputBGR);
    }
    else if (EffectiveInferenceBackend == EDetectionInferenceBackend::ONNXRuntime)
    {
        bInferOk = RunOnnxRuntimeInference_BG(modelInputBGR);
    }
    else if (EffectiveInferenceBackend == EDetectionInferenceBackend::OpenCVDNN)
    {
        bInferOk = RunOpenCVDNNInference_BG(modelInputBGR);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Unknown inference backend value: %d"), static_cast<int32>(EffectiveInferenceBackend));
        return {};
    }

    if (!bInferOk)
    {
        return {};
    }

    const double InferMs = (FPlatformTime::Seconds() - T0) * 1000.0;
    if (OutInferenceMs)
    {
        *OutInferenceMs = InferMs;
    }
    if (bLogPerf)
    {
        UE_LOG(LogTemp, Log, TEXT("%s forward: %.1f ms"), BackendToString(EffectiveInferenceBackend), InferMs);
    }

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    const float ConfThreshold = FMath::Clamp(ConfidenceThreshold, 0.01f, 0.99f);
    const float IoUThreshold = FMath::Clamp(NmsThreshold, 0.01f, 0.99f);
    const float* OutData = TrtOutputHost.GetData();

    auto CommitMappedBox = [&](const cv::Rect& Box, float Score, int32 ClassId)
    {
        boxes.push_back(Box);
        class_ids.push_back(ClassId);
        confidences.push_back(Score);
    };

    auto MapXYXYToSource = [&](float x1, float y1, float x2, float y2, bool bUndoLetterbox, cv::Rect& OutBox, float& OutOverflow) -> bool
    {
        OutOverflow = 0.0f;
        const float MaxAbsCoord = FMath::Max(
            FMath::Max(FMath::Abs(x1), FMath::Abs(y1)),
            FMath::Max(FMath::Abs(x2), FMath::Abs(y2)));
        if (MaxAbsCoord <= 2.0f)
        {
            x1 *= static_cast<float>(ModelInputWidth);
            x2 *= static_cast<float>(ModelInputWidth);
            y1 *= static_cast<float>(ModelInputHeight);
            y2 *= static_cast<float>(ModelInputHeight);
        }

        const bool bApplyUndo = bUndoLetterbox && bUseLetterbox && (InputScale > KINDA_SMALL_NUMBER);
        float sx1 = x1;
        float sy1 = y1;
        float sx2 = x2;
        float sy2 = y2;
        if (bApplyUndo)
        {
            sx1 = (x1 - PadX) / InputScale;
            sy1 = (y1 - PadY) / InputScale;
            sx2 = (x2 - PadX) / InputScale;
            sy2 = (y2 - PadY) / InputScale;
        }
        else
        {
            sx1 = x1 * static_cast<float>(Width) / static_cast<float>(ModelInputWidth);
            sy1 = y1 * static_cast<float>(Height) / static_cast<float>(ModelInputHeight);
            sx2 = x2 * static_cast<float>(Width) / static_cast<float>(ModelInputWidth);
            sy2 = y2 * static_cast<float>(Height) / static_cast<float>(ModelInputHeight);
        }

        const float MinX = 0.0f;
        const float MaxX = static_cast<float>(Width - 1);
        const float MinY = 0.0f;
        const float MaxY = static_cast<float>(Height - 1);
        const float RawL = FMath::Min(sx1, sx2);
        const float RawT = FMath::Min(sy1, sy2);
        const float RawR = FMath::Max(sx1, sx2);
        const float RawB = FMath::Max(sy1, sy2);

        auto ClampWithOverflow = [&](float V, float MinV, float MaxV) -> float
        {
            if (V < MinV)
            {
                OutOverflow += (MinV - V);
                return MinV;
            }
            if (V > MaxV)
            {
                OutOverflow += (V - MaxV);
                return MaxV;
            }
            return V;
        };

        const float Lf = ClampWithOverflow(RawL, MinX, MaxX);
        const float Tf = ClampWithOverflow(RawT, MinY, MaxY);
        const float Rf = ClampWithOverflow(RawR, MinX, MaxX);
        const float Bf = ClampWithOverflow(RawB, MinY, MaxY);

        const int32 Left = FMath::FloorToInt(Lf);
        const int32 Top = FMath::FloorToInt(Tf);
        const int32 Right = FMath::CeilToInt(Rf);
        const int32 Bottom = FMath::CeilToInt(Bf);
        const int32 BoxW = Right - Left;
        const int32 BoxH = Bottom - Top;
        if (BoxW <= 1 || BoxH <= 1)
        {
            return false;
        }

        OutBox = cv::Rect(Left, Top, BoxW, BoxH);
        return true;
    };

    if (!bIsOnnxModel || !OutData || TrtOutputChannels <= 0 || TrtOutputDetections <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Unexpected model output state, skipping detections"));
    }
    else
    {
        // Normalize output layout to [attrs, dets].
        int32 Attrs = TrtOutputChannels;
        int32 Dets = TrtOutputDetections;
        bool bAttrsMajor = true;
        if (TrtOutputDetections <= 256 && TrtOutputChannels > TrtOutputDetections)
        {
            Attrs = TrtOutputDetections;
            Dets = TrtOutputChannels;
            bAttrsMajor = false;
        }

        auto ReadAttr = [&](int32 Attr, int32 Det) -> float
        {
            return bAttrsMajor ? OutData[(Attr * Dets) + Det] : OutData[(Det * Attrs) + Attr];
        };

        const int32 NumClassesHint = static_cast<int32>(ClassNames.size());

        if (Attrs == 6)
        {
            // End-to-end layouts: [x1, y1, x2, y2, score, class_id] or
            // [x1, y1, x2, y2, class_id, score], depending on exporter/backend.
            struct FSixColumnLayoutStats
            {
                int32 ScoreAttr = 4;
                int32 ClassAttr = 5;
                int32 ClassLikeRows = 0;
                int32 ScoreLikeRows = 0;
                int32 ScoreRows = 0;
                int32 AboveThresholdRows = 0;
                int32 MappedRows = 0;
                float MinScore = FLT_MAX;
                float MaxScore = -FLT_MAX;
                double ScoreSum = 0.0;
            };

            auto IsScoreLike = [](float Value) -> bool
            {
                return FMath::IsFinite(Value) && Value >= 0.0f && Value <= 1.0f;
            };

            auto EvaluateSixColumnLayout = [&](int32 ScoreAttr, int32 ClassAttr) -> FSixColumnLayoutStats
            {
                FSixColumnLayoutStats Stats;
                Stats.ScoreAttr = ScoreAttr;
                Stats.ClassAttr = ClassAttr;
                for (int32 i = 0; i < Dets; ++i)
                {
                    const float x1 = ReadAttr(0, i);
                    const float y1 = ReadAttr(1, i);
                    const float x2 = ReadAttr(2, i);
                    const float y2 = ReadAttr(3, i);
                    const float RawScore = ReadAttr(ScoreAttr, i);
                    const float RawClass = ReadAttr(ClassAttr, i);
                    if (!FMath::IsFinite(x1) || !FMath::IsFinite(y1) || !FMath::IsFinite(x2) || !FMath::IsFinite(y2))
                    {
                        continue;
                    }

                    if (IsClassIdLike(RawClass, NumClassesHint))
                    {
                        ++Stats.ClassLikeRows;
                    }
                    if (IsScoreLike(RawScore))
                    {
                        ++Stats.ScoreLikeRows;
                    }

                    const float Score = NormalizeConfidence(RawScore);
                    if (Score < 0.0f)
                    {
                        continue;
                    }

                    ++Stats.ScoreRows;
                    Stats.MinScore = FMath::Min(Stats.MinScore, Score);
                    Stats.MaxScore = FMath::Max(Stats.MaxScore, Score);
                    Stats.ScoreSum += Score;

                    if (Score < ConfThreshold || !IsClassIdLike(RawClass, NumClassesHint))
                    {
                        continue;
                    }
                    ++Stats.AboveThresholdRows;

                    cv::Rect ProbeBox;
                    float ProbeOverflow = 0.0f;
                    if (MapXYXYToSource(x1, y1, x2, y2, bUseLetterbox, ProbeBox, ProbeOverflow))
                    {
                        ++Stats.MappedRows;
                    }
                }
                return Stats;
            };

            const FSixColumnLayoutStats StandardLayout = EvaluateSixColumnLayout(4, 5);
            const bool bUseOpenCvAdaptiveSixColumnLayout =
                EffectiveInferenceBackend == EDetectionInferenceBackend::OpenCVDNN;
            const FSixColumnLayoutStats SwappedLayout =
                bUseOpenCvAdaptiveSixColumnLayout ? EvaluateSixColumnLayout(5, 4) : FSixColumnLayoutStats{};
            const FSixColumnLayoutStats& ChosenLayout =
                (bUseOpenCvAdaptiveSixColumnLayout && SwappedLayout.MappedRows > StandardLayout.MappedRows)
                    ? SwappedLayout
                    : StandardLayout;

            if (bUseOpenCvAdaptiveSixColumnLayout)
            {
                static int32 OpenCvSixColumnLayoutLogDecimator = 0;
                if ((OpenCvSixColumnLayoutLogDecimator++ % 60) == 0)
                {
                    auto AvgScore = [](const FSixColumnLayoutStats& Stats) -> float
                    {
                        return Stats.ScoreRows > 0
                            ? static_cast<float>(Stats.ScoreSum / static_cast<double>(Stats.ScoreRows))
                            : 0.0f;
                    };

                    UE_LOG(LogTemp, Warning,
                        TEXT("OpenCVDNN 6-col layout stats: chosen=[score=%d,class=%d] standard(mapped=%d above=%d classLike=%d scoreLike=%d score=[%.3f,%.3f,%.3f]) swapped(mapped=%d above=%d classLike=%d scoreLike=%d score=[%.3f,%.3f,%.3f]) dets=%d conf=%.2f"),
                        ChosenLayout.ScoreAttr,
                        ChosenLayout.ClassAttr,
                        StandardLayout.MappedRows,
                        StandardLayout.AboveThresholdRows,
                        StandardLayout.ClassLikeRows,
                        StandardLayout.ScoreLikeRows,
                        StandardLayout.MinScore == FLT_MAX ? 0.0f : StandardLayout.MinScore,
                        AvgScore(StandardLayout),
                        StandardLayout.MaxScore == -FLT_MAX ? 0.0f : StandardLayout.MaxScore,
                        SwappedLayout.MappedRows,
                        SwappedLayout.AboveThresholdRows,
                        SwappedLayout.ClassLikeRows,
                        SwappedLayout.ScoreLikeRows,
                        SwappedLayout.MinScore == FLT_MAX ? 0.0f : SwappedLayout.MinScore,
                        AvgScore(SwappedLayout),
                        SwappedLayout.MaxScore == -FLT_MAX ? 0.0f : SwappedLayout.MaxScore,
                        Dets,
                        ConfThreshold);
                }
            }

            for (int32 i = 0; i < Dets; ++i)
            {
                const float x1 = ReadAttr(0, i);
                const float y1 = ReadAttr(1, i);
                const float x2 = ReadAttr(2, i);
                const float y2 = ReadAttr(3, i);
                const float RawScore = ReadAttr(ChosenLayout.ScoreAttr, i);
                const float RawClass = ReadAttr(ChosenLayout.ClassAttr, i);
                if (!FMath::IsFinite(RawScore) || !FMath::IsFinite(RawClass) ||
                    !IsClassIdLike(RawClass, NumClassesHint))
                {
                    continue;
                }

                const float score = NormalizeConfidence(RawScore);
                if (score < ConfThreshold)
                {
                    continue;
                }

                int32 classId = FMath::RoundToInt(RawClass);
                if (NumClassesHint > 0)
                {
                    classId = FMath::Clamp(classId, 0, NumClassesHint - 1);
                }
                else
                {
                    classId = FMath::Max(0, classId);
                }

                cv::Rect MappedBox;
                float Overflow = 0.0f;
                if (MapXYXYToSource(x1, y1, x2, y2, bUseLetterbox, MappedBox, Overflow))
                {
                    CommitMappedBox(MappedBox, score, classId);
                }
            }
        }
        else if (Attrs >= 5)
        {
            // Raw layout: [cx, cy, w, h, ...classes] or [cx, cy, w, h, obj, ...classes].
            const bool bHasObjectness = (NumClassesHint > 0 && (Attrs - 5) == NumClassesHint);
            const int32 ClassStart = bHasObjectness ? 5 : 4;
            const int32 NumClasses = Attrs - ClassStart;
            if (NumClasses <= 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("Unsupported raw output shape [%d, %d]"), Attrs, Dets);
            }
            else
            {
                for (int32 i = 0; i < Dets; ++i)
                {
                    const float cx = ReadAttr(0, i);
                    const float cy = ReadAttr(1, i);
                    const float w = ReadAttr(2, i);
                    const float h = ReadAttr(3, i);
                    float obj = bHasObjectness ? ReadAttr(4, i) : 1.0f;
                    if (bHasObjectness && (obj < 0.0f || obj > 1.0f))
                    {
                        obj = Sigmoidf(obj);
                    }

                    float bestScore = 0.0f;
                    int32 bestClass = -1;
                    for (int32 c = 0; c < NumClasses; ++c)
                    {
                        float cls = ReadAttr(ClassStart + c, i);
                        if (cls < 0.0f || cls > 1.0f)
                        {
                            cls = Sigmoidf(cls);
                        }
                        const float score = obj * cls;
                        if (score > bestScore)
                        {
                            bestScore = score;
                            bestClass = c;
                        }
                    }

                    if (bestClass < 0 || bestScore < ConfThreshold)
                    {
                        continue;
                    }

                    const float x1 = cx - (w * 0.5f);
                    const float y1 = cy - (h * 0.5f);
                    const float x2 = cx + (w * 0.5f);
                    const float y2 = cy + (h * 0.5f);
                    cv::Rect MappedBox;
                    float Overflow = 0.0f;
                    if (MapXYXYToSource(x1, y1, x2, y2, bUseLetterbox, MappedBox, Overflow))
                    {
                        CommitMappedBox(MappedBox, bestScore, bestClass);
                    }
                }
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Unsupported output shape [%d, %d]"), Attrs, Dets);
        }
    }

    std::vector<int> indices;
    ApplyNms(boxes, confidences, ConfThreshold, IoUThreshold, indices);

    if (EffectiveInferenceBackend == EDetectionInferenceBackend::OpenCVDNN && indices.empty())
    {
        static int32 OpenCvEmptyLogDecimator = 0;
        if ((OpenCvEmptyLogDecimator++ % 30) == 0)
        {
            float MinV = 0.0f;
            float MaxV = 0.0f;
            if (OutData && TrtOutputElements > 0)
            {
                MinV = OutData[0];
                MaxV = OutData[0];
                const int32 ProbeCount = FMath::Min(TrtOutputElements, 8192);
                for (int32 i = 1; i < ProbeCount; ++i)
                {
                    MinV = FMath::Min(MinV, OutData[i]);
                    MaxV = FMath::Max(MaxV, OutData[i]);
                }
            }

            UE_LOG(LogTemp, Warning, TEXT("OpenCVDNN produced 0 final boxes (raw=%d, shape=[%d,%d], value_range=[%.4f, %.4f], conf=%.2f, nms=%.2f)"),
                static_cast<int32>(boxes.size()), TrtOutputChannels, TrtOutputDetections, MinV, MaxV, ConfThreshold, IoUThreshold);
        }
    }

    TArray<FDetectionResult> Out;
    for (int idx : indices) {
        cv::Rect box = boxes[idx];
        TArray<FVector2D> corners;
        RectToCorners(box, corners);

        FString label = FString::Printf(
            TEXT("%s: %.2f"),
            (class_ids[idx] >= 0 && static_cast<size_t>(class_ids[idx]) < ClassNames.size())
                ? *FString(ClassNames[class_ids[idx]].c_str())
                : TEXT("Unknown"),
            confidences[idx]
        );

        FDetectionResult det;
        det.Corners = corners;
        det.Label = label;
        det.Confidence = confidences[idx];
        det.ClassId = class_ids[idx];
        Out.Add(det);
    }
    return Out;
}

// void UMyActorComponent::TestOpenCV(){
//     UE_LOG(LogTemp, Log, TEXT("Testing OpenCV..."));
//     int testDim[3] = {2, 3, 4};
//     cv::Mat testMat(3, testDim, CV_32FC1);
//     UE_LOG(
//         LogTemp, Log,
//         TEXT("dimension = %d, %d, %d"),
//         testMat.size[0], testMat.size[1], testMat.size[2]);
//     UE_LOG(LogTemp, Log, TEXT("Testing Done!"));
// }

// // Called when the game starts
// void UMyActorComponent::BeginPlay()
// {
// 	Super::BeginPlay();
//     TestOpenCV();
//     InitializeSceneCapture();
// 	// ...
// 	GetWorld()->GetTimerManager().SetTimer(
//         TimerHandle_Capture,                    // FTimerHandle (store in your .h)
//         this,
//         &UMyActorComponent::TickCapture,        // function to call
//         0.2f,                                   // interval (seconds)
//         true,                                   // looping
//         0.5f                                    // initial delay (seconds)
//     );
// }

// void UMyActorComponent::TickCapture()
// {
//     CaptureAndProcess(50); // example threshold
// }

// void UMyActorComponent::InitializeSceneCapture()
// {
//     // AActor* OwnerActor = GetOwner();

//     // FVector2D ViewportSize = FVector2D(1, 1);
//     // GEngine->GameViewport->GetViewportSize(ViewportSize);

//     // UCameraComponent* Camera = OwnerActor->FindComponentByClass<UCameraComponent>();

//     // // Create SceneCaptureComponent2D
//     // SceneCaptureComponent = NewObject<USceneCaptureComponent2D>(this);
//     // SceneCaptureComponent->SetupAttachment(Camera);
//     // // UE_LOG(LogTemp, Log, TEXT(GetOwner()->GetParentComponent()));
//     // SceneCaptureComponent->RegisterComponent();

//     // // Create RenderTarget
//     // RenderTarget = NewObject<UTextureRenderTarget2D>();
//     // RenderTarget->InitAutoFormat(ViewportSize.X, ViewportSize.Y); // Set desired resolution
//     // RenderTarget->UpdateResource();

//     // OverlayText = NewObject<UTexture2D>();
//     // // OverlayText->InitAutoFormat(ViewportSize.X, ViewportSize.Y); // Set desired resolution
//     // OverlayText->UpdateResource();

//     // // Configure SceneCaptureComponent
//     // SceneCaptureComponent->TextureTarget = RenderTarget;
//     // SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
//     // UE_LOG(LogTemp, Log, TEXT("Init Complete!"));
//     // FString VectorString;
//     // // UE_LOG(LogTemp, Warning, TEXT("%s"), );
//     // for (size_t i = 0; i < class_names.size(); i++) {
//     //     VectorString += FString(class_names[i].c_str()) + TEXT(" ");
//     // }
//     // UE_LOG(LogTemp, Warning, TEXT("%s"), *VectorString);

    
// }

// void UMyActorComponent::CaptureAndProcess(int threshold)
// {
//     // if (!RenderTarget)
//     // {
//     //     UE_LOG(LogTemp, Warning, TEXT("RenderTarget not initialized."));
//     //     return;
//     // }

//     // // Capture the current frame
//     // FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
//     // if (!RenderTargetResource)
//     // {
//     //     UE_LOG(LogTemp, Warning, TEXT("RenderTargetResource not available."));
//     //     return;
//     // }

//     // TArray<FColor> Bitmap;
//     // FReadSurfaceDataFlags ReadPixelFlags(RCM_UNorm);
//     // ReadPixelFlags.SetLinearToGamma(false);

//     // // Read the pixels into the Bitmap array
//     // if (RenderTargetResource->ReadPixels(Bitmap))
//     // {
//     //     int32 Width = RenderTarget->SizeX;
//     //     int32 Height = RenderTarget->SizeY;

//     //     ProcessWithOpenCV(Bitmap, Width, Height, threshold, yolo_net);

//     //     // OverlayText = FOpenCVHelper::TextureFromCvMat(Img);
//     // }
//     // else
//     // {
//     //     UE_LOG(LogTemp, Warning, TEXT("Failed to read pixels from RenderTarget."));
//     // }
//     if (!GEngine || !GEngine->GameViewport) return;

//     FViewport* Viewport = GEngine->GameViewport->Viewport;
//     if (!Viewport) return;

//     TArray<FColor> Bitmap;
//     FIntPoint Size = Viewport->GetSizeXY();

//     if (Size.X <= 0 || Size.Y <= 0) return;

//     if (!Viewport->ReadPixels(Bitmap)) return;

//     if (Bitmap.Num() != Size.X * Size.Y) return;

//     ProcessWithOpenCV(Bitmap, Size.X, Size.Y, threshold, yolo_net);
// }

	// TArray<FDetectionResult> UMyActorComponent::ProcessWithOpenCV(const TArray<FColor>& Bitmap, int32 Width, int32 Height, int threshold, cv::dnn::Net& net) {
//     cv::Mat Image(Height, Width, CV_8UC4);
//     TArray<FDetectionResult> results;

//     for (int32 y = 0; y < Height; y++)
//     {
//         for (int32 x = 0; x < Width; x++)
//         {
//             const FColor& Pixel = Bitmap[y * Width + x];
//             Image.at<cv::Vec4b>(y, x) = cv::Vec4b(Pixel.B, Pixel.G, Pixel.R, Pixel.A); // BGRA format
//         }
//     }

//     cv::Mat img;
//     cv::Mat img2;
//     cv::cvtColor(Image, img, cv::COLOR_BGRA2BGR);
//     img2 = cv::dnn::blobFromImage(img, 1/255.f, cv::Size(608, 608), cv::Scalar(), true, false);
//     net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
//     net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);

//     net.setInput(img2);

//     std::vector<std::string> names;
//     if(names.empty()){
//         std::vector<int32_t> out_layers = net.getUnconnectedOutLayers();
//         std::vector<std::string> layers_names = net.getLayerNames();
//         names.resize( out_layers.size() );
//         for( size_t i = 0; i < out_layers.size(); ++i ){
//             names[i] = layers_names[out_layers[i] - 1];
//         }
//     }

//     std::vector<cv::Mat> outputs;

//     try
//     {
//         net.forward(outputs, names);
//     }
//     catch(const std::exception& e)
//     {
//         FString str(e.what());
//         UE_LOG(LogTemp, Error, TEXT("%s"), *str);
//     }
    

//     std::vector<cv::Rect> boxes;
//     std::vector<float> confidences;
//     std::vector<int> class_ids;


//     cv::Mat output = outputs[0];

//     for ( cv::Mat& det : outputs ) {
//         for( int32_t i = 0; i < det.rows; i++) {
//             cv::Mat region = det.row( i );
//             cv::Mat scores = region.colRange( 5, det.cols );
//             cv::Point class_id;
//             double confidence;
//             cv::minMaxLoc( scores, 0, &confidence, 0, &class_id );
//             constexpr float thr = 0.4;
//             if( thr > confidence){
//                 continue;
//             }
//             const int32_t x_center = static_cast<int32_t>( region.at<float>( 0 ) * Width);
//             const int32_t y_center = static_cast<int32_t>( region.at<float>( 1 ) * Height );
//             const int32_t width    = static_cast<int32_t>( region.at<float>( 2 ) * Width );
//             const int32_t height   = static_cast<int32_t>( region.at<float>( 3 ) * Height );
//             const cv::Rect rectangle  = cv::Rect( x_center - ( width / 2 ), y_center - ( height / 2 ), width, height );

//             // Add Class ID, Confidence, Rectangle
//             class_ids.push_back( class_id.x );
//             confidences.push_back( confidence );
//             boxes.push_back( rectangle );
//         }
//     }

//     std::vector<int> indices;
//     cv::dnn::NMSBoxes(boxes, confidences, 0.5, 0.4, indices);

//     for (int idx : indices) {
//         if (idx < boxes.size()) {
//             cv::Rect box = boxes[idx];

//             // Corners
//             TArray<FVector2D> corners;
//             corners.Add(FVector2D(box.x, box.y));
//             corners.Add(FVector2D(box.x + box.width, box.y));
//             corners.Add(FVector2D(box.x + box.width, box.y + box.height));
//             corners.Add(FVector2D(box.x, box.y + box.height));

//             // Label
//             FString label = FString::Printf(
//                 TEXT("%s: %.2f"),
//                 *FString(class_names[class_ids[idx]].c_str()),
//                 confidences[idx]
//             );

//             FDetectionResult detection;
//             detection.Corners = corners;
//             detection.Label   = label;

//             results.Add(detection);
//         }
//     }

//     LastFrameDetections = results;  // store for HUD
//     return results;

//     // return Image;
// }


// // Called every frame
// void UMyActorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
// {
// 	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);	// ...
// }

// void UMyActorComponent::UpdateDetections(const TArray<FDetectionResult>& NewDetections)
// {
//     LastFrameDetections = NewDetections;
// }
