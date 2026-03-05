@echo off
set "VSCMD_START_DIR=%CD%"
set "PATH_BACKUP=%PATH%"

:: Remove all stray double quotes from PATH to prevent VsDevCmd.bat from crashing
set "PATH=%PATH:"=%"

call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64

echo --- RUNNING RC.EXE ---
rc.exe resource.rc

echo --- RUNNING CL.EXE ---
cl.exe /EHsc /O2 /MD main.cpp resource.res user32.lib gdi32.lib comctl32.lib /link /OUT:Pushly.exe /SUBSYSTEM:WINDOWS

set "PATH=%PATH_BACKUP%"
