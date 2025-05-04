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
' -----------------------------------------------------------------------------
' CVBasic source file. See: github.com/nanochess/CVBasic
' -----------------------------------------------------------------------------

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
' PICO9918 Config Ids.
' See Pico9918Options enum in main.c
CONST CONF_PICO_MODEL       = 0
CONST CONF_HW_VERSION       = 1
CONST CONF_SW_VERSION       = 2
CONST CONF_SW_PATCH_VERSION = 3
CONST CONF_CLOCK_TESTED     = 4
CONST CONF_DISP_DRIVER      = 5
' ^^^ read only

' now the read/write ones
CONST CONF_CRT_SCANLINES    = 8         ' 0 (off) or 1 (on)
CONST CONF_SCANLINE_SPRITES = 9         ' 0 - 3 where value = (1 << (x + 2))
CONST CONF_CLOCK_PRESET_ID  = 10        ' 0 - 2 see ClockSettings in main.c
CONST CONF_DIAG             = 16
CONST CONF_DIAG_REGISTERS   = 17
CONST CONF_DIAG_PERFORMANCE = 18
CONST CONF_DIAG_PALETTE     = 19
CONST CONF_DIAG_ADDRESS     = 20
' ^^^ read/write config IDs

' now the "special" config IDs
CONST CONF_SAVE_TO_FLASH    = 255   
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

DEF FN MENU_DATA(I, C) = configMenuData((I) * CONF_STRUCT_LEN + (C))
DEF FN SET_MENU(I) = g_currentMenu = I

DIM tempConfigValues(CONF_COUNT)
DIM savedConfigValues(CONF_COUNT)

GOTO main

INCLUDE "vdp-utils.bas"
INCLUDE "patterns.bas"

INCLUDE "ui.bas"
INCLUDE "input.bas"

INCLUDE "config.bas"

INCLUDE "menu-main.bas"
INCLUDE "menu-firmware.bas"
INCLUDE "menu-info.bas"
INCLUDE "menu-diag.bas"
INCLUDE "menu-palette.bas"

    ' =========================================================================
    ' PROGRAM ENTRY
    ' -------------------------------------------------------------------------
main:

    ' GLOBALS    
    g_currentMenuIndex = 0                  ' current menu index
    g_paletteDirty = FALSE
    g_diagDirty = FALSE

    ' setup the screen
    VDP_DISABLE_INT_DISP_OFF

    GOSUB setupTiles
    GOSUB setupHeader

    ' what are we working with?
    GOSUB vdpDetect

    PRINT AT XY(4, 21), "Detected: "

    IF isF18ACompatible THEN

        ' looks like we're F18A compatible. do some more digging...
        
        VDP_DISABLE_INT_DISP_OFF

        VDP_SET_CURRENT_STATUS_REG(1)       ' SR1: ID
        statReg = VDP_READ_STATUS

        verPatch = 0

        IF (statReg AND $E8) = $E8 THEN
            VDP_SET_CURRENT_STATUS_REG(12)  ' config
            VDP(58) = CONF_SW_VERSION
            optValue = VDP_READ_STATUS
            verMajor = optValue / 16
            verMinor = optValue AND $0f
            VDP(58) = CONF_SW_PATCH_VERSION
            verPatch = VDP_READ_STATUS
            PRINT "PICO9918 v", verMajor, ".", verMinor, ".", verPatch
            isPico9918 = TRUE
        ELSEIF (statReg AND $E0) = $E0 THEN
            VDP_SET_CURRENT_STATUS_REG(14)      ' SR14: Version
            verReg = VDP_READ_STATUS
            verMajor = verReg / 16
            verMinor = verReg AND $0f
            PRINT "    F18A v ."
            PUT_XY(5 + 19, 21, hexChar(verMajor))
            PUT_XY(5 + 21, 21, hexChar(verMinor))
        ELSE
            PRINT "  UNKNOWN SR1 = ", <>statReg
        END IF

        VDP_RESET_STATUS_REG
        VDP_ENABLE_INT_DISP_OFF
    ELSEIF isV9938 THEN
        PRINT "Yamaha V9938"
    ELSE
        PRINT "  TI TMS99x8"
    END IF

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
        VDP(50) = $80  ' reset VDP registers to boot values
        VDP(7) = defaultReg(7)
        VDP(0) = defaultReg(0)  ' VDP() doesn't accept variables, so...
        VDP(1) = defaultReg(1)
        VDP(2) = defaultReg(2)
        VDP(3) = defaultReg(3)
        VDP(4) = defaultReg(4)
        VDP(5) = defaultReg(5)
        VDP(6) = defaultReg(6)

          ' enable interrupts (so we can wait)
        VDP_ENABLE_INT_DISP_OFF
        WAIT    
        WAIT            ' ensure default config is now in place
        VDP_DISABLE_INT_DISP_OFF ' enable display, but interrupts still off

        GOSUB vdpUnlock ' reset locked the vdp. unlock it again

        VDP_DISABLE_INT_DISP_OFF

        GOSUB vdpLoadConfigValues  ' load config values from VDP

        GOSUB applyConfigValues

        VDP_ENABLE_INT_DISP_OFF

        oldIndex = 0

        ' render the menu
        GOSUB updatePalette

        SET_MENU(MENU_ID_MAIN)

        ' palette for sprites and tile 1 layer
        VDP(24) = $11

        WHILE 1
            ON g_currentMenu GOSUB mainMenu, deviceInfoMenu, diagMenu, paletteMenu, firmwareMenu
            VDP_DISABLE_INT
            GOSUB clearScreen
        WEND

    END IF

' -----------------------------------------------------------------------------
' end it all 
' -----------------------------------------------------------------------------
exit:
    WAIT
    GOTO exit
    
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
