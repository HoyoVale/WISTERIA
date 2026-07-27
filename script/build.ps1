param(
    [string]$cmake
)

cd $PSScriptRoot
cd '..'


& $cmake -S . -B build
& $cmake --build build