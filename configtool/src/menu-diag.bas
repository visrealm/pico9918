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


diagMenu: PROCEDURE

    DRAW_TITLE("DIAGNOSTICS")

    VDP_ENABLE_INT

    GOSUB delay

    WHILE 1
        WAIT
        GOSUB updateNavInput
        IF (g_nav AND NAV_CANCEL) THEN EXIT WHILE
    WEND

    SET_MENU(MENU_ID_MAIN)
    END
