/*
 * Project: pico9918 - palette
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 *
 */

#include "pico/stdlib.h"

#define TMSPAL00 0x00000000 /* transparent*/
#define TMSPAL01 0x000000ff /* black */
#define TMSPAL02 0x21c942ff /* medium green */
#define TMSPAL03 0x5edc78ff /* light green */
#define TMSPAL04 0x5455edff /* dark blue */
#define TMSPAL05 0x7d75fcff /* light blue */
#define TMSPAL06 0xd3524dff /* dark red */
#define TMSPAL07 0x43ebf6ff /* cyan */
#define TMSPAL08 0xfd5554ff /* medium red */
#define TMSPAL09 0xff7978ff /* light red */
#define TMSPAL10 0xd3c153ff /* dark yellow */
#define TMSPAL11 0xe5ce80ff /* light yellow */
#define TMSPAL12 0x21b03cff /* dark green */
#define TMSPAL13 0xc95bbaff /* magenta */
#define TMSPAL14 0xccccccff /* grey */
#define TMSPAL15 0xffffffff /* white */


 /* macros to convert from RGBA32 to BGR12 */
#define RFROMRGBA(rgba) ((rgba >> 24) & 0xff)
#define GFROMRGBA(rgba) ((rgba >> 16) & 0xff)
#define BFROMRGBA(rgba) ((rgba >> 8) & 0xff)
#define C4FROMC8(c4) ((uint16_t)(c4 / 16.0f) & 0x0f)
#define BGR12FROMRGB(r, g, b) ((C4FROMC8(b) << 8) | (C4FROMC8(g) << 4) | C4FROMC8(r))
#define BGR12FROMRGBA32(rgba32) (BGR12FROMRGB(RFROMRGBA(rgba32), GFROMRGBA(rgba32), BFROMRGBA(rgba32)))

 /* palette of 12-bit BGR values generated from above 32-bit RGBA values
  * Format: 0000BBBBGGGGRRRR */

uint16_t __aligned(8) tms9918PaletteBGR12[16] =
{
  BGR12FROMRGBA32(TMSPAL00),
  BGR12FROMRGBA32(TMSPAL01),
  BGR12FROMRGBA32(TMSPAL02),
  BGR12FROMRGBA32(TMSPAL03),
  BGR12FROMRGBA32(TMSPAL04),
  BGR12FROMRGBA32(TMSPAL05),
  BGR12FROMRGBA32(TMSPAL06),
  BGR12FROMRGBA32(TMSPAL07),
  BGR12FROMRGBA32(TMSPAL08),
  BGR12FROMRGBA32(TMSPAL09),
  BGR12FROMRGBA32(TMSPAL10),
  BGR12FROMRGBA32(TMSPAL11),
  BGR12FROMRGBA32(TMSPAL12),
  BGR12FROMRGBA32(TMSPAL13),
  BGR12FROMRGBA32(TMSPAL14),
  BGR12FROMRGBA32(TMSPAL15),
};
