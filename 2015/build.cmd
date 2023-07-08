@echo off
set SOURCE=main.c
set OUTPUT=xmi2mid.exe
set COMPILER=cl

%COMPILER% /EHsc %SOURCE% /Fe%OUTPUT%

if %errorlevel% neq 0 (
    echo Compilation failed with error %errorlevel%.
    exit /b %errorlevel%
)

echo Compilation successful. Executable created: %OUTPUT%
pause
