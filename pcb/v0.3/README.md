## PICO9918 v0.3 (2024-06-07)

This is the first "public" version and is powered by a piggy-backed USB-C Pi Pico module. I have never produced or sold this version beyond the initial few prototypes.

<p align="left"><img src="../../img/pico9918_v0_3_sm.jpg" alt="PICO9918 v0.3" width="720px"></p>


### Schematic

<p align="left"><img src="pico9918_v0_3_schematic.png" alt="PICO9918 v0.3 Schematic" width="720px"></p>


### PCB

<p align="left"><img src="pico9918_v0_3_pcb.png" alt="PICO9918 v0.3 PCB" width="720px"></p>

* [PICO9918 v0.3 Gerber](pico9918_v0_3_gerber.zip)
* [PICO9918 v0.3 BOM](pico9918_v0_3_bom.xlsx)
* [PICO9918 v0.3 Pick and place](pico9918_v0_3_picknplace.csv)

### Changelog
- Switched to use USB-C Pi Pico module instead of genuine Pi Pico.
- Added CPUCLK and GROMCLK.

### PCB v0.3 Notes

There are a number of 0 Ohm resistors (jumpers). You may need to omit the RST resistor. On some machines, the extra time is required to bootstrap the Pico. This will be changed to a soft reset on v0.4.

### Raspberry Pi Pico Module

Note: Due to GROMCLK and CPUCLK using GPIO23 and GPIO29, a genuine Raspberry Pi Pico can't be used. v0.3 of the PCB is designed for the DWEII? RP2040 USB-C module which exposes these additional GPIOs. A future pico9918 revision will do without an external RP2040 board and use the RP2040 directly.

Purchase links:
 * https://www.amazon.com/RP2040-Board-Type-C-Raspberry-Micropython/dp/B0CG9BY48X
 * https://www.aliexpress.com/item/1005007066733934.html

I could reduce the VGA bit depth to 9-bit or 10-bit to allow the use of a genuine Raspberry Pi Pico board, but given the longer-term plan is to use the RP2040 directly, I've decided to go this way for the prototype.
