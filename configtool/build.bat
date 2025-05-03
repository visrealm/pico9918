setlocal
@echo off

mkdir build 2> NUL
mkdir build\asm 2> NUL
mkdir build\bin 2> NUL

set VERSION=v1-0-0
set FRIENDLYVER=%VERSION:-=.%

echo.
echo ---------------------------------------------------------------------
echo   Generating firmware source file from pico9918-vga-build-%VERSION%.uf2
echo ---------------------------------------------------------------------
echo.
echo 8KB BANK SIZE:
echo --------------
python3 tools\uf2cvb.py -b 8 -o src\firmware_8k pico9918-vga-build-%VERSION%.uf2

echo.
echo 16KB BANK SIZE:
echo ---------------
python3 tools\uf2cvb.py -b 16 -o src\firmware_16k pico9918-vga-build-%VERSION%.uf2
if %errorlevel% neq 0 exit /b %errorlevel%

pushd build

echo.
echo ---------------------------------------------------------------------
echo   Copying source files to %CD%
echo ---------------------------------------------------------------------


set PATH=..\tools\cvbasic;%PATH%
set PATH=..\..\..\CVBasic\build\Release;%PATH%
set LIBPATH=..\src\lib
for %%D in ("%LIBPATH%") do set LIBPATH=%%~fD

del *.bas
copy /Y ..\src\*.bas .
copy /Y ..\src\lib lib

echo.
echo ---------------------------------------------------------------------
for /f "tokens=1 delims=" %%A in ('where cvbasic.exe') do (
    echo Using CVBasic from : %%A
    goto :end
)
:end
echo ---------------------------------------------------------------------


:: TI-99

echo.
echo ---------------------------------------------------------------------
echo   Compiling for TI-99/4A
echo ---------------------------------------------------------------------

set BASENAME=pico9918_%VERSION%_ti99
cvbasic --ti994a pico9918conf.bas asm\%BASENAME%.a99 %LIBPATH%
if %errorlevel% neq 0 exit /b %errorlevel%
python3.13 c:\tools\xdt99\xas99.py -b -R asm/%BASENAME%.a99
if %errorlevel% neq 0 exit /b %errorlevel%
linkticart.py %BASENAME%_b00.bin bin\%BASENAME%_8.bin "PICO9918 %FRIENDLYVER%"
echo Output: bin\%BASENAME%_8.bin

echo.
echo ---------------------------------------------------------------------
echo   Compiling for TI-99/4A Emulator (F18A testing mode)
echo ---------------------------------------------------------------------

set BASENAME=pico9918_%VERSION%_ti99_f18a
cvbasic --ti994a -dF18A_TESTING=1 pico9918conf.bas asm\%BASENAME%.a99 %LIBPATH%
if %errorlevel% neq 0 exit /b %errorlevel%
python3.13 c:\tools\xdt99\xas99.py -b -R asm/%BASENAME%.a99
if %errorlevel% neq 0 exit /b %errorlevel%
linkticart.py %BASENAME%_b00.bin bin\%BASENAME%_8.bin "PICO9918 %FRIENDLYVER%"
echo Output: bin\%BASENAME%_8.bin


:: ColecoVision

echo.
echo ---------------------------------------------------------------------
echo   Compiling for Colecovision
echo ---------------------------------------------------------------------

set BASENAME=pico9918_%VERSION%_cv
cvbasic pico9918conf.bas asm/%BASENAME%.asm %LIBPATH%
if %errorlevel% neq 0 exit /b %errorlevel%
gasm80 asm\%BASENAME%.asm -o bin\%BASENAME%.rom
copy /Y bin\%BASENAME%.rom c:\tools\Classic99Phoenix
echo.
echo Output: bin\%BASENAME%.rom


:: MSX

echo.
echo ---------------------------------------------------------------------
echo   Compiling for MSX
echo ---------------------------------------------------------------------

set BASENAME=pico9918_%VERSION%_msx
cvbasic --msx pico9918conf.bas asm/%BASENAME%.asm %LIBPATH%
if %errorlevel% neq 0 exit /b %errorlevel%
gasm80 asm\%BASENAME%.asm -o bin\%BASENAME%.rom
echo Output: bin\%BASENAME%.rom

:: CreatiVision
::cvbasic --creativision pico9918conf-nobank.bas asm\pico9918tool_crv.asm %LIBPATH%
    

:: HBC56
::cvbasic --hbc56 pico9918conf-nobank.bas asm\pico9918tool_hbc56.asm %LIBPATH%
::gasm80 asm\pico9918tool_hbc56.asm -o bin\pico9918tool_hbc56.rom

popd
echo.