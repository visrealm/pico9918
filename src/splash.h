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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef PICO9918_NO_SPLASH
#define PICO9918_NO_SPLASH      0
#endif

void resetSplash();

void allowSplashHide();

void outputSplash(uint16_t y, uint32_t frameCount, uint32_t vBorder, uint32_t vPixels, uint16_t* pixels);