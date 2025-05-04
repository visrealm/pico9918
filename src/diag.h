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

void initDiagnostics();

void diagSetTemperature(float tempC);

void diagSetClockHz(float clockHz);

void diagnosticsConfigUpdated();

void updateDiagnostics(uint32_t frameCount);

void updateRenderTime(uint32_t renderTime, uint32_t frameTime);

int renderText(uint16_t scanline, const char *text, uint16_t x, uint16_t y, uint16_t fg, uint16_t bg, uint16_t* pixels);

void renderDiagnostics(uint16_t y, uint16_t* pixels);

