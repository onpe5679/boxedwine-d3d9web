@echo off
reg add "HKCU\Software\Wine\DllOverrides" /v d3d9 /d native /f
d3d9_com_smoke_client.exe
echo D3D9WEB_PHASE4_EXITCODE=%ERRORLEVEL%
