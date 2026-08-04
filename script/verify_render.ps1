[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',

    [ValidateRange(60, 3600)]
    [int]$Frames = 180,

    [string]$Model = '',
    [string]$Motion = '',

    [switch]$SkipNativeDemos,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildPath = Join-Path $ProjectRoot 'build-verify'
$OutputRoot = Join-Path $ProjectRoot 'artifacts/render-smoke/windows'

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "命令执行失败（退出码 $LASTEXITCODE）：$FilePath $($Arguments -join ' ')"
    }
}

function Find-Executable {
    param([string[]]$Candidates)
    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }
    throw "找不到可执行文件。已检查：$($Candidates -join ', ')"
}

function Reset-Directory {
    param([string]$Path)
    Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Show-CaptureSummary {
    param([string]$Path)
    $Files = @(Get-ChildItem -LiteralPath $Path -Filter '*.bmp' -File | Sort-Object Name)
    if ($Files.Count -lt 2) {
        throw "截图数量不足：$Path 中只找到 $($Files.Count) 张 BMP。"
    }

    $Hashes = @()
    foreach ($File in $Files) {
        $Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $File.FullName).Hash
        $Hashes += $Hash
        Write-Host ("[CAPTURE] {0}  {1}" -f $Hash.Substring(0, 16), $File.Name)
    }
    $UniqueCount = @($Hashes | Sort-Object -Unique).Count
    if ($UniqueCount -le 1) {
        throw "所有截图完全相同，疑似动画/渲染卡在同一帧：$Path"
    }
    Write-Host "[CAPTURE] $($Files.Count) 张截图，$UniqueCount 个不同 SHA256。" -ForegroundColor Green
}

$CMake = (Get-Command cmake.exe -ErrorAction Stop).Source
$CTest = (Get-Command ctest.exe -ErrorAction Stop).Source

if (-not $SkipBuild) {
    Write-Host '==> 配置 Windows 验证构建（Native=ON）' -ForegroundColor Cyan
    Invoke-Checked $CMake @(
        '-S', $ProjectRoot,
        '-B', $BuildPath,
        '-DWISTERIA_BUILD_NATIVE=ON',
        '-DBUILD_TESTING=ON'
    )

    Write-Host '==> 编译 wisteria / tests / native' -ForegroundColor Cyan
    Invoke-Checked $CMake @(
        '--build', $BuildPath,
        '--config', $Configuration,
        '--target', 'wisteria', 'wisteria_tests', 'wisteria_native',
        '--parallel'
    )
}

Write-Host '==> 运行 CTest' -ForegroundColor Cyan
Invoke-Checked $CTest @(
    '--test-dir', $BuildPath,
    '-C', $Configuration,
    '--output-on-failure'
)

$WisteriaExe = Find-Executable @(
    (Join-Path $BuildPath "$Configuration/wisteria.exe"),
    (Join-Path $BuildPath 'wisteria.exe')
)
$NativeLibrary = Find-Executable @(
    (Join-Path $BuildPath "$Configuration/wisteria_native.dll"),
    (Join-Path $BuildPath 'wisteria_native.dll')
)

$PythonCommand = $null
$PythonPrefix = @()
if (Get-Command py.exe -ErrorAction SilentlyContinue) {
    $PythonCommand = (Get-Command py.exe).Source
    $PythonPrefix = @('-3')
}
elseif (Get-Command python.exe -ErrorAction SilentlyContinue) {
    $PythonCommand = (Get-Command python.exe).Source
}
elseif (-not $SkipNativeDemos) {
    throw '找不到 Python 3。可安装 Python，或使用 -SkipNativeDemos 只测 C++ 窗口。'
}

$OldEnvironment = @{
    WISTERIA_ASSET_ROOT = $env:WISTERIA_ASSET_ROOT
    WISTERIA_FRAME_PROFILE = $env:WISTERIA_FRAME_PROFILE
    WISTERIA_GL_DIAGNOSTICS = $env:WISTERIA_GL_DIAGNOSTICS
    WISTERIA_SCREENSHOT_DIR = $env:WISTERIA_SCREENSHOT_DIR
    WISTERIA_SCREENSHOT_INTERVAL = $env:WISTERIA_SCREENSHOT_INTERVAL
    WISTERIA_NATIVE_LIB = $env:WISTERIA_NATIVE_LIB
}

try {
    $env:WISTERIA_ASSET_ROOT = Join-Path $ProjectRoot 'assets'
    $env:WISTERIA_FRAME_PROFILE = '1'
    $env:WISTERIA_GL_DIAGNOSTICS = '1'
    $env:WISTERIA_SCREENSHOT_INTERVAL = '30'
    $env:WISTERIA_NATIVE_LIB = $NativeLibrary

    $DesktopOutput = Join-Path $OutputRoot 'desktop'
    Reset-Directory $DesktopOutput
    $env:WISTERIA_SCREENSHOT_DIR = $DesktopOutput

    $DesktopArguments = @('--frames', "$Frames", '--fixed-dt', '0.016666667')
    if (-not [string]::IsNullOrWhiteSpace($Model)) {
        $DesktopArguments += @('--model', $Model)
    }
    if (-not [string]::IsNullOrWhiteSpace($Motion)) {
        $DesktopArguments += @('--motion', $Motion)
    }

    Write-Host "==> C++ 桌面 demo：固定运行 $Frames 帧" -ForegroundColor Cyan
    Push-Location $ProjectRoot
    try {
        Invoke-Checked $WisteriaExe $DesktopArguments
    }
    finally {
        Pop-Location
    }
    Show-CaptureSummary $DesktopOutput

    if (-not $SkipNativeDemos) {
        $NativeOutput = Join-Path $OutputRoot 'native-single'
        Reset-Directory $NativeOutput
        $env:WISTERIA_SCREENSHOT_DIR = $NativeOutput
        $NativeArguments = @(
            'examples/python/native_window_demo.py',
            '--frames', "$Frames"
        )
        if (-not [string]::IsNullOrWhiteSpace($Model)) {
            $NativeArguments += @('--model', $Model)
        }
        if (-not [string]::IsNullOrWhiteSpace($Motion)) {
            $NativeArguments += @('--motion', $Motion)
        }

        Write-Host "==> Python ctypes 单 Context demo：$Frames 帧" -ForegroundColor Cyan
        Push-Location $ProjectRoot
        try {
            Invoke-Checked $PythonCommand ($PythonPrefix + $NativeArguments)
        }
        finally {
            Pop-Location
        }
        Show-CaptureSummary $NativeOutput

        $MultiOutput = Join-Path $OutputRoot 'native-multi'
        Reset-Directory $MultiOutput
        $env:WISTERIA_SCREENSHOT_DIR = $MultiOutput
        $DestroyAt = [Math]::Max(30, [Math]::Floor($Frames / 2))
        $MultiArguments = @(
            'examples/python/native_multi_context_demo.py',
            '--frames', "$Frames",
            '--destroy-first-at', "$DestroyAt"
        )
        if (-not [string]::IsNullOrWhiteSpace($Model)) {
            $MultiArguments += @('--model', $Model)
        }
        if (-not [string]::IsNullOrWhiteSpace($Motion)) {
            $MultiArguments += @('--motion', $Motion)
        }

        Write-Host '==> Python ctypes 双 Context 生命周期 demo' -ForegroundColor Cyan
        Push-Location $ProjectRoot
        try {
            Invoke-Checked $PythonCommand ($PythonPrefix + $MultiArguments)
        }
        finally {
            Pop-Location
        }
        Show-CaptureSummary $MultiOutput
    }
}
finally {
    foreach ($Name in $OldEnvironment.Keys) {
        Set-Item -Path "Env:$Name" -Value $OldEnvironment[$Name] -ErrorAction SilentlyContinue
        if ($null -eq $OldEnvironment[$Name]) {
            Remove-Item -Path "Env:$Name" -ErrorAction SilentlyContinue
        }
    }
}

Write-Host "验证完成。截图与日志目录：$OutputRoot" -ForegroundColor Green
