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

' passing in L since I'm seeing issues using LEN(T) here. Possibly a bug?
DEF FN DRAW_TITLE(T, L) = a_titleLen = L : PRINT AT XY((32 - a_titleLen) / 2, MENU_TITLE_ROW), T : GOSUB drawTitleBox

clearScreen: PROCEDURE
    DEFINE VRAM NAME_TAB_XY(0, 2), 32, horzBar
    FOR R = 3 TO 19
        PRINT AT XY(0, R), "                                "
    NEXT R
    END

drawTitleBox: PROCEDURE
    L = a_titleLen
    DEFINE VRAM NAME_TAB_XY((32 - L) / 2, MENU_TITLE_ROW - 1), L, horzBar
    DEFINE VRAM NAME_TAB_XY((32 - L) / 2, MENU_TITLE_ROW + 1), L, horzBar
    
    VPOKE NAME_TAB_XY((32 - L) / 2 - 1,     MENU_TITLE_ROW), PATT_IDX_BORDER_V
    VPOKE NAME_TAB_XY((32 - L) / 2 + L,     MENU_TITLE_ROW), PATT_IDX_BORDER_V

    VPOKE NAME_TAB_XY((32 - L) / 2 - 1,     MENU_TITLE_ROW - 1), PATT_IDX_BORDER_TL
    VPOKE NAME_TAB_XY((32 - L) / 2 + L, MENU_TITLE_ROW - 1), PATT_IDX_BORDER_TR
    VPOKE NAME_TAB_XY((32 - L) / 2 - 1,     MENU_TITLE_ROW + 1), PATT_IDX_BORDER_BL
    VPOKE NAME_TAB_XY((32 - L) / 2 + L, MENU_TITLE_ROW + 1), PATT_IDX_BORDER_BR

    END

' -----------------------------------------------------------------------------
' set up the menu header (and footer)
' -----------------------------------------------------------------------------
setupHeader: PROCEDURE
    
    CONST LOGO_WIDTH = 19
    DEFINE VRAM NAME_TAB_XY(0, 0), LOGO_WIDTH, logoNames
    DEFINE VRAM NAME_TAB_XY(0, 1), LOGO_WIDTH, logoNames2

    PRINT AT XY(28, 0),"v1.0"
    PRINT AT XY(20, 1),"Configurator"
    PRINT AT XY(6, 23), "{}2024 Troy Schrapel"    

    DEFINE VRAM NAME_TAB_XY(0, 2), 32, horzBar
    'DEFINE VRAM NAME_TAB_XY(0, 4), 32, horzBar
    DEFINE VRAM NAME_TAB_XY(0, 20), 32, horzBar
    DEFINE VRAM NAME_TAB_XY(0, 22), 32, horzBar

    END

' -----------------------------------------------------------------------------
' update the PICO9918 palette (shades of blue)
' -----------------------------------------------------------------------------
updatePalette: PROCEDURE    
    WAIT
    VDP(47) = $c0 + 18 ' palette data port from index #2
    PRINT "\0\7"
    PRINT "\0\10"
    PRINT "\0\12"
    PRINT "\0\15"
    PRINT "\0\15"
    PRINT "\2\47"
    PRINT "\4\79"
    PRINT "\7\127"
    PRINT "\10\0"
    PRINT "\15\255"
    PRINT "\15\255"
    PRINT "\15\255"
    PRINT "\9\153"
    VDP(47) = $40
    END