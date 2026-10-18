@echo off
setlocal

dumpbin /exports 10_eldendlc.dll > exports.txt
if errorlevel 1 (echo dumpbin failed & exit /b 1)

findstr /C:"UCO_PluginInit" exports.txt >nul
if errorlevel 1 (echo FAIL: UCO_PluginInit not exported & exit /b 1)

findstr /C:"UCO_PluginShutdown" exports.txt >nul
if errorlevel 1 (echo FAIL: UCO_PluginShutdown not exported & exit /b 1)

dumpbin /headers 10_eldendlc.dll | findstr /C:"machine (x64)" >nul
if errorlevel 1 (echo FAIL: not a 64-bit DLL & exit /b 1)

for %%A in (10_eldendlc.dll) do if %%~zA LSS 10240 (echo FAIL: suspiciously small & exit /b 1)

echo ALL CHECKS PASSED
exit /b 0
