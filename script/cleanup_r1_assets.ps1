param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [switch]$IncludeBuildFolders
)

$ErrorActionPreference = 'Stop'

Write-Host "Project root: $ProjectRoot"

$metadata = Get-ChildItem -LiteralPath $ProjectRoot -Recurse -Force -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Name -eq '.DS_Store' -or
        $_.Name -eq '__MACOSX' -or
        $_.Name -like '._*'
    } |
    Sort-Object FullName -Descending

foreach ($item in $metadata) {
    Write-Host "Removing metadata: $($item.FullName)"
    Remove-Item -LiteralPath $item.FullName -Recurse -Force -ErrorAction SilentlyContinue
}

if ($IncludeBuildFolders) {
    Get-ChildItem -LiteralPath $ProjectRoot -Directory -Force |
        Where-Object { $_.Name -eq 'build' -or $_.Name -like 'build-*' } |
        ForEach-Object {
            Write-Host "Removing build folder: $($_.FullName)"
            Remove-Item -LiteralPath $_.FullName -Recurse -Force
        }
}

Write-Host 'R1 cleanup complete.'
