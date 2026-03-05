$ErrorActionPreference = "Stop"

# 1. Convert PNG to ICO
Write-Host "Converting Logo to ICO..."
.\convert_ico.ps1

# 2. Setup MSVC Environment
# We found it here: C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"

Write-Host "Compiling Resources..."
cmd.exe /c "`"$vcvars`" x64 && rc.exe resource.rc"

Write-Host "Compiling C++ Source Code..."
cmd.exe /c "`"$vcvars`" x64 && cl.exe /EHsc /O2 /MD main.cpp resource.res user32.lib gdi32.lib /link /OUT:Pushly.exe /SUBSYSTEM:WINDOWS"

Write-Host "Build complete! Pushly.exe generated."
