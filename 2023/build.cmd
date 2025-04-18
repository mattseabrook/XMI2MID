@echo off
set SOURCE=xmi2mid.cpp
set OUTPUT=xmi2mid.exe
set COMPILER=cl

%COMPILER% /EHsc /std:c++17 %SOURCE% /Fe%OUTPUT%

if %errorlevel% neq 0 (
    echo Compilation failed with error %errorlevel%.
    exit /b %errorlevel%
)

echo Compilation successful. Executable created: %OUTPUT%

REM Delete object file
del *.obj