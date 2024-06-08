# PICO9918

A drop-in replacement for a classic TMS9918A VDP using a Raspberry Pi Pico.

Currently in the early prototyping stages, but looks very promising.

## Hardware

Gerbers will be released soon once I validate the v0.3 PCB. The next stage will be to shrink it down, by including the RP2040 directly, rather than pluggin in an external Pi PIco module.

v0.3 Schematic is available now under (see ) but that revision has not yet been tested.

<p align="left"><a href="pcb/schematic_v0_3.png"><img src="pcb/schematic_v0_3.png" alt="PICO9918 v0.3" width="720px"></a></p>

## Videos

Initial "raw" videos recorded in the moments following the first boot on my TI-99/4A.

These videos are showing the v0.2 hardware with an external Pi Pico providing the required GROMCLK signal to the TI-99. This signal has been added to v0.3. I'm still waiting on v0.3 boards to arrive.

### It freaking works!
[![PICO9918 Prototype - It freaking works](https://img.visualrealmsoftware.com/youtube/thumb/Ri09dCjWxGE)](https://youtu.be/Ri09dCjWxGE)

### Don't mess with Texas!
[![PICO9918 Prototype - Don't mess with Texas](https://img.visualrealmsoftware.com/youtube/thumb/ljNRFKbOGJs)](https://youtu.be/ljNRFKbOGJs)

## License
This code is licensed under the [MIT](https://opensource.org/licenses/MIT "MIT") license
