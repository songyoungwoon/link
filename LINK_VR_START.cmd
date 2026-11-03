@echo off
setlocal
if not exist "%~dp0build\vr\Release\LINK.exe" (
  echo Build LINK VR first: powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-vr.ps1" -Test
  pause
  exit /b 1
)
start "" "%~dp0build\vr\Release\LINK.exe" --vr
