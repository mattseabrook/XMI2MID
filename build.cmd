@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
set "SOURCE=%ROOT%xmi2mid.cpp"
set "OUTPUT=%ROOT%xmi2mid.exe"
set "BUILD_DIR=%TEMP%\xmi2mid-build-%RANDOM%-%RANDOM%"
set "TEMP_OUTPUT=%BUILD_DIR%\xmi2mid.exe"
set "OBJECT=%BUILD_DIR%\xmi2mid.obj"
set "PDB=%BUILD_DIR%\xmi2mid.pdb"

if /i "%~1"=="clean" goto clean
if /i "%~1"=="rebuild" (
    call "%~f0" clean
    if errorlevel 1 exit /b !errorlevel!
    shift /1
)
if not "%~1"=="" if /i not "%~1"=="build" goto usage

if not exist "%SOURCE%" (
    echo Missing source file: "%SOURCE%"
    exit /b 1
)

where cl.exe >nul 2>nul
if errorlevel 1 call :load_vs_2022
if errorlevel 1 exit /b !errorlevel!

mkdir "%BUILD_DIR%" >nul 2>nul
if errorlevel 1 (
    echo Cannot create temporary build directory: "%BUILD_DIR%"
    exit /b 1
)

cl.exe /nologo /std:c++23preview /EHsc /O2 /GL /permissive- /W4 /utf-8 /Zc:__cplusplus /DNDEBUG "%SOURCE%" /Fe:"%TEMP_OUTPUT%" /Fo:"%OBJECT%" /Fd:"%PDB%" /link /LTCG /OPT:REF /OPT:ICF
if errorlevel 1 (
    set "BUILD_RESULT=!errorlevel!"
    rd /s /q "%BUILD_DIR%" 2>nul
    exit /b !BUILD_RESULT!
)

attrib -r "%OUTPUT%" 2>nul
copy /y "%TEMP_OUTPUT%" "%OUTPUT%" >nul
if errorlevel 1 (
    echo Build succeeded, but Windows denied copying the exe to:
    echo "%OUTPUT%"
    echo.
    echo The built exe is still here:
    echo "%TEMP_OUTPUT%"
    exit /b 1
)

rd /s /q "%BUILD_DIR%" 2>nul
echo Built "%OUTPUT%"
exit /b 0

:clean
del /q "%OUTPUT%" "%ROOT%xmi2mid.obj" "%ROOT%xmi2mid.pdb" "%ROOT%vc*.pdb" "%ROOT%*.ilk" 2>nul
echo Cleaned build outputs.
exit /b 0

:usage
echo Usage: build.cmd [clean^|build^|rebuild]
exit /b 1

:load_vs_2022
for %%R in ("%ProgramFiles%\Microsoft Visual Studio\2022" "%ProgramFiles(x86)%\Microsoft Visual Studio\2022") do (
    for %%E in (Community Professional Enterprise BuildTools) do (
        if exist "%%~R\%%E\Common7\Tools\VsDevCmd.bat" (
            call "%%~R\%%E\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo >nul
            exit /b !errorlevel!
        )
    )
)

echo cl.exe was not found. Run from Developer PowerShell for VS 2022, or install the VS 2022 C++ build tools.
exit /b 1
