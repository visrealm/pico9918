## TI-99/4A no-cut mod

The no-cut mod for the TI-99/4A consists of a custom PCB which replaces the original A/V DIN socket:

![ti99 no cut](./ti99/img/nocut-ti99-installed-pcb.jpg)

The PCB is the supported by the printed enclosure:

![ti99 no cut](./ti99/img/nocut-ti99-installed-enclosure.jpg)

### PCB

There is now a single PCB (v1.3) for both JST and FFC connector types.

#### Configuration

The 5P/6P jumper is used to connect the correct DIN pin to the ground plane of the no-cut PCB. For 5-pin DINs (most US models), place a jumper between the 5P and middle pin. For 6-pin DINs (most EU/AUS models), place a jumper between the 6P and middle pin.

See [ti99/pcb/](ti99/pcb/)

To install the PCB, first remove the A/V connector from your TI-99/4A. Then install using the 3D printed spacer: [stl/pico9918-nocut-ti99-pcb-spacer.stl](stl/pico9918-nocut-ti99-pcb-spacer.stl)

### Enclosure

The vent holes on the black versus beige TI-99/4As are slightly different. For that reason, find either the beige or black version of the enclosure top and the generic enclosure bottom. They should be printed like this:

![ti99 no cut print layout](./ti99/img/pico9918-nocut-ti99-build-plate.png)

The case is held together with 4x screws. The screw hole diameter is 2.4mm (3/32"). I use 4G x 3/8 self-tapping screws, but there is some flexibility in screw sizes.

See [ti99/stl/](ti99/stl/)
