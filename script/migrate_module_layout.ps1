param(
    [Parameter(Mandatory = $false)]
    [string]$ProjectRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
else {
    $ProjectRoot = (Resolve-Path $ProjectRoot).Path
}

$cmakePath = Join-Path $ProjectRoot "CMakeLists.txt"
$newPhysicsHeader = Join-Path $ProjectRoot "include\wisteria\physics\physics_world.hpp"
$newMmdPolicyHeader = Join-Path $ProjectRoot "include\wisteria\mmd\physics\mmd_physics_policy.hpp"
$manifestPath = Join-Path $PSScriptRoot "module_layout_removed_paths.txt"

if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
    throw "ProjectRoot does not contain CMakeLists.txt: $ProjectRoot"
}
if (-not (Test-Path -LiteralPath $newPhysicsHeader -PathType Leaf) -or
    -not (Test-Path -LiteralPath $newMmdPolicyHeader -PathType Leaf)) {
    throw "New modular files are missing. Extract the refactor patch over the project before running this script."
}
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Migration manifest is missing: $manifestPath"
}

$removedFiles = 0
$missingFiles = 0
foreach ($relativePath in Get-Content -LiteralPath $manifestPath -Encoding UTF8) {
    $trimmed = $relativePath.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith("#")) {
        continue
    }

    $target = Join-Path $ProjectRoot ($trimmed -replace '/', '\')
    if (Test-Path -LiteralPath $target -PathType Leaf) {
        Remove-Item -LiteralPath $target -Force
        $removedFiles++
    }
    else {
        $missingFiles++
    }
}

$legacyDirectories = @(
    (Join-Path $ProjectRoot "include\Models"),
    (Join-Path $ProjectRoot "src\Models")
)
foreach ($directory in $legacyDirectories) {
    if (Test-Path -LiteralPath $directory -PathType Container) {
        $children = @(Get-ChildItem -LiteralPath $directory -Force)
        if ($children.Count -eq 0) {
            Remove-Item -LiteralPath $directory -Force
        }
    }
}

Write-Host "WISTERIA module-layout migration completed." -ForegroundColor Green
Write-Host "Project root: $ProjectRoot"
Write-Host "Removed legacy files: $removedFiles"
Write-Host "Already absent: $missingFiles"
Write-Host "Next: .\run.ps1 test"
