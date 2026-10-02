@echo off
setlocal
REM Convenience wrapper. Set GAME_PATH to your Stray (or other) dump folder first.
REM Example: set GAME_PATH=D:\gamesps5\PPSA07429
cd /d "%~dp0.."
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Trace-Game.ps1" %*
exit /b %ERRORLEVEL%