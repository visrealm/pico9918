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
renderMainMenu: PROCEDURE
    MENU_INDEX_OFFSET = 0
    MENU_INDEX_COUNT = 9
    MENU_START_X = 1
    menuTopRow = MENU_TITLE_ROW + 3
    GOSUB renderMenu
    R = menuTopRow + MENU_INDEX_COUNT : GOSUB emptyRowR
    END

renderMenu: PROCEDURE
    FOR a_menuIndexToRender = MENU_INDEX_OFFSET TO MENU_INDEX_OFFSET + MENU_INDEX_COUNT - 1
        GOSUB renderMenuRow
    NEXT a_menuIndexToRender
    END

' -----------------------------------------------------------------------------
' render a menu row. Arguments: a_menuIndexToRender
' -----------------------------------------------------------------------------
renderMenuRow: PROCEDURE
    ' don't render special index 255
    MENU_INDEX_POSITION = a_menuIndexToRender - MENU_INDEX_OFFSET

    IF MENU_DATA(a_menuIndexToRender, CONF_INDEX) = 255 THEN R = menuTopRow + MENU_INDEX_POSITION : GOSUB emptyRowR : RETURN

    ' pre-compute row offset. we'll need this a few times
    #ROWOFFSET = XY(0, menuTopRow + MENU_INDEX_POSITION)

    ' output menu number (index + 1)
    PRINT AT #ROWOFFSET + MENU_START_X, " ", MENU_INDEX_POSITION + 1, ". "

    ' output menu label
    #addr = #VDP_NAME_TAB + #ROWOFFSET + MENU_START_X
    DEFINE VRAM #addr + 4, CONF_LABEL_LEN, VARPTR configMenuData(a_menuIndexToRender * CONF_STRUCT_LEN + CONF_LABEL)
    IF MENU_START_X < 2 THEN PRINT AT #ROWOFFSET + MENU_START_X + 20, "            "

    ' determine and output config option value label
    valuesCount = MENU_DATA(a_menuIndexToRender, CONF_NUM_VALUES)
    IF valuesCount > 0 THEN
        valuesBaseIndex = MENU_DATA(a_menuIndexToRender, CONF_VALUES_IND)
        currentValueOffset = tempConfigValues(a_menuIndexToRender)

        ' output option value
        DEFINE VRAM #addr + 22, 6, VARPTR configMenuOptionValueData((valuesBaseIndex + currentValueOffset) * CONF_VALUE_LABEL_LEN)
    END IF

    optId = MENU_DATA(a_menuIndexToRender, CONF_INDEX)
    configDirty = configMenuData(a_menuIndexToRender * CONF_STRUCT_LEN + CONF_NUM_VALUES) > 0
    configDirty = configDirty AND (savedConfigValues(a_menuIndexToRender) <> tempConfigValues(a_menuIndexToRender))
    configDirty = configDirty OR ((optId = CONF_MENU_PALETTE) AND g_paletteDirty)
    
    ' if the config option is "dirty" output an asterix next to it
    IF configDirty THEN
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

menuLoop: PROCEDURE

    key = CONT.KEY
    IF key >= $30 THEN key = key - $30

    lastMenuIndex = g_currentMenuIndex
    valueChanged = FALSE

    MIN_MENU_INDEX = MENU_INDEX_OFFSET
    MAX_MENU_INDEX = MENU_INDEX_OFFSET + MENU_INDEX_COUNT - 1

    GOSUB updateNavInput

    ' <down> button pressed?
    IF g_nav AND NAV_DOWN THEN  
        WHILE 1
            g_currentMenuIndex  = g_currentMenuIndex + 1
            IF g_currentMenuIndex > MAX_MENU_INDEX THEN g_currentMenuIndex = MIN_MENU_INDEX
            IF MENU_DATA(g_currentMenuIndex, CONF_INDEX) <> 255 THEN
                EXIT WHILE
            END IF
        WEND
    
    ' <up> button pressed?
    ELSEIF g_nav AND NAV_UP THEN  
        WHILE 1
            IF g_currentMenuIndex = MIN_MENU_INDEX THEN
                g_currentMenuIndex = MAX_MENU_INDEX
            ELSE
                g_currentMenuIndex = g_currentMenuIndex - 1
            END IF
            IF MENU_DATA(g_currentMenuIndex, CONF_INDEX) <> 255 THEN
                EXIT WHILE
            END IF
        WEND

    ' number button pressed?
    ELSEIF key > 0 AND key <= MENU_INDEX_COUNT THEN
        I = MENU_DATA(MIN_MENU_INDEX + key - 1, CONF_INDEX)
        IF I <> 255 THEN
            g_currentMenuIndex = MIN_MENU_INDEX + key - 1

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
    END


' -----------------------------------------------------------------------------
' the top-level menu
' -----------------------------------------------------------------------------
mainMenu: PROCEDURE 
    
    DRAW_TITLE("MAIN MENU")

    g_currentMenuIndex = oldIndex

    GOSUB renderMainMenu
    GOSUB initSprites

    GOSUB delay

    ' main menu loop
    WHILE 1

        IF g_currentMenuIndex = 1 THEN
            GOSUB animateSprites  ' do this first to ensure it's done within a frame
        ELSE
            WAIT
        END IF


        GOSUB menuLoop

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
                SET_MENU(MENU_ID_FIRMWARE)
                EXIT WHILE
            ELSEIF vdpOptId = CONF_MENU_INFO THEN
                SET_MENU(MENU_ID_INFO)
                EXIT WHILE
            ELSEIF vdpOptId = CONF_MENU_DIAG THEN
                SET_MENU(MENU_ID_DIAG)
                EXIT WHILE
            ELSEIF vdpOptId = CONF_MENU_PALETTE THEN
                SET_MENU(MENU_ID_PALETTE)
                EXIT WHILE
            ELSEIF vdpOptId = CONF_MENU_RESET THEN
                GOSUB resetOptions
            ELSEIF vdpOptId = CONF_MENU_SAVE THEN
                GOSUB saveOptionsMenu
            END IF
            GOSUB delay
        END IF
        
    WEND

    oldIndex = g_currentMenuIndex

    END



' -----------------------------------------------------------------------------
' save configuration
' -----------------------------------------------------------------------------
saveOptionsMenu: PROCEDURE

    configChanged = FALSE
    FOR I = 0 TO CONF_COUNT - 1
        IF savedConfigValues(I) <> tempConfigValues(I) THEN configChanged = TRUE
    NEXT I

    IF NOT g_paletteDirty AND NOT configChanged THEN
        PRINT AT XY(0, MENU_HELP_ROW), "  Skipped! No changes to save   "
        RETURN
    END IF

    oldIndex = g_currentMenuIndex

    ' prompt first

    ' remove highlight from main menu
    I = g_currentMenuIndex
    g_currentMenuIndex = 0
    RENDER_MENU_ROW(I)
    g_currentMenuIndex = I

    DRAW_POPUP_W("Save Changes?", 5, 20)

    menuTopRow = MENU_TITLE_ROW + 9
    MENU_INDEX_OFFSET = 10
    MENU_INDEX_COUNT = 2
    MENU_START_X = 6
    g_currentMenuIndex = MENU_INDEX_OFFSET

    GOSUB renderMenu

    GOSUB delay

    didSave = FALSE

    ' main menu loop
    WHILE 1
        WAIT

        GOSUB menuLoop

        IF valueChanged THEN
            vdpOptId = MENU_DATA(g_currentMenuIndex, CONF_INDEX)

            IF vdpOptId = CONF_MENU_OK THEN
                GOSUB saveOptions
                didSave = TRUE
            END IF

            EXIT WHILE
        END IF
        
        IF g_nav AND NAV_CANCEL THEN EXIT WHILE

    WEND

    g_currentMenuIndex = oldIndex

    GOSUB renderMainMenu

    IF didSave THEN
        ' if the clock frequency has changed... inform reboot
        PRINT AT XY(0, MENU_HELP_ROW), " Success! "
        IF clockChanged THEN
            PRINT "** Reboot required ** "
        ELSE
            PRINT " Configuration saved  "
        END IF
    END IF

    END


' -----------------------------------------------------------------------------
' go back to main menu
' -----------------------------------------------------------------------------
backOptionsMenu: PROCEDURE

    oldIndex = g_currentMenuIndex

    menuTopRow = MENU_TITLE_ROW + 9
    MENU_INDEX_OFFSET = 12
    MENU_INDEX_COUNT = 1
    MENU_START_X = 6
    g_currentMenuIndex = MENU_INDEX_OFFSET

    GOSUB renderMenu

    GOSUB delay

    ' main menu loop
    WHILE 1
        WAIT
        IF g_nav AND NAV_CANCEL THEN EXIT WHILE
        IF g_nav AND NAV_OK THEN EXIT WHILE
    WEND

    g_currentMenuIndex = oldIndex

    END


INCLUDE "conf-scanline-sprites.bas"

' -----------------------------------------------------------------------------
' Pico9918Options index, name[16], values index, num values, help[32]
' -----------------------------------------------------------------------------
configMenuData:
    DATA BYTE CONF_CRT_SCANLINES,   "CRT scanlines   ", 0, 2, "    Faux CRT scanline effect    "
    DATA BYTE CONF_SCANLINE_SPRITES,"Scanline sprites", 2, 4, "                                "
    DATA BYTE CONF_CLOCK_PRESET_ID, "Clock frequency ", 6, 3, "  MCU clock  (requires reboot)  "
    DATA BYTE CONF_MENU_DIAG,       "Diagnostics  >>>", 0, 0, "   Manage diagnostics options   "
    DATA BYTE CONF_MENU_PALETTE,    "Palette      >>>", 0, 0, "     Change default palette     "
    DATA BYTE CONF_MENU_INFO,       "Device info. >>>", 0, 0, "    View device information     "
    DATA BYTE CONF_MENU_FIRMWARE,   "Firmware     >>>", 0, 0, "        Update firmware         "
    DATA BYTE CONF_MENU_RESET,      "Reset defaults  ", 0, 0, " Reset to default configuration "
    DATA BYTE CONF_MENU_SAVE,       "Save settings   ", 0, 0, " Save configuration to PICO9918 "
    DATA BYTE CONF_MENU_EMPTY,      "                ", 0, 0, "                                "

    DATA BYTE CONF_MENU_OK,         "Confirm         ", 0, 0, " Save configuration to PICO9918 "
    DATA BYTE CONF_MENU_CANCEL,     "Cancel          ", 0, 0, "        Back to main menu       "

    DATA BYTE CONF_MENU_RESET,      "Reset defaults  ", 0, 0, " Reset to default configuration "
    
    DATA BYTE CONF_MENU_CANCEL,     "<<< Main menu   ", 0, 0, "                                "

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
