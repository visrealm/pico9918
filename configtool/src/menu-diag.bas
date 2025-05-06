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

' Master switch
' Registers
' Performance
' Temperature
' Palette

diagMenu: PROCEDURE

    DRAW_TITLE("DIAGNOSTICS")

    MENU_INDEX_OFFSET = 16
    MENU_INDEX_COUNT = 5
    g_currentMenuIndex = MENU_INDEX_OFFSET

    GOSUB renderMenu

    GOSUB delay

    WHILE 1
        
        WAIT

        GOSUB menuLoop

        IF valueChanged THEN
            RENDER_MENU_ROW(g_currentMenuIndex)
            WAIT
            vdpOptId = MENU_DATA(g_currentMenuIndex, CONF_INDEX)
            IF vdpOptId < 200 THEN
                VDP_CONFIG(vdpOptId) = currentValueIndex
            END IF

            optionIndex = MENU_DATA(g_currentMenuIndex, CONF_INDEX)
            IF optionIndex = CONF_MENU_CANCEL THEN EXIT WHILE
            GOSUB delay
        END IF

        IF g_nav AND NAV_CANCEL THEN EXIT WHILE
        
    WEND

    g_currentMenuIndex = oldIndex
    SET_MENU(MENU_ID_MAIN)

    g_diagDirty = FALSE
    FOR I = CONF_DIAG_REGISTERS TO CONF_DIAG_ADDRESS
        IF savedConfigValues(I) <> tempConfigValues(I) THEN g_diagDirty = TRUE
    NEXT I

    END
