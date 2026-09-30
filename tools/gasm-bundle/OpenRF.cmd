@echo off
rem OpenRF @VERSION@ on gasm-run @GASM_VERSION@. Double-click to play; OpenRF.cmd --help for the options.
rem The work is done by openrf.ps1 (CD picker, saved CD location, gasm-run command line).
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0openrf.ps1" %*
set rc=%errorlevel%
rem Keep the window open on errors when started by double-click (OPENRF_NO_PAUSE=1 skips this).
if not "%rc%"=="0" if not "%OPENRF_NO_PAUSE%"=="1" pause
exit /b %rc%
