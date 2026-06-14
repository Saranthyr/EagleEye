param(
    [string]$UERoot = "E:\UE_5.6",
    [string]$Config = "Development",
    [string]$ArchiveRoot = "",
    [string]$Maps = "/Game/ThirdPerson/Blueprints/TestMap/TestWorld+/Game/ThirdPerson/Blueprints/ProcGen/ProcGen",
    [switch]$SkipUAT
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ProjectFile = Join-Path $ProjectRoot "EagleEye.uproject"
$OnnxRuntimeRoot = Join-Path $ProjectRoot "ThirdParty\OnnxRuntimeDirectML"
$DirectMLRoot = Join-Path $ProjectRoot "ThirdParty\DirectML"
$EnvFile = Join-Path $ProjectRoot "Saved\InferenceDeps.ps1"

if ([string]::IsNullOrWhiteSpace($ArchiveRoot)) {
    $ArchiveRoot = Join-Path $ProjectRoot "Builds\Win10-DirectML-Metrics"
}

function Write-Log {
    param([string]$Message)
    Write-Host "[EagleEye package] $Message"
}

function Find-RequiredFile {
    param(
        [string[]]$Candidates,
        [string]$Name
    )

    foreach ($Candidate in $Candidates) {
        if ($Candidate -and (Test-Path -LiteralPath $Candidate)) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }

    throw "Missing $Name. Run: Scripts\setup-inference-deps.ps1 -InstallDirectML -InstallDirectMLRedist"
}

function Copy-RuntimeDll {
    param(
        [string]$Source,
        [string[]]$DestinationDirs
    )

    foreach ($DestinationDir in $DestinationDirs) {
        if ([string]::IsNullOrWhiteSpace($DestinationDir)) {
            continue
        }

        New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
        Copy-Item -LiteralPath $Source -Destination (Join-Path $DestinationDir (Split-Path -Leaf $Source)) -Force
    }
}

function Get-PackageRuntimeDirs {
    param([string]$Root)

    $Dirs = New-Object System.Collections.Generic.HashSet[string]

    if (Test-Path -LiteralPath $Root) {
        Get-ChildItem -LiteralPath $Root -Directory -Recurse |
            Where-Object { $_.FullName -match '\\Binaries\\Win64$' } |
            ForEach-Object { [void]$Dirs.Add($_.FullName) }

        Get-ChildItem -LiteralPath $Root -Filter "EagleEye*.exe" -File -Recurse |
            ForEach-Object { [void]$Dirs.Add($_.DirectoryName) }
    }

    return @($Dirs.GetEnumerator())
}

$OnnxRuntimeDll = Find-RequiredFile @(
    (Join-Path $OnnxRuntimeRoot "runtimes\win-x64\native\onnxruntime.dll"),
    (Join-Path $OnnxRuntimeRoot "bin\onnxruntime.dll"),
    (Join-Path $OnnxRuntimeRoot "lib\onnxruntime.dll")
) "onnxruntime.dll"

$OnnxRuntimeProvidersSharedDll = Find-RequiredFile @(
    (Join-Path $OnnxRuntimeRoot "runtimes\win-x64\native\onnxruntime_providers_shared.dll"),
    (Join-Path $OnnxRuntimeRoot "bin\onnxruntime_providers_shared.dll"),
    (Join-Path $OnnxRuntimeRoot "lib\onnxruntime_providers_shared.dll")
) "onnxruntime_providers_shared.dll"

$DirectMLDll = Find-RequiredFile @(
    (Join-Path $DirectMLRoot "bin\x64-win\DirectML.dll"),
    (Join-Path $DirectMLRoot "runtimes\win-x64\native\DirectML.dll"),
    (Join-Path $DirectMLRoot "native\DirectML.dll"),
    (Join-Path $DirectMLRoot "DirectML.dll")
) "DirectML.dll"

& (Join-Path $ProjectRoot "Scripts\setup-inference-deps.ps1") `
    -OnnxRuntimeRoot $OnnxRuntimeRoot `
    -DirectMLRoot $DirectMLRoot `
    -EnvFile $EnvFile

. $EnvFile
$env:ONNXRUNTIME_ROOT = $OnnxRuntimeRoot
$env:ORT_ROOT = $OnnxRuntimeRoot
$env:DIRECTML_ROOT = $DirectMLRoot
$env:PATH = "$(Split-Path -Parent $OnnxRuntimeDll);$(Split-Path -Parent $DirectMLDll);$env:PATH"

if (-not $SkipUAT) {
    $RunUAT = Join-Path $UERoot "Engine\Build\BatchFiles\RunUAT.bat"
    if (-not (Test-Path -LiteralPath $RunUAT)) {
        throw "Missing RunUAT.bat at $RunUAT"
    }

    Write-Log "Running BuildCookRun Win64 $Config -> $ArchiveRoot"
    & $RunUAT BuildCookRun `
        "-project=$ProjectFile" `
        -noP4 `
        -platform=Win64 `
        "-clientconfig=$Config" `
        -build `
        -cook `
        -stage `
        -pak `
        -archive `
        "-archivedirectory=$ArchiveRoot" `
        "-map=$Maps"
}

$RuntimeDirs = Get-PackageRuntimeDirs $ArchiveRoot
if ($RuntimeDirs.Count -eq 0) {
    throw "Packaged runtime directory not found under $ArchiveRoot"
}

Copy-RuntimeDll $OnnxRuntimeDll $RuntimeDirs
Copy-RuntimeDll $OnnxRuntimeProvidersSharedDll $RuntimeDirs
Copy-RuntimeDll $DirectMLDll $RuntimeDirs

foreach ($RuntimeDir in $RuntimeDirs) {
    foreach ($DllName in @("onnxruntime.dll", "onnxruntime_providers_shared.dll", "DirectML.dll")) {
        $DllPath = Join-Path $RuntimeDir $DllName
        if (-not (Test-Path -LiteralPath $DllPath)) {
            throw "Missing staged DLL: $DllPath"
        }
    }
}

Write-Log "Packaged build ready: $ArchiveRoot"
Write-Log "Verified DirectML runtime dirs: $($RuntimeDirs -join ', ')"
