mkdir asm 2> NUL
mkdir bin 2> NUL


:: TI-99
cvbasic --ti994a pico9918tool.bas asm/pico9918tool99.a99 lib
call assm asm/pico9918tool99.a99
if %errorlevel% neq 0 exit /b %errorlevel%
linkticart.py pico9918tool99_b00.bin bin/pico9918tool99_8.bin "PICO9918 CONFIG TOOL"

:: ColecoVision
cvbasic pico9918tool.bas asm/pico9918tool_cv.asm lib
gasm80 asm/pico9918tool_cv.asm -o bin/pico9918tool_cv.rom

:: MSX
cvbasic --msx pico9918tool.bas asm/pico9918tool_msx.asm lib
gasm80 asm/pico9918tool_msx.asm -o bin/pico9918tool_msx.rom
