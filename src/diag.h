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

#include <stdint.h>

void diagSetTemperatureBcd(uint32_t tempBcd);

void updateDiagnostics(uint32_t frameCount);

void updateRenderTime(uint32_t renderTime, uint32_t frameTime);

void renderDiagnostics(uint16_t y, uint16_t* pixels);