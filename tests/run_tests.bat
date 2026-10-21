@echo off
rem ============================================================
rem Build and run the engine-core tests (qmake + nmake, no CMake).
rem
rem RUN IT FROM A NORMAL COMMAND PROMPT:
rem     cd /d <repo>\tests
rem     run_tests.bat
rem
rem It calls vcvars64.bat itself, so the prompt does not need to be a
rem "Developer Command Prompt".
rem
rem SHADOW BUILD: the makefile, objects and bp_tests.exe all land in
rem <repo>\build\tests, NOT next to the sources. The project folder is
rem meant to hold sources and resources only -- no build leftovers.
rem
rem Keep this file ASCII-only: cmd.exe reads .bat in the system ANSI
rem codepage, so non-ASCII text here can break parsing.
rem
rem EXIT CODE NOTES (both bit me while writing this):
rem   1. "set RC=%ERRORLEVEL%" or "exit /b %ERRORLEVEL%" inside a
rem      parenthesised block expands at parse time -> the test result is
rem      lost and failures look like success.
rem      So: no variables, no goto, and crucially NO parenthesised block
rem      around the program call.
rem   2. Running bp_tests.exe by hand needs Qt's bin on PATH, otherwise
rem      Windows reports 0xC0000135 (Qt6Core.dll not found).
rem ============================================================

setlocal
cd /d "%~dp0"

rem --- locate Qt and MSVC without hardcoding this machine's paths ---
rem   Optional local override, NOT in version control: <repo>\qt-env.bat
rem       content:  set "QTDIR=C:\Qt\6.10.2\msvc2022_64"
if exist "%~dp0..\qt-env.bat" call "%~dp0..\qt-env.bat"
rem   Qt   : set QTDIR (e.g. C:\Qt\6.10.2\msvc2022_64) or edit QT_BIN below
rem   MSVC : found via vswhere; set VCVARS to force a specific VS install
if not defined QT_BIN if defined QTDIR set "QT_BIN=%QTDIR%\bin"
if not defined QT_BIN for %%d in (C: D: E:) do if not defined QT_BIN if exist "%%d\Qt" for /d %%q in ("%%d\Qt\6.*") do if not defined QT_BIN if exist "%%q\msvc2022_64\bin\qmake.exe" set "QT_BIN=%%q\msvc2022_64\bin"
if not defined QT_BIN (
    echo [ERROR] Qt not found. Set QTDIR to your Qt msvc kit ^(e.g. C:\Qt\6.10.2\msvc2022_64^).
    exit /b 1
)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
rem NOTE: do not put this inside a parenthesised block. "%ProgramFiles(x86)%" expands to
rem "Program Files (x86)", and that ")" would close the block early -- classic cmd trap.
if exist "%VSWHERE%" if not defined VCVARS for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"

rem <repo>\build\tests  (this script sits in <repo>\tests)
set BUILD_DIR=%~dp0..\build\tests

if not exist "%QT_BIN%\qmake.exe" (
    echo [ERROR] qmake not found: %QT_BIN%\qmake.exe
    echo         Edit QT_BIN in this script.
    exit /b 1
)

if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found: %VCVARS%
    echo         Edit VCVARS in this script.
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%BUILD_DIR%" (
    echo [ERROR] cannot create the build directory: %BUILD_DIR%
    exit /b 1
)

echo [0/4] Setting up MSVC environment ...
call "%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat failed
    exit /b 1
)

pushd "%BUILD_DIR%"

echo [1/4] Running qmake (shadow build into %BUILD_DIR%) ...
"%QT_BIN%\qmake.exe" "%~dp0tests.pro"
if errorlevel 1 (
    echo [ERROR] qmake failed
    popd
    exit /b 1
)

echo [2/4] Compiling ...
nmake /NOLOGO
if errorlevel 1 (
    echo [ERROR] build failed
    popd
    exit /b 1
)

rem Put Qt's DLL directory on PATH so the freshly built exe can start.
set PATH=%QT_BIN%;%PATH%

rem Pick the produced executable WITHOUT a parenthesised block.
set TEST_EXE=
if exist "release\bp_tests.exe" set TEST_EXE=release\bp_tests.exe
if not defined TEST_EXE if exist "debug\bp_tests.exe" set TEST_EXE=debug\bp_tests.exe
if not defined TEST_EXE if exist "bp_tests.exe" set TEST_EXE=bp_tests.exe
if not defined TEST_EXE (
    echo [ERROR] bp_tests.exe was not produced
    popd
    exit /b 1
)

echo [3/4] Running %TEST_EXE% ...
echo.

"%TEST_EXE%"
set TEST_RC=%ERRORLEVEL%

echo.
if "%TEST_RC%"=="0" echo [OK] all engine-core tests passed
if not "%TEST_RC%"=="0" echo [FAILED] engine-core tests failed, exit code %TEST_RC%

popd
exit /b %TEST_RC%
