[CmdletBinding()]
param(
    # 包含 models/ 和 motions/ 的目录，或包含 assets/models/ 和
    # assets/motions/ 的目录（例如另一个 WISTERIA 工作区）。
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,

    # 目标资源根目录，默认为本仓库的 assets/。
    [string]$DestinationRoot,

    # 同时复制场景模式与备用模型（源目录中存在时才会复制）。
    [switch]$IncludeOptional
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($DestinationRoot)) {
    $DestinationRoot = Join-Path $ProjectRoot 'assets'
}

$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$DestinationRoot = [System.IO.Path]::GetFullPath($DestinationRoot)

if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
    throw "源目录不存在：$SourceRoot"
}

# 兼容两种源目录形态：
#   1) <SourceRoot>/models + <SourceRoot>/motions
#   2) <SourceRoot>/assets/models + <SourceRoot>/assets/motions
$sourceModels = Join-Path $SourceRoot 'models'
$sourceMotions = Join-Path $SourceRoot 'motions'
$sourceAssets = Join-Path $SourceRoot 'assets'
if ((Test-Path -LiteralPath $sourceModels) -and
    (Test-Path -LiteralPath $sourceMotions)) {
    $sourceModelsRoot = $sourceModels
    $sourceMotionsRoot = $sourceMotions
}
elseif ((Test-Path -LiteralPath (Join-Path $sourceAssets 'models')) -and
        (Test-Path -LiteralPath (Join-Path $sourceAssets 'motions'))) {
    $sourceModelsRoot = Join-Path $sourceAssets 'models'
    $sourceMotionsRoot = Join-Path $sourceAssets 'motions'
}
else {
    throw @"
找不到资产布局。期望以下两种之一：

  $SourceRoot\models\  和  $SourceRoot\motions\
  或
  $SourceRoot\assets\models\  和  $SourceRoot\assets\motions\
"@
}

function Copy-DemoDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath,

        [Parameter(Mandatory = $true)]
        [string]$SourceRootDirectory,

        [Parameter(Mandatory = $true)]
        [string]$DestinationRootDirectory,

        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    $source = Join-Path $SourceRootDirectory $RelativePath
    $destination = Join-Path $DestinationRootDirectory $RelativePath

    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        Write-Warning "[跳过] 源目录不存在：$source"
        return
    }

    Write-Host "[复制] $Label" -ForegroundColor Cyan
    Write-Host "  $source"
    Write-Host "  -> $destination"

    # Copy-Item 对已存在目录会嵌套，因此先删除旧目标再整体复制。
    if (Test-Path -LiteralPath $destination) {
        Remove-Item -LiteralPath $destination -Recurse -Force
    }
    $parent = Split-Path -Parent $destination
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    Copy-Item -LiteralPath $source -Destination $destination -Recurse -Force
}

# 默认 Demo 必需项。
Copy-DemoDirectory `
    -RelativePath 'mmd\蕾米埃尔-黑' `
    -SourceRootDirectory $sourceModelsRoot `
    -DestinationRootDirectory (Join-Path $DestinationRoot 'models') `
    -Label '默认角色模型（蕾米埃尔-黑）'

Copy-DemoDirectory `
    -RelativePath '梦的翅膀' `
    -SourceRootDirectory $sourceMotionsRoot `
    -DestinationRootDirectory (Join-Path $DestinationRoot 'motions') `
    -Label '默认动作与相机（梦的翅膀）'

# 可选：场景模式与备用模型。
if ($IncludeOptional) {
    Copy-DemoDirectory `
        -RelativePath 'mmd\随便观' `
        -SourceRootDirectory $sourceModelsRoot `
        -DestinationRootDirectory (Join-Path $DestinationRoot 'models') `
        -Label '场景模型（随便观）'

    Copy-DemoDirectory `
        -RelativePath 'mmd\叶瞬光皮肤_pmx' `
        -SourceRootDirectory $sourceModelsRoot `
        -DestinationRootDirectory (Join-Path $DestinationRoot 'models') `
        -Label '备用模型（叶瞬光皮肤_pmx）'
}

$requiredModel = Join-Path $DestinationRoot 'models\mmd\蕾米埃尔-黑\蕾米埃尔-黑.pmx'
if (-not (Test-Path -LiteralPath $requiredModel -PathType Leaf)) {
    throw "默认角色 PMX 未复制成功：$requiredModel"
}

Write-Host ''
Write-Host '演示资产准备完成。' -ForegroundColor Green
Write-Host "  资产根目录：$DestinationRoot"
Write-Host '  运行：.\run.ps1 run'
