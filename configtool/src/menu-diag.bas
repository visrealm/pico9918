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

' Master switch
' Registers
' Performance
' Temperature
' Palette

diagMenu: PROCEDURE

    DRAW_TITLE("DIAGNOSTICS")

    GOSUB pushMenuCtx
    SET_MENU_CTX(16, 5, 1, MENU_TITLE_ROW + 3)
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

        IF NAV(NAV_CANCEL) THEN EXIT WHILE

    WEND

    GOSUB popMenuCtx
    SET_MENU(MENU_ID_MAIN)

    g_diagDirty = FALSE
    FOR I = CONF_DIAG_REGISTERS TO CONF_DIAG_ADDRESS
        IF savedConfigValues(I) <> tempConfigValues(I) THEN g_diagDirty = TRUE
    NEXT I

    END
