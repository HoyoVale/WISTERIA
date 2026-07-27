$cmake = 'C:\\Program Files\\CMake\\bin\\cmake.exe' # cmake 地址，需要更换成实际地址
$clear = '.\\script\\clear_build.ps1'               
$build = '.\\script\\build.ps1'
$build_path = '.\\build'
$execute = '.\\build\\Debug\\wisteria.exe'


& $cmake --version
& $clear $build_path
& $build $cmake
if(Test-Path $execute){
    & $execute
}
else{
    Write-Error '可执行文件未生成'
}
