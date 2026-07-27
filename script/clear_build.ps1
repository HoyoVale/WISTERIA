param(
    [string]$build_path
)

cd $PSScriptRoot
cd '..'

if (Test-Path $build_path){
    Remove-Item `
        -Path $build_path `
        -Recurse `
        -Force `
        -Confirm:$false
    Write-Host "已清除 build 文件夹" -ForeGroundColor Green
}
else {
    Write-Host "[warn] build 文件夹不存在" -ForeGroundColor Yellow 
}
