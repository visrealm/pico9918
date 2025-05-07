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

' The TI-99 implementation only has 8kB Banks
' Other implementations have 16kB banks.


#if TI994A
    BANK ROM 128
    CONST BANK_SIZE = 8
    INCLUDE "firmware_8k.h.bas"
    #INFO "TI-99/4A - 8KB BANK SIZE"
#elif NABU
    CONST BANK_SIZE = 0
    INCLUDE "firmware_16k.h.bas"
    #INFO "NABU - No banking"
#elif CREATIVISION
    CONST BANK_SIZE = 0
    INCLUDE "firmware_16k.h.bas"
    #INFO "CreatiVision - No banking"
#else
    BANK ROM 128
    CONST BANK_SIZE = 16
    INCLUDE "firmware_16k.h.bas"
    #INFO "Other - 16KB BANK SIZE"
#endif

#if F18A_TESTING
    #INFO "F18A testing mode"
#endif

GOTO main

INCLUDE "banksel.bas"
INCLUDE "core.bas"

#if TI994A
    INCLUDE "firmware_8k.bas"
#elif BANK_SIZE
    INCLUDE "firmware_16k.bas"
#endif
