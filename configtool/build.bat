setlocal
@echo off

mkdir build 2> NUL
mkdir build\asm 2> NUL
mkdir build\bin 2> NUL

python3.13 tools\uf2cvb.py -b 8 -o src\firmware_8k pico9918-vga-build-v1-2.uf2
python3.13 tools\uf2cvb.py -b 16 -o src\firmware_16k pico9918-vga-build-v1-2.uf2

if %errorlevel% neq 0 exit /b %errorlevel%

pushd build

set PATH=..\tools\cvbasic;%PATH%
set LIBPATH=..\src\lib


del *.bas
copy /Y ..\src\*.bas .
copy /Y ..\src\lib lib


:: TI-99
cvbasic --ti994a pico9918conf-ti99.bas asm\pico9918tool_ti99.a99 %LIBPATH%
python3.13 c:\tools\xdt99\xas99.py -b -R asm/pico9918tool_ti99.a99
if %errorlevel% neq 0 exit /b %errorlevel%
linkticart.py pico9918tool_ti99_b00.bin bin\pico9918tool_ti99_8.bin "PICO9918 CONFIG"
copy /Y bin\pico9918tool_ti99_8.bin c:\tools\Classic99

:: ColecoVision
cvbasic pico9918conf.bas asm\pico9918tool_cv.asm %LIBPATH%
gasm80 asm\pico9918tool_cv.asm -o bin\pico9918tool_cv.rom
copy /Y bin\pico9918tool_cv.rom c:\tools\Classic99Phoenix

:: MSX
cvbasic --msx pico9918conf.bas asm\pico9918tool_msx.asm %LIBPATH%
gasm80 asm\pico9918tool_msx.asm -o bin\pico9918tool_msx.rom

:: CreatiVision
::cvbasic --creativision pico9918conf-nobank.bas asm\pico9918tool_crv.asm %LIBPATH%
    

:: HBC56
::cvbasic --hbc56 pico9918conf-nobank.bas asm\pico9918tool_hbc56.asm %LIBPATH%
::gasm80 asm\pico9918tool_hbc56.asm -o bin\pico9918tool_hbc56.rom

popd