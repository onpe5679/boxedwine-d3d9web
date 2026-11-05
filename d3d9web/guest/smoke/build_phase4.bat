@echo off
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
cl /nologo /utf-8 /LD /O2 /MT /GS- /W3 d3d9_com_smoke.cpp ^
  /link /DLL /NODEFAULTLIB /ENTRY:DllMain /OUT:d3d9_com_smoke.dll ^
  /DEF:..\d3d9.def kernel32.lib user32.lib
if errorlevel 1 exit /b 1

cl /nologo /utf-8 /O2 /MT /GS- /W3 d3d9_com_smoke_client.cpp ^
  /link /OUT:d3d9_com_smoke_client.exe kernel32.lib user32.lib
if errorlevel 1 exit /b 1

echo [OK] phase4 x86 DLL and client built
endlocal
