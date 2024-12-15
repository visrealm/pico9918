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
DEF FN DRAW_POPUP(T, L, H) = a_titleLen = L : a_popupHeight = H : a_popupTop = (23 - a_popupHeight) / 2 : GOSUB drawPopup : PRINT AT XY((32 - a_titleLen) / 2, a_popupTop), T

clearScreen: PROCEDURE
    DEFINE VRAM NAME_TAB_XY(0, 2), 32, horzBar
    FOR R = 3 TO 19
        DEFINE VRAM NAME_TAB_XY(0, R), 32, emptyRow
    NEXT R
    END

drawTitleBox: PROCEDURE
    L = a_titleLen
    X = (32 - L) / 2

    DEFINE VRAM NAME_TAB_XY(X - 1, MENU_TITLE_ROW - 1), L + 2, horzBar
    DEFINE VRAM NAME_TAB_XY(X - 1, MENU_TITLE_ROW + 1), L + 2, horzBar
    
    VPOKE NAME_TAB_XY(X - 2, MENU_TITLE_ROW), PATT_IDX_BORDER_V
    VPOKE NAME_TAB_XY(X + L + 1, MENU_TITLE_ROW), PATT_IDX_BORDER_V

    VPOKE NAME_TAB_XY(X - 2, MENU_TITLE_ROW - 1), PATT_IDX_BORDER_HD
    VPOKE NAME_TAB_XY(X + L + 1, MENU_TITLE_ROW - 1), PATT_IDX_BORDER_HD
    VPOKE NAME_TAB_XY(X - 2, MENU_TITLE_ROW + 1), PATT_IDX_BORDER_BL
    VPOKE NAME_TAB_XY(X + L + 1, MENU_TITLE_ROW + 1), PATT_IDX_BORDER_BR

    END

drawPopup: PROCEDURE
    L = a_titleLen
    H = a_popupHeight
    T = a_popupTop
    X = (32 - L) / 2

    DEFINE VRAM NAME_TAB_XY(X, T - 1), L, horzBar
    FOR Y = T TO T + H
        DEFINE VRAM NAME_TAB_XY(X - 1, Y), L + 1, vBar
        VPOKE NAME_TAB_XY(X + L, Y), PATT_IDX_BORDER_V
    NEXT Y
    DEFINE VRAM NAME_TAB_XY(X, T + 1), L, horzBar
    DEFINE VRAM NAME_TAB_XY(X, T + H + 1), L, horzBar

    VPOKE NAME_TAB_XY(X - 1, T - 1), PATT_IDX_BORDER_TL
    VPOKE NAME_TAB_XY(X + L, T - 1), PATT_IDX_BORDER_TR
    VPOKE NAME_TAB_XY(X - 1, T + 1), PATT_IDX_BORDER_VR
    VPOKE NAME_TAB_XY(X + L, T + 1), PATT_IDX_BORDER_VL
    VPOKE NAME_TAB_XY(X - 1, T + H + 1), PATT_IDX_BORDER_BL
    VPOKE NAME_TAB_XY(X + L, T + H + 1), PATT_IDX_BORDER_BR

    END


' -----------------------------------------------------------------------------
' set up the menu header (and footer)
' -----------------------------------------------------------------------------
setupHeader: PROCEDURE
    
    CONST LOGO_WIDTH = 19
    DEFINE VRAM NAME_TAB_XY(0, 0), LOGO_WIDTH, logoNames
    DEFINE VRAM NAME_TAB_XY(0, 1), LOGO_WIDTH, logoNames2

    PRINT AT XY(28, 0),"v",FIRMWARE_MAJOR_VER,".",FIRMWARE_MINOR_VER
    PRINT AT XY(20, 1),"Configurator"
    PRINT AT XY(6, 23), "{}",#FIRMWARE_YEAR," Troy Schrapel"    

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