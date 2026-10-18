@echo off
setlocal
cl /nologo /LD /MT /O2 /EHsc /std:c++17 eldendlc.cpp /link /OUT:10_eldendlc.dll
if errorlevel 1 exit /b 1
echo Built 10_eldendlc.dll
