@echo off
rem Bypass PowerShell execution policy for the unsigned install script.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
exit /b %ERRORLEVEL%
