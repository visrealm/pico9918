# PICO9918 Documentation

[&larr; Back to PICO9918](../README.md)

## Contents

* [FFC Connector](#ffc-connector)
* [CPU and GRM Jumpers](#cpu-and-grm-jumper)

---

## FFC Connector

The FFC (Flat Flexible Cable) connector on the PICO9918 v1.2+ is used to attach accessories such as the VGA or [Digital A/V dongle](../README.md#digital-av-hdmi-dongle-new).

If you're ordering replacement cables from a thirdparty, look for 12 pin 0.5mm pitch FFC cables.

### Connecting the FFC Cable

<p align="left"><img src="img/ffc_0.jpg" alt="FFC connector - open latch" width="480px"></p>

**Step 1** — Open the FFC connector latch by lifting the locking tab upward.

<p align="left"><img src="img/ffc_1.jpg" alt="FFC connector - insert cable" width="480px"></p>

**Step 2** — Insert the FFC cable into the connector with the contacts facing down.

<p align="left"><img src="img/ffc_2.jpg" alt="FFC connector - cable seated" width="480px"></p>

**Step 3** — Push the cable fully into the connector until it seats flush.

<p align="left"><img src="img/ffc_3.jpg" alt="FFC connector - close latch" width="480px"></p>

**Step 4** — Close the latch by pressing the locking tab back down until it clicks.

<p align="left"><img src="img/ffc_4.jpg" alt="FFC connector - secured" width="480px"></p>

**Step 5** — Confirm the cable is secure and the latch is fully closed.

---


## CPU and GRM Jumper

The PICO9918 v1.x boards include solder jumpers for configuring the CPU clock and GRM clock outputs. In some cases, it might be necessary to cut  the CPU jumper to ensure compativility with TMS992xA devices. One example being to use the ColecoVision Expansion module 1 with your ColecoVision.

### Cutting a jumper

The CPUCLK and GROMCLK jumpers are located on the board as shown below.

<p align="left"><img src="img/jumpers_0.jpg" alt="PICO9918 jumpers" width="480px"></p>

Using a sharp hobby knife, position the blade across the jumper trace you wish to cut.

<p align="left"><img src="img/jumpers_1.jpg" alt="Blade positioned against jumper" width="480px"></p>

Apply firm pressure to cut through the trace. Use a multimeter to confirm the jumper is fully open.

<p align="left"><img src="img/jumpers_2.jpg" alt="Cut jumper" width="480px"></p>

### Replacing a jumper

To re-connect a cut jumper, apply a small solder blob onto the pad.

---
