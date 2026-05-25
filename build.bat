@echo off
setlocal

echo ==================================================
echo   WSL Mount Manager - Build (C++)
echo ==================================================
echo.

:: Set path to Visual Studio
set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"

if not exist "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [ERROR] Visual Studio vcvarsall.bat not found at:
    echo "%VS_PATH%"
    echo Please check if the path is correct.
    pause
    exit /b 1
)

:: Configure environment for x64 compilation
echo [OK] Loading VS Build Tools...
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul

echo context: building...

:: Create output directory
if not exist build mkdir build

:: Compile resources (admin manifest and icons)
rc /nologo /fo build\app.res res\app.rc
if %errorlevel% neq 0 (
    echo [ERROR] Resource compilation failed.
    pause
    exit /b 1
)

:: Main compilation and linking (single line for reliability)
cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W3 /DUNICODE /D_UNICODE /I src /Fo:build\ src\main.cpp src\app.cpp src\wsl_bridge.cpp src\operations.cpp build\app.res /Fe:build\WSLMountManager.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib dwmapi.lib comctl32.lib uxtheme.lib ole32.lib oleaut32.lib crypt32.lib shlwapi.lib comdlg32.lib advapi32.lib uuid.lib

if %errorlevel%==0 (
    echo.
    echo ==================================================
    echo   BUILD SUCCESSFUL!
    echo   Executable: build\WSLMountManager.exe
    echo ==================================================
) else (
    echo.
    echo [ERROR] Build failed! Check errors above.
)
echo.
pause
