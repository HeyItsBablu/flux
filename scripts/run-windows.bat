@echo off
cd %~dp0\..

:: Find and activate the MSVC environment
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Is Visual Studio installed?
    pause
    exit /b 1
)

:: Get the VS install path
for /f "usebackq tokens=*" %%i in (
    `"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`
) do set "VS_PATH=%%i"

if "%VS_PATH%"=="" (
    echo ERROR: No Visual Studio installation with C++ tools found.
    pause
    exit /b 1
)

:: Activate the x64 MSVC environment
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"

cmake -S . -B build-windows -G Ninja ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DFLUX_BUILD_EXAMPLES=ON ^
    -DCMAKE_C_COMPILER=cl.exe ^
    -DCMAKE_CXX_COMPILER=cl.exe

cmake --build build-windows

build-windows\lib\flux_app.exe