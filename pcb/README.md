# PICO9918 PCBs

Here you will find schematics and gerbers for all working revisions of the PICO9918.

## [v1.1 (2024-09-12)](v1.1)

### Changelog
- Removed reset button.
- Default CPUCLK and GROMCLK jumper pads to closed.
- Minor positioning adjustments of bottom-side resistors to clear header pins.

## [v1.0 (2024-08-01)](v1.0)

First version available for sale on Tindie and ArcadeShopper

<p align="left"><a href="../img/pico9918_v1_0_sm.png"><img src="../img/pico9918_v1_0_sm.png" alt="PICO9918 v1.0" width="720px"></a></p>

### Changelog
- Added reset button.
- Moved CPUCLK and GROMCLK jumper pads to the top of the board.
- Added SWD and SWC jumper pads to allow SWD (debugging)
- Flipped top labels and graphics so it doesn't look upside-down.

## [v0.4 (2024-07-16)](v0.4)

This is the first fully-integrated single-board version and is powered by an RP2040 directly. This version was never released, however I hand-built 9 of them and sent most of the to various retro enthusiasts from AtariAge.

<p align="left"><a href="../img/pico9918_v0_4_sm.png"><img src="../img/pico9918_v0_4_sm.png" alt="PICO9918 v0.4" width="720px"></a></p>

### Changelog
- Removed piggy backed Pi Pico module.
- Added RP2040 and all of its dependencies.
- Switched out resisitor networks for discrete resistors.
- Shrinkified the package to something resembling the first version for sale (v1.0).
- Updated VGA connector to 6p 1.25mm JST.

## [v0.3 (2024-06-07)](v0.3)

This is the first "public" version and is powered by a piggy-backed USB-C Pi Pico module. I have never produced or sold this version beyond the initial few prototypes.

<p align="left"><img src="../img/pico9918_v0_3_sm.jpg" alt="PICO9918 v0.3" width="720px"></p>

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


## What happened to v0.1 and v0.2?

For the curious amongst you, v0.1 was the only version that wan't usable. The TMS9918A socket interface was 0.1" too narrow. Rookie error! Fortunately, I noticed within hours of ordering the PCBs, so ordered v0.2 long before v0.1 arrived.

v0.2 was very usable and is the version used in the initial "It freaking works!" video. The only reason it isn't published is because it lacked the GROMCLK and CPUCLK signals required for many systems. In the video, you can see the GROMCLK signal is being provided by a second Pi Pico.