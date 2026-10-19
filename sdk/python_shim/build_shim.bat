@echo off
REM ============================================================================
REM  Build bp_python_shim.dll (the Python loader for native mods).
REM
REM  Usage: double-click this file, or run build_shim.bat from a terminal.
REM  Output: bp_python_shim.dll in this folder.
REM
REM  Notes (all learned the hard way):
REM    /LD      build a DLL
REM    /MD      same C runtime as the host game (see ABI contract 3.2.1)
REM    /I..     find sdk\bp_mod_api.h (this folder is sdk\python_shim)
REM    /TC      compile as C even if the extension says otherwise
REM    /utf-8   the source carries Chinese comments. WITHOUT it MSVC decodes the
REM             file in the local code page, a Chinese char can eat the closing
REM             quote of a string literal, and the errors look unrelated (C2055
REM             "expected a formal parameter list" was exactly this).
REM
REM  The shim does NOT link against CPython: it loads python3XX.dll at runtime.
REM  So you do NOT need the Python headers or an import library to build it.
REM
REM  Keep THIS FILE ASCII-only: cmd.exe reads .bat in the OEM code page.
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

echo [2/3] compiling bp_python_shim.c into bp_python_shim.dll ...
cl /nologo /LD /MD /TC /utf-8 /W3 /I".." bp_python_shim.c /link advapi32.lib /OUT:bp_python_shim.dll
if errorlevel 1 ( echo [ERROR] compile failed & exit /b 1 )

echo [3/3] cleaning intermediate files ...
del /q bp_python_shim.obj bp_python_shim.exp bp_python_shim.lib 2>nul

echo.
echo Done: %CD%\bp_python_shim.dll
echo Next: copy it into a Python mod folder as ^<modid^>.dll (see sdk\samples\hello_python).
endlocal
