@echo off
REM Build Palworld Hardcore Restorer (native, single self-contained .exe).
REM Run from a "x64 Native Tools Command Prompt for VS", or this script will
REM try to locate and load the VS build environment automatically.

setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if errorlevel 1 (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
  if defined VSINSTALL call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
)
where cl >nul 2>nul
if errorlevel 1 (
  echo ERROR: MSVC compiler ^(cl.exe^) not found. Open a "x64 Native Tools Command Prompt for VS" and run build.bat again.
  exit /b 1
)

if not exist build mkdir build
if not exist dist  mkdir dist

cl /nologo /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W3 ^
   /I src\ooz /Fobuild\ ^
   src\main.cpp src\palsave.cpp ^
   src\ooz\kraken.cpp src\ooz\bitknit.cpp src\ooz\lzna.cpp src\ooz\stdafx.cpp ^
   src\miniz.c ^
   /Fedist\PalworldHardcoreRestorer.exe ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED

if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
echo.
echo Built: dist\PalworldHardcoreRestorer.exe
