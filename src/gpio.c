/*
 * Project: pico9918
 *
 * Copyright (c) 2024 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/pico9918
 *
 */

#include "gpio.h"

#include "pico/binary_info.h"


bi_decl(bi_1pin_with_name(GPIO_GROMCL, "GROM Clock"));
bi_decl(bi_1pin_with_name(GPIO_CPUCL, "CPU Clock"));
bi_decl(bi_pin_mask_with_names(GPIO_CD_MASK, "CPU Data (CD7 - CD0)"));
bi_decl(bi_1pin_with_name(GPIO_CSR, "Read"));
bi_decl(bi_1pin_with_name(GPIO_CSW, "Write"));
bi_decl(bi_1pin_with_name(GPIO_MODE, "Mode"));
bi_decl(bi_1pin_with_name(GPIO_INT, "Interrupt"));

bi_decl(bi_1pin_with_name(GPIO_RESET, "Host Reset"));
bi_decl(bi_1pin_with_name(GPIO_MODE1, "Mode 1 (V9938)"));
