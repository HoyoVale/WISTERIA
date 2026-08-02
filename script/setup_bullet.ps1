[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ThirdPartyRoot = Join-Path $ProjectRoot 'third-party'
$Destination = Join-Path $ThirdPartyRoot 'bullet3'
$Temporary = Join-Path $ThirdPartyRoot '.bullet3-download'
$ExpectedCommit = '2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5'
$Tag = '3.25'
$Repository = 'https://github.com/bulletphysics/bullet3.git'
$RequiredHeader = Join-Path $Destination 'src/btBulletDynamicsCommon.h'

if ((Test-Path -LiteralPath $RequiredHeader -PathType Leaf) -and -not $Force) {
    Write-Host "Bullet $Tag 已存在：$Destination" -ForegroundColor Green
    return
}

$GitCommand = Get-Command git.exe -ErrorAction SilentlyContinue
if ($null -eq $GitCommand) {
    $GitCommand = Get-Command git -ErrorAction SilentlyContinue
}
if ($null -eq $GitCommand) {
    throw '找不到 Git。请先安装 Git for Windows 并加入 PATH。'
}

Remove-Item -LiteralPath $Temporary -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $ThirdPartyRoot -Force | Out-Null

try {
    Write-Host "正在获取 Bullet $Tag..." -ForegroundColor Cyan
    & $GitCommand.Source clone `
        --branch $Tag `
        --depth 1 `
        --single-branch `
        --filter=blob:none `
        --sparse `
        $Repository `
        $Temporary
    if ($LASTEXITCODE -ne 0) {
        throw "Bullet clone 失败，退出码：$LASTEXITCODE"
    }

    & $GitCommand.Source -C $Temporary sparse-checkout set src
    if ($LASTEXITCODE -ne 0) {
        throw "Bullet sparse-checkout 失败，退出码：$LASTEXITCODE"
    }

    $ActualCommit = (& $GitCommand.Source -C $Temporary rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $ActualCommit -ne $ExpectedCommit) {
        throw "Bullet 提交校验失败。期望 $ExpectedCommit，实际 $ActualCommit"
    }

    $UpstreamNote = Join-Path $Destination 'UPSTREAM.md'
    $SavedNote = $null
    if (Test-Path -LiteralPath $UpstreamNote -PathType Leaf) {
        $SavedNote = Get-Content -LiteralPath $UpstreamNote -Raw -Encoding UTF8
    }

    Remove-Item -LiteralPath $Destination -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null

    # WISTERIA needs Bullet's source tree, license and version only. Examples,
    # Python bindings, data files and build-system integrations are not copied.
    Copy-Item `
        -LiteralPath (Join-Path $Temporary 'src') `
        -Destination (Join-Path $Destination 'src') `
        -Recurse
    Copy-Item `
        -LiteralPath (Join-Path $Temporary 'LICENSE.txt') `
        -Destination (Join-Path $Destination 'LICENSE.txt')
    Copy-Item `
        -LiteralPath (Join-Path $Temporary 'VERSION') `
        -Destination (Join-Path $Destination 'VERSION')

    if ($null -ne $SavedNote) {
        Set-Content -LiteralPath $UpstreamNote -Value $SavedNote -Encoding UTF8
    }

    Set-Content `
        -LiteralPath (Join-Path $Destination '.wisteria-version') `
        -Value $ExpectedCommit `
        -Encoding ASCII

    if (-not (Test-Path -LiteralPath $RequiredHeader -PathType Leaf)) {
        throw "Bullet 源码不完整，缺少：$RequiredHeader"
    }

    Write-Host "Bullet $Tag 已固定到项目：$Destination" -ForegroundColor Green
    Write-Host '请将 third-party/bullet3 一并提交到 Git。' -ForegroundColor Yellow
}
finally {
    Remove-Item -LiteralPath $Temporary -Recurse -Force -ErrorAction SilentlyContinue
}
