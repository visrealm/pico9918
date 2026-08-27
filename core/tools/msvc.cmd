@echo off
rem Run a command with the MSVC toolset on PATH.
rem
rem   core\tools\msvc.cmd core/tools/ci.sh package
rem   core\tools\msvc.cmd test/live/ci.sh
rem
rem Why this exists rather than -G "Visual Studio 17 2022": that generator asks
rem CMake to discover an installation, and on a GitHub runner the discovery
rem returned nothing even though the image ships VS 2022 with the VC toolset.
rem vcvars64 plus Ninja needs no discovery, is version-agnostic - vswhere reports
rem whatever is installed - and builds faster than the VS generator does.
rem
rem The generator is still selectable, so a developer who wants the VS generator
rem sets CI_GENERATOR and gets it. This only fixes the default.

setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=vswhere"

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"

if "%VSPATH%"=="" (
  echo msvc.cmd: vswhere found no installation with the x64 VC toolset. What it sees:
  "%VSWHERE%" -products * -property installationPath
  exit /b 1
)

echo msvc.cmd: %VSPATH%
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

if "%CI_GENERATOR%"=="" set "CI_GENERATOR=Ninja"
rem Ninja picks a compiler off PATH, and would take gcc over cl if both are there
if "%CC%"=="" set "CC=cl"

rem Git Bash by path, NOT whatever `bash` resolves to. On a developer machine
rem that is WSL, which happily runs the whole job under Linux gcc and reports a
rem green MSVC leg - the vcvars above having been set up for nobody.
set "SH=%ProgramFiles%\Git\bin\bash.exe"
if not exist "%SH%" set "SH=%ProgramFiles(x86)%\Git\bin\bash.exe"
if not exist "%SH%" (
  echo msvc.cmd: no Git Bash found. Install Git for Windows, or set SH.
  exit /b 1
)

echo msvc.cmd: %SH%
"%SH%" %*
exit /b %ERRORLEVEL%
