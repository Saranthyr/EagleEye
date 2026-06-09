param(
    [switch]$InstallDirectML,
    [string]$PackageVersion = "1.24.4",
    [switch]$InstallDirectMLRedist,
    [string]$DirectMLVersion = "1.15.2",
    [string]$OnnxRuntimeRoot = "",
    [string]$DirectMLRoot = "",
    [string]$EnvFile = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OnnxRuntimeRoot)) {
    $OnnxRuntimeRoot = Join-Path $ProjectRoot "ThirdParty\OnnxRuntimeDirectML"
}
if ([string]::IsNullOrWhiteSpace($EnvFile)) {
    $EnvFile = Join-Path $ProjectRoot "Saved\InferenceDeps.ps1"
}
if ([string]::IsNullOrWhiteSpace($DirectMLRoot)) {
    $DirectMLRoot = Join-Path $ProjectRoot "ThirdParty\DirectML"
}

function Write-Log {
    param([string]$Message)
    Write-Host "[EagleEye deps] $Message"
}

function Test-DirectMLLayout {
    param([string]$Root)

    $IncludeDir = Join-Path $Root "build\native\include"
    $NativeDir = Join-Path $Root "runtimes\win-x64\native"
    $FlatDmlHeader = Join-Path $IncludeDir "dml_provider_factory.h"
    $NestedDmlHeader = Join-Path $IncludeDir "onnxruntime\core\providers\dml\dml_provider_factory.h"

    return (Test-Path (Join-Path $IncludeDir "onnxruntime_cxx_api.h")) -and
        ((Test-Path $FlatDmlHeader) -or (Test-Path $NestedDmlHeader)) -and
        (Test-Path (Join-Path $NativeDir "onnxruntime.lib")) -and
        (Test-Path (Join-Path $NativeDir "onnxruntime.dll"))
}

function Test-DirectMLRedistLayout {
    param([string]$Root)

    $CandidateDlls = @(
        (Join-Path $Root "bin\x64-win\DirectML.dll"),
        (Join-Path $Root "runtimes\win-x64\native\DirectML.dll"),
        (Join-Path $Root "native\DirectML.dll"),
        (Join-Path $Root "DirectML.dll")
    )

    foreach ($Dll in $CandidateDlls) {
        if (Test-Path $Dll) {
            return $true
        }
    }

    return $false
}

function Install-OnnxRuntimeDirectML {
    param(
        [string]$Root,
        [string]$Version
    )

    New-Item -ItemType Directory -Force -Path $Root | Out-Null
    $PackageId = "microsoft.ml.onnxruntime.directml"
    $PackageFile = Join-Path $env:TEMP "$PackageId.$Version.nupkg"
    $PackageZip = Join-Path $env:TEMP "$PackageId.$Version.zip"
    $PackageUrl = "https://api.nuget.org/v3-flatcontainer/$PackageId/$Version/$PackageId.$Version.nupkg"

    Write-Log "Downloading Microsoft.ML.OnnxRuntime.DirectML $Version"
    Invoke-WebRequest -Uri $PackageUrl -OutFile $PackageFile
    Copy-Item -LiteralPath $PackageFile -Destination $PackageZip -Force

    Write-Log "Extracting to $Root"
    if (Test-Path $Root) {
        Get-ChildItem -LiteralPath $Root -Force | Remove-Item -Recurse -Force
    }
    Expand-Archive -Path $PackageZip -DestinationPath $Root -Force
}

function Install-DirectMLRedist {
    param(
        [string]$Root,
        [string]$Version
    )

    New-Item -ItemType Directory -Force -Path $Root | Out-Null
    $PackageId = "microsoft.ai.directml"
    $PackageFile = Join-Path $env:TEMP "$PackageId.$Version.nupkg"
    $PackageZip = Join-Path $env:TEMP "$PackageId.$Version.zip"
    $PackageUrl = "https://api.nuget.org/v3-flatcontainer/$PackageId/$Version/$PackageId.$Version.nupkg"

    Write-Log "Downloading Microsoft.AI.DirectML $Version"
    Invoke-WebRequest -Uri $PackageUrl -OutFile $PackageFile
    Copy-Item -LiteralPath $PackageFile -Destination $PackageZip -Force

    Write-Log "Extracting to $Root"
    if (Test-Path $Root) {
        Get-ChildItem -LiteralPath $Root -Force | Remove-Item -Recurse -Force
    }
    Expand-Archive -Path $PackageZip -DestinationPath $Root -Force
}

if (-not $InstallDirectML -and -not (Test-DirectMLLayout $OnnxRuntimeRoot)) {
    throw "ONNX Runtime DirectML not found at $OnnxRuntimeRoot. Rerun with -InstallDirectML."
}

if ($InstallDirectML) {
    Install-OnnxRuntimeDirectML -Root $OnnxRuntimeRoot -Version $PackageVersion
}

if ($InstallDirectMLRedist) {
    Install-DirectMLRedist -Root $DirectMLRoot -Version $DirectMLVersion
}

if (-not (Test-DirectMLLayout $OnnxRuntimeRoot)) {
    throw "Invalid ONNX Runtime DirectML layout at $OnnxRuntimeRoot. Expected build\native\include and runtimes\win-x64\native assets."
}

if (-not (Test-DirectMLRedistLayout $DirectMLRoot)) {
    Write-Log "DirectML redistributable not found at $DirectMLRoot. DirectML EP may use old C:\Windows\System32\DirectML.dll. Rerun with -InstallDirectMLRedist."
}

$EnvDir = Split-Path -Parent $EnvFile
New-Item -ItemType Directory -Force -Path $EnvDir | Out-Null
@"
# Dot-source this before building/running EagleEye with ONNX Runtime DirectML.
`$env:ONNXRUNTIME_ROOT = "$OnnxRuntimeRoot"
`$env:ORT_ROOT = "$OnnxRuntimeRoot"
`$env:DIRECTML_ROOT = "$DirectMLRoot"
`$env:PATH = "$OnnxRuntimeRoot\runtimes\win-x64\native;`$env:PATH"
"@ | Set-Content -Path $EnvFile -Encoding ASCII

Write-Log "ONNX Runtime DirectML ready: $OnnxRuntimeRoot"
Write-Log "Wrote env file: $EnvFile"
Write-Log "Next: . `"$EnvFile`""
