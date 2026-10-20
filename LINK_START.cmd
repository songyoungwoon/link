@echo off
setlocal
if not exist "%~dp0build\native\Release\LINK.exe" (
  echo Build LINK first: powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-native.ps1" -Test
  pause
  exit /b 1
)
start "" "%~dp0build\native\Release\LINK.exe"
