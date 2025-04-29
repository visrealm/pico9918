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

    GOSUB delay

    PRINT AT XY(9, 10), "Coming soon..."

    GOSUB waitForInput

    SET_MENU(MENU_ID_MAIN)
    END
