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



firmwareMenu: PROCEDURE
    
    DRAW_TITLE("FIRMWARE UPDATE", 15)

    VDP_ENABLE_INT
    GOSUB delay
    I = 0
    FOR B = 1 TO 5
        ON B FAST GOSUB ,selectBank1,selectBank2,selectBank3,selectBank4,selectBank5
        ',selectBank6',selectBank7',selectBank8,selectBank9,selectBank10
        DEFINE VRAM NAME_TAB_XY(0, MENU_TOP_ROW + B), 32, VARPTR bank1Start(289)
    NEXT B

    BANK SELECT 0

    WHILE 1
        WAIT
        GOSUB getNavButton

        IF (g_nav AND NAV_CANCEL) THEN EXIT WHILE
    WEND

    g_currentMenu = MENU_ID_MAIN
    END    

selectBank1:
    BANK SELECT 1
    RETURN

selectBank2:
    BANK SELECT 2
    RETURN

selectBank3:
    BANK SELECT 3
    RETURN

selectBank4:
    BANK SELECT 4
    RETURN

selectBank5:
    BANK SELECT 5
    RETURN

'selectBank6:
    'BANK SELECT 6
    'RETURN

'selectBank7:
    'BANK SELECT 7
    'RETURN

'selectBank8:
    'BANK SELECT 8
    'RETURN
'
'selectBank9:
    'BANK SELECT 9
    'RETURN

'selectBank10:
    'BANK SELECT 10
    'RETURN
