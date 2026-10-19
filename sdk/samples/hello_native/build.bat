@echo off
REM ============================================================================
REM  Build the hello_native sample mod (no Visual Studio project needed).
REM
REM  Usage: double-click this file, or run build.bat from a terminal.
REM  Output: hello_native.dll in this folder.
REM
REM  NOTE: this file is intentionally plain ASCII. A .bat with non-ASCII text is
REM        decoded by cmd.exe using the OEM code page, which turns the text into
REM        mojibake and can break the script itself. Keep .bat files ASCII.
REM
REM  What the switches mean (this doubles as "how to build a native mod"):
REM    /LD      build a DLL (not an exe)
REM    /MD      same C runtime as the host (see ABI contract 3.2.1)
REM    /utf-8   source files are UTF-8. **Required if your source has non-ASCII
REM             text**: without it MSVC reads the file in the local code page and
REM             a Chinese character can end up escaping the closing quote,
REM             producing the confusing "newline in string literal" error.
REM    /I..\..  find sdk\bp_mod_api.h
REM    /EHsc    standard C++ exceptions inside the mod; never let them cross
REM             the ABI boundary - catch them in your own function instead.
REM ============================================================================
setlocal

set "VCVARS=D:\Mygrammer\aboutcpp\grammer\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found: %VCVARS%
    echo         Edit the VCVARS line above if MSVC lives somewhere else.
    exit /b 1
)

echo [1/3] setting up the MSVC environment ...
call "%VCVARS%" >nul
if errorlevel 1 ( echo [ERROR] vcvars64 failed & exit /b 1 )

echo [2/3] compiling hello_native.cpp into hello_native.dll ...
cl /nologo /LD /MD /std:c++17 /utf-8 /EHsc /W3 /I"..\.." hello_native.cpp /link /OUT:hello_native.dll
if errorlevel 1 ( echo [ERROR] compile failed & exit /b 1 )

echo [3/3] cleaning intermediate files ...
del /q hello_native.obj hello_native.exp hello_native.lib 2>nul

echo.
echo Done: %CD%\hello_native.dll
echo Next: enable "allow native mods" in the game, then enable this mod.
endlocal
