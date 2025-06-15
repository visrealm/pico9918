setlocal
@echo off

echo ---------------------------
echo PICO9918 Configurator build
echo ---------------------------
echo.

echo Setting up build directories
echo.
echo Intermediates: build\tmp\asm
echo Binaries:      build
echo.

mkdir build 2> NUL
mkdir build\tmp 2> NUL
mkdir build\tmp\asm 2> NUL

del /Q /S build\*

set VERSION=v1-0-2
set FRIENDLYVER=%VERSION:-=.%

if exist *.uf2 del /Q *.uf2

for %%D in ("..\build\src\pico9918-vga-build-%VERSION%.uf2") do set FIRMWARE_FILE=%%~fD

echo Copying source firmware from %FIRMWARE_FILE%
echo.

if not exist %FIRMWARE_FILE% (
    echo %FIRMWARE_FILE% not found
    exit /b 1
)

copy /Y %FIRMWARE_FILE% .

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

echo.
echo NO BANKS:
echo ---------------
python3 tools\uf2cvb.py -b 0 -o src\firmware pico9918-vga-build-%VERSION%.uf2
if %errorlevel% neq 0 exit /b %errorlevel%

pushd build\tmp

echo.
echo ---------------------------------------------------------------------
echo Finding CVBasic compiler...


set PATH=..\..\tools\cvbasic;%PATH%

:: This is where I have my CVBasic fork, so grab it from here if available
set PATH=..\..\..\..\CVBasic\build\Release;%PATH%  
set LIBPATH=..\..\src\lib
for %%D in ("%LIBPATH%") do set LIBPATH=%%~fD

where cvbasic.exe
if %errorlevel% neq 0 (
    echo.
    echo cvbasic.exe not in %%PATH%%
    echo.
    echo %%PATH%%="%PATH%"
    exit /b %errorlevel%
)
for /f "tokens=1 delims=" %%A in ('where cvbasic.exe') do (
    echo.
    echo Using : %%A
    goto :end
)
:end
echo ---------------------------------------------------------------------


echo.
echo ---------------------------------------------------------------------
echo   Copying source files to %CD%
echo ---------------------------------------------------------------------


del *.bas
copy /Y ..\..\src\*.bas .
copy /Y ..\..\src\lib lib

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
linkticart.py %BASENAME%_b00.bin ..\%BASENAME%_8.bin "PICO9918 %FRIENDLYVER%"
echo Output: build\%BASENAME%_8.bin

echo.
echo ---------------------------------------------------------------------
echo   Compiling for TI-99/4A Emulator (F18A testing mode)
echo ---------------------------------------------------------------------

set BASENAME=pico9918_%VERSION%_ti99_f18a
cvbasic --ti994a -dF18A_TESTING=1 pico9918conf.bas asm\%BASENAME%.a99 %LIBPATH%
if %errorlevel% neq 0 exit /b %errorlevel%
python3.13 c:\tools\xdt99\xas99.py -b -R asm/%BASENAME%.a99 -L ..\%BASENAME%.lst
if %errorlevel% neq 0 exit /b %errorlevel%
linkticart.py %BASENAME%_b00.bin ..\%BASENAME%_8.bin "PICO9918 %FRIENDLYVER%"
echo Output: build\%BASENAME%_8.bin


:: ColecoVision

echo.
echo ---------------------------------------------------------------------
echo   Compiling for Colecovision
echo ---------------------------------------------------------------------

set BASENAME=pico9918_%VERSION%_cv
cvbasic pico9918conf.bas asm/%BASENAME%.asm %LIBPATH%
if %errorlevel% neq 0 exit /b %errorlevel%
gasm80 asm\%BASENAME%.asm -o ..\%BASENAME%.rom
copy /Y ..\%BASENAME%.rom c:\tools\Classic99Phoenix
echo.
echo Output: build\%BASENAME%.rom


:: MSX

echo.
echo ---------------------------------------------------------------------
echo   Compiling for MSX
echo ---------------------------------------------------------------------

set BASENAME=pico9918_%VERSION%_msx_asc16
cvbasic --msx pico9918conf.bas asm/%BASENAME%.asm %LIBPATH%
if %errorlevel% neq 0 exit /b %errorlevel%
gasm80 asm\%BASENAME%.asm -o ..\%BASENAME%.rom
echo Output: build\%BASENAME%.rom

set BASENAME=pico9918_%VERSION%_msx_konami
cvbasic --msx -konami pico9918conf.bas asm/%BASENAME%.asm %LIBPATH%
if %errorlevel% neq 0 exit /b %errorlevel%
gasm80 asm\%BASENAME%.asm -o ..\%BASENAME%.rom
echo Output: build\%BASENAME%.rom


:: NABU

echo.
echo ---------------------------------------------------------------------
echo   Compiling for NABU
echo ---------------------------------------------------------------------

set BASENAME=pico9918_%VERSION%
cvbasic --nabu pico9918conf.bas asm/%BASENAME%_nabu.asm %LIBPATH%
if %errorlevel% neq 0 exit /b %errorlevel%
gasm80 asm\%BASENAME%_nabu.asm -o ..\%BASENAME%.nabu
echo Output: build\%BASENAME%.nabu

echo.
echo   Compiling for NABU (MAME)

:: this is a different version as it is designed to allow running on a TMS99xxA
:: so don't be tempted to copy the .nabu file from above
set BASENAME=pico9918_%VERSION%_nabu_mame
cvbasic --nabu -DTMS9918_TESTING=1 pico9918conf.bas asm/%BASENAME%.asm %LIBPATH%
if %errorlevel% neq 0 exit /b %errorlevel%
gasm80 asm\%BASENAME%.asm -o ..\000001.nabu
pushd ..
tar.exe -a -c -f %BASENAME%.zip 000001.nabu
copy /Y %BASENAME%.zip %BASENAME%.npz
del %BASENAME%.zip
del 000001.nabu
popd
echo Output: build\%BASENAME%.npz


:: CreatiVision

echo.
echo ---------------------------------------------------------------------
echo   Compiling for CreatiVision
echo ---------------------------------------------------------------------

set BASENAME=pico9918_%VERSION%_crv
cvbasic --creativision pico9918conf.bas asm\%BASENAME%.asm %LIBPATH%
if %errorlevel% neq 0 exit /b %errorlevel%
gasm80 asm\%BASENAME%.asm -o ..\%BASENAME%.bin
echo Output: build\%BASENAME%.bin
    

:: HBC56
::cvbasic --hbc56 pico9918conf-nobank.bas asm\pico9918tool_hbc56.asm %LIBPATH%
::gasm80 asm\pico9918tool_hbc56.asm -o bin\pico9918tool_hbc56.rom

popd
echo.