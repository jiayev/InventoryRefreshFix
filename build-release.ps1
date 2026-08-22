[CmdletBinding()]
param(
    [ValidateSet("SE", "AE", "All")]
    [string]$Target = "All",

    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSCommandPath
$buildRoot = Join-Path $repositoryRoot "build"

function Get-SafeChildPath {
    param(
        [Parameter(Mandatory)]
        [string]$Root,

        [Parameter(Mandatory)]
        [string]$Child
    )

    $fullRoot = [System.IO.Path]::GetFullPath($Root)
    $fullChild = [System.IO.Path]::GetFullPath($Child)
    $rootWithSeparator = $fullRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar

    if (-not $fullChild.StartsWith($rootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to operate outside '$fullRoot': '$fullChild'"
    }

    return $fullChild
}

function Invoke-XMake {
    param([Parameter(Mandatory)][string[]]$Arguments)

    Write-Host "> xmake $($Arguments -join ' ')"
    & xmake @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "xmake failed with exit code $LASTEXITCODE"
    }
}

$releaseRoot = Get-SafeChildPath -Root $repositoryRoot -Child (Join-Path $repositoryRoot "release")

function Build-And-Package {
    param(
        [Parameter(Mandatory)]
        [ValidateSet("SE", "AE")]
        [string]$Variant
    )

    $variantBuildRoot = Get-SafeChildPath -Root $buildRoot -Child (Join-Path $buildRoot $Variant)
    $buildDirectory = Get-SafeChildPath -Root $variantBuildRoot -Child (Join-Path $variantBuildRoot "output")
    $packageDirectory = Get-SafeChildPath -Root $buildDirectory -Child (Join-Path $buildDirectory "packages")

    if ($Clean -and (Test-Path -LiteralPath $variantBuildRoot)) {
        Remove-Item -LiteralPath $variantBuildRoot -Recurse -Force
    }

    Push-Location $repositoryRoot
    try {
        $aeOption = if ($Variant -eq "AE") { "y" } else { "n" }
        Invoke-XMake @("f", "-o", $buildDirectory, "-m", "releasedbg", "--skyrim_ae=$aeOption")
        Invoke-XMake @("build")

        if (Test-Path -LiteralPath $packageDirectory) {
            $stalePackages = @(Get-ChildItem -LiteralPath $packageDirectory -Filter "*.zip" -File)
            foreach ($package in $stalePackages) {
                Remove-Item -LiteralPath $package.FullName -Force
            }
        }

        Invoke-XMake @("package")
    }
    finally {
        Pop-Location
    }

    $packages = @(Get-ChildItem -LiteralPath $packageDirectory -Filter "*.zip" -File | Sort-Object LastWriteTimeUtc -Descending)
    if ($packages.Count -ne 1) {
        throw "Expected exactly one ZIP in '$packageDirectory', found $($packages.Count)."
    }

    New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
    $releasePath = Join-Path $releaseRoot ("{0}-{1}.zip" -f $packages[0].BaseName, $Variant)
    Copy-Item -LiteralPath $packages[0].FullName -Destination $releasePath -Force

    return Get-Item -LiteralPath $releasePath
}

if (-not (Get-Command xmake -ErrorAction SilentlyContinue)) {
    throw "xmake was not found on PATH. Install XMake 3.0.0 or later before running this script."
}

if ($Clean -and (Test-Path -LiteralPath $releaseRoot)) {
    Remove-Item -LiteralPath $releaseRoot -Recurse -Force
}

$variants = if ($Target -eq "All") { @("SE", "AE") } else { @($Target) }
$artifacts = foreach ($variant in $variants) {
    Build-And-Package -Variant $variant
}

$checksumPath = Join-Path $releaseRoot "SHA256SUMS.txt"
$checksumLines = foreach ($artifact in $artifacts | Sort-Object Name) {
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $artifact.FullName
    "{0} *{1}" -f $hash.Hash.ToLowerInvariant(), $artifact.Name
}
Set-Content -LiteralPath $checksumPath -Value $checksumLines -Encoding utf8

Write-Host "Release artifacts:"
$artifacts | ForEach-Object { Write-Host "  $($_.FullName)" }
Write-Host "  $checksumPath"
