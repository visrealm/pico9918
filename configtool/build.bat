cvbasic --ti994a pico9918tool.bas pico9918tool.a99 lib
xas99.py -b -R pico9918tool.a99
linkticart.py pico9918tool.bin pico9918tool_8.bin "PICO9918 TOOL"

cvbasic pico9918tool.bas pico9918tool.asm lib
gasm80 pico9918tool.asm -o pico9918tool.rom -l pico9918tool.lst
