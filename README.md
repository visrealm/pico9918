# PICO9918

A drop-in replacement for a classic TMS9918A VDP using a Raspberry Pi Pico.

Currently in the early prototyping stages, but looks very promising on my TI-99/4A and [HBC-56](https://github.com/visrealm/hbc-56) test beds.

The TMS9918A emulation is handled by my [vrEmuTms9918 library](https://github.com/visrealm/vrEmuTms9918) which is included as a submodule here.

## Hardware

Gerbers will be released soon once I validate the v0.3 PCB. The next stage will be to shrink it down, by including the RP2040 directly, rather than pluggin in an external Pi PIco module.

v0.3 Schematic is available now, however this revision has not yet been tested.

<p align="left"><a href="pcb/schematic_v0_3.png"><img src="pcb/schematic_v0_3.png" alt="PICO9918 v0.3" width="720px"></a></p>

### Raspberry Pi Pico Module

Note: Due to GROMCLK and CPUCLK using GPIO23 and GPIO29, a genuine Raspberry Pi Pico can't be used. v0.3 of the PCB is designed for the DWEII? RP2040 USB-C module which exposes these additional GPIOs. A future pico9918 revision will do without an external RP2040 board and use the RP2040 directly.

Purchase links:
 * https://www.amazon.com/RP2040-Board-Type-C-Raspberry-Micropython/dp/B0CG9BY48X
 * https://www.aliexpress.com/item/1005007066733934.html

I could reduce the VGA bit depth to 9-bit or 10-bit to allow the use of a genuine Raspberry Pi Pico board, but given the longer-term plan is to use the RP2040 directly, I've decided to go this way for the prototype.

## Videos

Initial "raw" videos recorded in the moments following the first boot on my TI-99/4A.

These videos are showing the v0.2 hardware with an external Pi Pico providing the required GROMCLK signal to the TI-99. This signal has been added to v0.3. I'm still waiting on v0.3 boards to arrive.

### It freaking works!
[![PICO9918 Prototype - It freaking works](https://img.visualrealmsoftware.com/youtube/thumb/Ri09dCjWxGE)](https://youtu.be/Ri09dCjWxGE)

### Don't mess with Texas!
[![PICO9918 Prototype - Don't mess with Texas](https://img.visualrealmsoftware.com/youtube/thumb/ljNRFKbOGJs)](https://youtu.be/ljNRFKbOGJs)

## License
This code is licensed under the [MIT](https://opensource.org/licenses/MIT "MIT") license
