'
' Project: pico9918
'
' PICO9918 Configurator
'
' Copyright (c) 2024 Troy Schrapel
'
' This code is licensed under the MIT license
'
' https://github.com/visrealm/pico9918
'

' helper constants
CONST TRUE           = -1
CONST FALSE          = 0

CONST MENU_TITLE_ROW   = 3
CONST MENU_HELP_ROW    = 19

CONST MENU_ID_MAIN     = 0
CONST MENU_ID_INFO     = 1
CONST MENU_ID_DIAG     = 2
CONST MENU_ID_PALETTE  = 3
CONST MENU_ID_FIRMWARE = 4
CONST MENU_ID_OUTPUT   = 5

' Pico9918Options index, name[16], values index, num values,help[32]
CONST CONF_COUNT      = 160 ' number of primary config options
CONST CONF_INDEX      = 0
CONST CONF_LABEL      = 1
CONST CONF_LABEL_LEN  = 16
CONST CONF_VALUES_IND = (CONF_LABEL + CONF_LABEL_LEN)
CONST CONF_NUM_VALUES = (CONF_VALUES_IND + 1)
CONST CONF_HELP       = (CONF_NUM_VALUES + 1)
CONST CONF_HELP_LEN   = 32
CONST CONF_STRUCT_LEN = (CONF_HELP + CONF_HELP_LEN)

' config option value label length
CONST CONF_VALUE_LABEL_LEN = 6

' -------------------------------
' PICO9918 Config Ids. Generated from the library's authoritative
' pico9918_config.h by tools/config2bas.py - never declare a PICO9918_CONF_*
' byte index here.
INCLUDE "config-ids.bas"

' must match firmware src/config.h
CONST PENDING_STATE_CONFIRMED = $C0
CONST PENDING_STATE_PENDING   = $9E
CONST PENDING_STATE_ARMED     = $A0
' -------------------------------

CONST CONF_MENU_PALETTE     = 251
CONST CONF_MENU_DIAG        = 252
CONST CONF_MENU_INFO        = 253
CONST CONF_MENU_RESET       = 254
CONST CONF_MENU_EMPTY       = 255
CONST CONF_MENU_SAVE        = 250
CONST CONF_MENU_FIRMWARE    = 249
CONST CONF_MENU_OK          = 248
CONST CONF_MENU_CANCEL      = 247
CONST CONF_MENU_OUTPUT      = 246

DEF FN MENU_DATA(I, C) = configMenuData((I) * CONF_STRUCT_LEN + (C))
DEF FN SET_MENU(I) = g_currentMenu = I

DIM tempConfigValues(CONF_COUNT)
DIM savedConfigValues(CONF_COUNT)

' All INCLUDEs below land in bank 0 (the default bank). Bank 0 is the
' dispatcher for cross-bank calls - only bank 0 may issue BANK SELECT.
' menu-palette.bas (included at the bottom of this file) opts into BANK 1.
' Firmware payload data lives in banks 2+.
INCLUDE "vdp-utils.bas"
INCLUDE "patterns.bas"

INCLUDE "ui.bas"
INCLUDE "input.bas"

INCLUDE "config.bas"

INCLUDE "menu-data.bas"
INCLUDE "menu-main.bas"
INCLUDE "menu-firmware.bas"
INCLUDE "menu-info.bas"
INCLUDE "menu-diag.bas"
INCLUDE "menu-output.bas"
INCLUDE "menu-confirm.bas"
    ' =========================================================================
    ' PROGRAM ENTRY
    ' -------------------------------------------------------------------------
main: PROCEDURE

    ' GLOBALS    
    g_currentMenuIndex = 0                  ' current menu index
    g_paletteDirty = FALSE
    g_diagDirty = FALSE
    g_outputDirty = FALSE
    g_resetPending = FALSE

    ' setup the screen
    VDP_DISABLE_INT_DISP_OFF

    GOSUB setupTiles
    GOSUB setupHeader

    ' what are we working with?
    GOSUB vdpDetect

    PRINT AT XY(3, 21), "Detected: "

    IF isF18ACompatible THEN

        ' looks like we're F18A compatible. do some more digging...
        
        VDP_DISABLE_INT_DISP_OFF

        VDP_STATUS_REG = 1       ' SR1: ID
        statReg = VDP_STATUS

        verPatch = 0

        IF (statReg AND $E8) = $E8 THEN
            VDP_STATUS_REG = 12  ' config
            VDP_REG(58) = PICO9918_CONF_SW_VERSION
            optValue = VDP_STATUS
            verMajor = optValue / 16
            verMinor = optValue AND $0f
            VDP_REG(58) = PICO9918_CONF_SW_PATCH_VERSION
            verPatch = VDP_STATUS
            VDP_REG(58) = PICO9918_CONF_PICO_MODEL
            picoModel = VDP_STATUS
            VDP_REG(58) = PICO9918_CONF_HW_VERSION
            optValue = VDP_STATUS
            hwMajor = optValue / 16
            hwMinor = optValue AND $0f
            PRINT "PICO9918 "
            IF picoModel = PICO_MODEL_RP2350 THEN PRINT "PRO "
            PRINT "v", hwMajor, "."
            IF hwMinor = 0 THEN PRINT "x" ELSE PRINT hwMinor
            isPico9918 = TRUE
        ELSEIF (statReg AND $E0) = $E0 THEN
            VDP_STATUS_REG = 14      ' SR14: Version
            verReg = VDP_STATUS
            verMajor = verReg / 16
            verMinor = verReg AND $0f
            PRINT "    F18A v"
            PRINT CHR$(hexChar(verMajor)), ".", CHR$(hexChar(verMinor))
        ELSE
            PRINT "  UNKNOWN SR1 = ", <>statReg
        END IF

        VDP_STATUS_REG0
        VDP_ENABLE_INT_DISP_OFF
#if TMS9918_TESTING
    ELSE
        PRINT " Emu Test Build"
#else
    ELSEIF isV9938 THEN
        PRINT "Yamaha V9938"
    ELSE
        PRINT "  TI TMS99xxA"
#endif
    END IF

#if TMS9918_TESTING
    isF18ACompatible = TRUE
    isPico9918 = isF18ACompatible
#endif

#if F18A_TESTING
    isPico9918 = isF18ACompatible   ' FOR TESTING
#endif

    VDP_ENABLE_INT_DISP_OFF

    IF NOT isPico9918 THEN
        PRINT AT XY(7, 6 + (isF18ACompatible AND 4)), "PICO9918 not found"
        IF NOT isF18ACompatible AND NOT isV9938 THEN
            PRINT AT XY(15, 9), "OR"
            PRINT AT XY(3, 12), "PICO9918 firmware too old"
            PRINT AT XY(4, 14), "Firmware v1.0+ required"
            PRINT AT XY(2, 16), "Update manually via USB from"
            PRINT AT XY(2, 18), "github.com/visrealm/pico9918"
        END IF
        VDP_ENABLE_INT
    ELSE
        ' We are a PICO9918, set up the menu
        WAIT
        VDP_REG(50) = $80  ' reset VDP registers to boot values
        VDP_REG(7) = defaultReg(7)
        VDP_REG(0) = defaultReg(0)  ' VDP_REG() doesn't accept variables, so...
        VDP_REG(1) = defaultReg(1)
        VDP_REG(2) = defaultReg(2)
        VDP_REG(3) = defaultReg(3)
        VDP_REG(4) = defaultReg(4)
        VDP_REG(5) = defaultReg(5)
        VDP_REG(6) = defaultReg(6)

        BANKSEL(1)

          ' enable interrupts (so we can wait)
        VDP_ENABLE_INT_DISP_OFF
        WAIT    
        WAIT            ' ensure default config is now in place
        VDP_DISABLE_INT_DISP_OFF ' enable display, but interrupts still off

        GOSUB vdpUnlock ' reset locked the vdp. unlock it again

        VDP_DISABLE_INT_DISP_OFF

        GOSUB vdpLoadConfigValues  ' load config values from VDP

        GOSUB checkFirmwareVersion  ' halt or force update if firmware is too old

        GOSUB checkPendingDisplayChange  ' prompt if a display change is awaiting confirmation

        GOSUB applyConfigValues

        VDP_ENABLE_INT_DISP_OFF

        oldIndex = 0

        ' render the menu
        GOSUB updatePalette

        SET_MENU(MENU_ID_MAIN)

        ' palette for sprites and tile 1 layer
        VDP_REG(24) = $11
        
        WHILE 1
            ON g_currentMenu GOSUB mainMenu, deviceInfoMenu, diagMenu, paletteMenu, firmwareMenu, outputMenu
            VDP_DISABLE_INT
            GOSUB clearScreen
        WEND

    END IF
    END
    
' -----------------------------------------------------------------------------
' delay between user input (2/15th second)
' -----------------------------------------------------------------------------
delay: PROCEDURE
    VDP_ENABLE_INT
    FOR del = 1 TO 8
        WAIT
    NEXT del
    END


hexChar:
    DATA BYTE "0123456789ABCDEF"

' menu-palette.bas opens with `BANK 1`, so everything inside it lands there.
INCLUDE "menu-palette.bas"
