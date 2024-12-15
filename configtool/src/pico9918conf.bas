'
' Project: pico9918
'
' PICO9918 Configurator (TI-99)
'
' Copyright (c) 2024 Troy Schrapel
'
' This code is licensed under the MIT license
'
' https://github.com/visrealm/pico9918
'
' -----------------------------------------------------------------------------
' CVBasic source file. See: github.com/nanochess/CVBasic
' -----------------------------------------------------------------------------

' The TI-99 implementation only has 8kB Banks, so we need a separate wrapper
' for it. Other implementations have 16kB banks.

BANK ROM 256

CONST BANK_SIZE = 16

INCLUDE "firmware_16k.h.bas"

INCLUDE "core.bas"

INCLUDE "firmware_8k.bas"