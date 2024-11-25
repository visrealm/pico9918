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

' menu helpers
DEF FN RENDER_MENU_ROW(R) = a_menuIndexToRender = R : WAIT : GOSUB renderMenuRow

' -----------------------------------------------------------------------------
' render all menu rows
' -----------------------------------------------------------------------------
renderMenu: PROCEDURE
    FOR a_menuIndexToRender = 0 TO CONF_COUNT - 1
        GOSUB renderMenuRow
    NEXT a_menuIndexToRender
    END

' -----------------------------------------------------------------------------
' render a menu row. Arguments: a_menuIndexToRender
' -----------------------------------------------------------------------------
renderMenuRow: PROCEDURE
    CONST MENU_START_X = 0

    ' don't render special index 255
    IF MENU_DATA(a_menuIndexToRender, CONF_INDEX) = 255 THEN RETURN

    ' pre-compute row offset. we'll need this a few times
    #ROWOFFSET = XY(0, MENU_TOP_ROW + a_menuIndexToRender)

    ' output menu number (index + 1)
    PRINT AT #ROWOFFSET + MENU_START_X, " ", a_menuIndexToRender + 1, ". "

    ' output menu label
    DEFINE VRAM #VDP_NAME_TAB + #ROWOFFSET + MENU_START_X + 4, CONF_LABEL_LEN, VARPTR configMenuData(a_menuIndexToRender * CONF_STRUCT_LEN + CONF_LABEL)
    PRINT AT #ROWOFFSET + MENU_START_X + 20, "            "

    ' determine and output config option value label
    valuesCount = MENU_DATA(a_menuIndexToRender, CONF_NUM_VALUES)
    IF valuesCount > 0 THEN
        valuesBaseIndex = MENU_DATA(a_menuIndexToRender, CONF_VALUES_IND)
        currentValueOffset = tempConfigValues(a_menuIndexToRender)

        ' output option value
        DEFINE VRAM #VDP_NAME_TAB + #ROWOFFSET + MENU_START_X + 23, 6, VARPTR configMenuOptionValueData((valuesBaseIndex + currentValueOffset) * CONF_VALUE_LABEL_LEN)
    END IF

    ' if the config option is "dirty" output an asterix next to it
    IF savedConfigValues(a_menuIndexToRender) <> tempConfigValues(a_menuIndexToRender) THEN
        PRINT AT #ROWOFFSET + 30 - MENU_START_X, "*"
    END IF    

    ' if this is the current menu item - highlight it
    IF a_menuIndexToRender = g_currentMenuIndex THEN
        GOSUB highlightMenuRow
    END IF
    END

' -----------------------------------------------------------------------------
' highlight a menu row. Arguments: a_menuIndexToRender
' -----------------------------------------------------------------------------
highlightMenuRow: PROCEDURE
    ' Set MSB bit for all characters in this row which selects the
    ' "highlight" versions of the patterns
    FOR R = MENU_START_X + 1 TO 31 - MENU_START_X
        C = VPEEK(#VDP_NAME_TAB + #ROWOFFSET+ R)
        C = C OR 128
        VPOKE (#VDP_NAME_TAB + #ROWOFFSET + R), C
    NEXT R

    ' ends of highlight bar
    VPOKE (#VDP_NAME_TAB + #ROWOFFSET + MENU_START_X),  PATT_IDX_SELECTED_L
    VPOKE (#VDP_NAME_TAB + #ROWOFFSET + 31 - MENU_START_X), PATT_IDX_SELECTED_R

    ' output help line for the active menu item
    DEFINE VRAM #VDP_NAME_TAB + XY(0, MENU_HELP_ROW), 32, VARPTR configMenuData(a_menuIndexToRender * CONF_STRUCT_LEN + CONF_HELP)
    END


' -----------------------------------------------------------------------------
' the top-level menu
' -----------------------------------------------------------------------------
mainMenu: PROCEDURE 
    
    DRAW_TITLE("MAIN MENU", 9)

    GOSUB renderMenu
    GOSUB initSprites

    VDP_ENABLE_INT
    GOSUB delay

    ' main menu loop
    WHILE 1
        WAIT

        IF g_currentMenuIndex = 1 THEN GOSUB animateSprites  ' do this first to ensure it's done within a frame

        key = CONT.KEY
        IF key >= $30 THEN key = key - $30

        lastMenuIndex = g_currentMenuIndex
        valueChanged = FALSE

        GOSUB getNavButton

        ' <down> button pressed?
        IF g_nav AND NAV_DOWN THEN  
            WHILE 1
                g_currentMenuIndex = g_currentMenuIndex + 1
                IF g_currentMenuIndex >= CONF_COUNT THEN g_currentMenuIndex = 0
                IF MENU_DATA(g_currentMenuIndex, CONF_INDEX) <> 255 THEN
                    EXIT WHILE
                END IF
            WEND
        
        ' <up> button pressed?
        ELSEIF g_nav AND NAV_UP THEN  
            WHILE 1
                g_currentMenuIndex = g_currentMenuIndex - 1
                IF g_currentMenuIndex >= CONF_COUNT THEN g_currentMenuIndex = CONF_COUNT - 1
                IF MENU_DATA(g_currentMenuIndex, CONF_INDEX) <> 255 THEN
                    EXIT WHILE
                END IF
            WEND

        ' number button pressed?
        ELSEIF key > 0 AND key <= CONF_COUNT THEN
            I = MENU_DATA(key - 1, CONF_INDEX)
            IF I <> 255 THEN
                g_currentMenuIndex = key - 1

                IF I > 200 THEN
                    valueChanged = TRUE
                END IF
            END IF

        ' <fire>, <space> or <right> pressed? - next option value
        ELSEIF (g_nav AND NAV_OK) OR (g_nav AND NAV_RIGHT) THEN 
            IF MENU_DATA(g_currentMenuIndex, CONF_INDEX) - 1 < 200 THEN
                tempConfigValuesCount = MENU_DATA(g_currentMenuIndex, CONF_NUM_VALUES)
                currentValueIndex = tempConfigValues(g_currentMenuIndex)
                currentValueIndex = currentValueIndex + 1
                IF currentValueIndex >= tempConfigValuesCount THEN currentValueIndex = 0
                tempConfigValues(g_currentMenuIndex) = currentValueIndex
            END IF
            valueChanged = TRUE

        ' <left> pressed - previous option value
        ELSEIF (g_nav AND NAV_LEFT) THEN 
            IF MENU_DATA(g_currentMenuIndex, CONF_INDEX) - 1 < 200 THEN
                tempConfigValuesCount = MENU_DATA(g_currentMenuIndex, CONF_NUM_VALUES)
                currentValueIndex = tempConfigValues(g_currentMenuIndex)
                currentValueIndex = currentValueIndex - 1
                IF currentValueIndex >= tempConfigValuesCount THEN currentValueIndex = tempConfigValuesCount - 1
                tempConfigValues(g_currentMenuIndex) = currentValueIndex
                valueChanged = TRUE
            END IF
        END IF
        
        ' have we changed menu items?
        IF g_currentMenuIndex <> lastMenuIndex THEN
            RENDER_MENU_ROW(lastMenuIndex)
            RENDER_MENU_ROW(g_currentMenuIndex)

            IF g_currentMenuIndex <> 1 THEN GOSUB hideSprites
            IF NOT valueChanged THEN GOSUB delay
        END IF

        ' has the value changed for this config option? (or we selected a submenu by number)
        IF valueChanged THEN
            RENDER_MENU_ROW(g_currentMenuIndex)
            WAIT
            vdpOptId = MENU_DATA(g_currentMenuIndex, CONF_INDEX)
            IF vdpOptId < 200 THEN
                VDP_WRITE_CONFIG(vdpOptId, currentValueIndex)
            END IF

            IF vdpOptId = CONF_CRT_SCANLINES THEN
                VDP(50) = currentValueIndex * $04
            ELSEIF vdpOptId = CONF_SCANLINE_SPRITES THEN
                VDP(30) = pow2(currentValueIndex + 2)
            ELSEIF vdpOptId = CONF_MENU_FIRMWARE THEN
                g_currentMenu = MENU_ID_FIRMWARE
                EXIT WHILE
            ELSEIF vdpOptId = CONF_MENU_INFO THEN
                g_currentMenu = MENU_ID_INFO
                EXIT WHILE
            ELSEIF vdpOptId = CONF_MENU_DIAG THEN
                g_currentMenu = MENU_ID_DIAG
                EXIT WHILE
            ELSEIF vdpOptId = CONF_MENU_PALETTE THEN
                g_currentMenu = MENU_ID_PALETTE
                EXIT WHILE
            ELSEIF vdpOptId = CONF_MENU_RESET THEN
                GOSUB resetOptions
            ELSEIF vdpOptId = CONF_MENU_SAVE THEN
                GOSUB saveOptions
            END IF
            GOSUB delay
        END IF
        
    WEND
    END

INCLUDE "conf-scanline-sprites.bas"

' -----------------------------------------------------------------------------
' Pico9918Options index, name[16], values index, num values, help[32]
' -----------------------------------------------------------------------------
configMenuData:
    DATA BYTE CONF_CRT_SCANLINES,   "CRT scanlines   ", 0, 2, "    Faux CRT scanline effect    "
    DATA BYTE CONF_SCANLINE_SPRITES,"Scanline sprites", 2, 4, "                                "
    DATA BYTE CONF_CLOCK_PRESET_ID, "Clock frequency ", 6, 3, " RP2040 clock (requires reboot) "
    DATA BYTE CONF_MENU_RESET,      "Reset defaults  ", 0, 0, " Reset to default configuration "
    DATA BYTE CONF_MENU_SAVE,       "Save Settings   ", 0, 0, " Save configuration to PICO9918 "
    DATA BYTE CONF_MENU_EMPTY,      "                ", 0, 0, "                                "
    DATA BYTE CONF_MENU_DIAG,       "Diagnostics  >>>", 0, 0, "   Manage diagnostics options   "
    DATA BYTE CONF_MENU_PALETTE,    "Palette      >>>", 0, 0, "     Change default palette     "
    DATA BYTE CONF_MENU_INFO,       "Device info. >>>", 0, 0, "    View device information     "
    DATA BYTE CONF_MENU_FIRMWARE,   "Firmware     >>>", 0, 0, "        Update firmware         "

' -----------------------------------------------------------------------------
' Pico9918Option values. Indexed from options()
' -----------------------------------------------------------------------------
configMenuOptionValueData:
    DATA BYTE "Off   "
    DATA BYTE "On    "
    DATA BYTE "4     "
    DATA BYTE "8     "
    DATA BYTE "16    "
    DATA BYTE "32    "
    DATA BYTE "252MHz"
    DATA BYTE "302MHz"
    DATA BYTE "352MHz"
