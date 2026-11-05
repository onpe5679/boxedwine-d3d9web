@echo off
rem Build the current x86 argument probe with periodic Present checkpoints.
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
  echo [ERROR] Visual Studio not found
  exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 (
  echo [ERROR] vcvars32 failed
  exit /b 1
)

cd /d "%~dp0"
cl /nologo /utf-8 /LD /O2 /MT /GS- /W3 d3d9proxy.cpp ^
  /link /DLL /OUT:d3d9_flushprobe.dll /DEF:d3d9.def kernel32.lib user32.lib
if errorlevel 1 (
  echo [ERROR] build failed
  exit /b 1
)

echo [OK] d3d9_flushprobe.dll built
echo Deploy beside Azrok.exe under the name d3d9.dll.
endlocal
