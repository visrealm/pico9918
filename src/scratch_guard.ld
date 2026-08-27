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

/*
 * Additive linker script fragment: bounds the scratch banks against the stacks
 * that share them.
 *
 * Each stack is reserved by size alone - the SDK script computes
 * __StackBottom = __StackTop - SIZEOF(.stack_dummy) from the region end, and
 * never compares the result against what the matching .scratch_* section
 * actually occupies. ld does catch SCRATCH_X, because .stack1_dummy is placed
 * contiguously after .scratch_x and trips a real region overflow, but it does
 * not track SCRATCH_Y at all: --print-memory-usage omits the region entirely,
 * and a core 0 stack large enough to span both banks links without a word.
 *
 * So assert it here. Bounding against __StackBottom rather than a literal
 * keeps these honest when PICO_STACK_SIZE or PICO_CORE1_STACK_SIZE moves.
 */

ASSERT(__scratch_x_end__ <= __StackOneBottom,
       "SCRATCH_X overflow: .scratch_x data has run into core 1's stack")

ASSERT(__scratch_y_end__ <= __StackBottom,
       "SCRATCH_Y overflow: .scratch_y data has run into core 0's stack")
