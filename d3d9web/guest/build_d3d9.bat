@echo off
rem Build the guest-side Direct3D9 drop-in (d3d9_web.cpp) as a 32-bit DLL.
rem
rem Usage:   build_d3d9.bat [variant]
rem
rem Variants (all are #if flags inside the same d3d9_web.cpp):
rem   production       D3D9WEB_TRANSIENT_TEXTURE_RECYCLE=1            <- default; the variant that shipped
rem   plain            no feature flags
rem   guestbatch       D3D9WEB_GUEST_BATCH=1
rem   recompose        D3D9WEB_GUEST_BATCH=1 + D3D9WEB_TRANSIENT_TEXTURE_RECYCLE=1
rem   colorwritetrace  production + D3D9WEB_TRACE_COLORWRITE=1        (diagnostic, never ship)
rem   slottrace        colorwritetrace + D3D9WEB_TRACE_MISSING_SLOT=1 (diagnostic, never ship)
rem
rem Output: out\d3d9_web.<variant>.dll (+ .map).  Copy it next to the game EXE
rem under the name d3d9.dll.  Needs Visual Studio Build Tools (x86 toolset).
setlocal
set "VARIANT=%~1"
if "%VARIANT%"=="" set "VARIANT=production"

set "FLAGS="
if /i "%VARIANT%"=="production"      set "FLAGS=/DD3D9WEB_TRANSIENT_TEXTURE_RECYCLE=1"
if /i "%VARIANT%"=="plain"           set "FLAGS="
if /i "%VARIANT%"=="guestbatch"      set "FLAGS=/DD3D9WEB_GUEST_BATCH=1"
if /i "%VARIANT%"=="recompose"       set "FLAGS=/DD3D9WEB_GUEST_BATCH=1 /DD3D9WEB_TRANSIENT_TEXTURE_RECYCLE=1"
if /i "%VARIANT%"=="colorwritetrace" set "FLAGS=/DD3D9WEB_TRANSIENT_TEXTURE_RECYCLE=1 /DD3D9WEB_TRACE_COLORWRITE=1"
if /i "%VARIANT%"=="slottrace"       set "FLAGS=/DD3D9WEB_TRANSIENT_TEXTURE_RECYCLE=1 /DD3D9WEB_TRACE_COLORWRITE=1 /DD3D9WEB_TRACE_MISSING_SLOT=1"
for %%V in (production plain guestbatch recompose colorwritetrace slottrace) do if /i "%%V"=="%VARIANT%" set "KNOWN=1"
if not defined KNOWN (
  echo [ERROR] unknown variant "%VARIANT%"
  exit /b 2
)

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
  echo [ERROR] Visual Studio Build Tools not found
  exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 (
  echo [ERROR] vcvars32 failed
  exit /b 1
)

cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /utf-8 /LD /O2 /Oi- /MT /GS- /W3 %FLAGS% /Fo"out\d3d9_web.%VARIANT%.obj" d3d9_web.cpp ^
  /link /DLL /NODEFAULTLIB /ENTRY:DllMain /OUT:"out\d3d9_web.%VARIANT%.dll" ^
  /MAP:"out\d3d9_web.%VARIANT%.map" /MAPINFO:EXPORTS ^
  /DEF:d3d9.def kernel32.lib user32.lib
if errorlevel 1 exit /b 1

echo [OK] out\d3d9_web.%VARIANT%.dll  (variant=%VARIANT% flags=%FLAGS%)
endlocal
