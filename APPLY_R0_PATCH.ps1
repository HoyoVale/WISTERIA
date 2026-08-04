$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
$LegacyNativeFile = Join-Path $ProjectRoot 'src/native/wisteria_native.cpp'
if (Test-Path -LiteralPath $LegacyNativeFile -PathType Leaf) {
    Remove-Item -LiteralPath $LegacyNativeFile -Force
    Write-Host '已删除旧的 src/native/wisteria_native.cpp' -ForegroundColor Yellow
}
Write-Host '补丁清理完成。开始 Windows 验收：' -ForegroundColor Green
Write-Host 'powershell -ExecutionPolicy Bypass -File .\script\verify_render.ps1'
