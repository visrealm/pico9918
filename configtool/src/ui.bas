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

DEF FN DRAW_TITLE_AT(T, R) = a_titleLen = LEN(T) : PRINT AT XY((32 - a_titleLen) / 2, R), T : GOSUB drawTitleBox
DEF FN DRAW_TITLE(T) = DRAW_TITLE_AT(T, MENU_TITLE_ROW)

DEF FN DRAW_POPUP_W(T, H, W) = a_titleLen = LEN(T) : a_popupHeight = H : a_popupWidth = W : a_popupTop = (23 - a_popupHeight) / 2 : GOSUB drawPopup : PRINT AT XY((32 - a_titleLen) / 2, a_popupTop), T
DEF FN DRAW_POPUP(T, H) = DRAW_POPUP_W(T, H, LEN(T))

' -----------------------------------------------------------------------------
' draw an empty row at R
' -----------------------------------------------------------------------------
emptyRowR: PROCEDURE
    DEFINE VRAM NAME_TAB_XY(0, R), 32, emptyRow
    END

' -----------------------------------------------------------------------------
' draw a horizontal bar
' -----------------------------------------------------------------------------
horzBarR:
    BW = 32
horzBarRW:
    BX = 0
horzBarRWX: PROCEDURE
    DEFINE VRAM NAME_TAB_XY(BX, BR), BW, horzBar
    END

' -----------------------------------------------------------------------------
' clear the screen (rows 3 to 19 anyway...)
' -----------------------------------------------------------------------------
clearScreen: PROCEDURE
    BR = 2 : GOSUB horzBarR
    FOR R = 3 TO 19
        GOSUB emptyRowR
    NEXT R
    END

' -----------------------------------------------------------------------------
' draw a title box (at the top of the screen)
' -----------------------------------------------------------------------------
drawTitleBox: PROCEDURE
    X = (32 - a_titleLen) / 2

    BW = a_titleLen + 2
    BX = X - 1
    BR = MENU_TITLE_ROW - 1 : GOSUB horzBarRWX
    BR = MENU_TITLE_ROW + 1 : GOSUB horzBarRWX
    
    x1 = X - 2 : x2 = X + a_titleLen + 1

    #addr = NAME_TAB_XY(x1, MENU_TITLE_ROW)
    VPOKE #addr, PATT_IDX_BORDER_V
    VPOKE #addr - 32, PATT_IDX_BORDER_HD
    VPOKE #addr + 32, PATT_IDX_BORDER_BL

    #addr = NAME_TAB_XY(x2, MENU_TITLE_ROW)
    VPOKE #addr, PATT_IDX_BORDER_V
    VPOKE #addr - 32, PATT_IDX_BORDER_HD
    VPOKE #addr + 32, PATT_IDX_BORDER_BR

    END

' -----------------------------------------------------------------------------
' render a popup window
' -----------------------------------------------------------------------------
drawPopup: PROCEDURE
    X = (32 - a_popupWidth) / 2

    BW = a_popupWidth
    BX = X

    BR = a_popupTop - 1: GOSUB horzBarRWX
    #addr = NAME_TAB_XY(X - 1, a_popupTop)
    FOR Y = 0 TO a_popupHeight
        VPOKE #addr, PATT_IDX_BORDER_V
        DEFINE VRAM #addr + 1, a_popupWidth, emptyRow
        VPOKE #addr + a_popupWidth + 1, PATT_IDX_BORDER_V
        #addr = #addr + 32
    NEXT Y
    BR = BR + 2: GOSUB horzBarRWX
    BR = BR + a_popupHeight: GOSUB horzBarRWX

    x2 = X + a_popupWidth
    #addr = NAME_TAB_XY(X - 1, a_popupTop + 1)
    VPOKE #addr - 64, PATT_IDX_BORDER_TL
    VPOKE #addr, PATT_IDX_BORDER_VR
    VPOKE #addr + a_popupHeight * 32, PATT_IDX_BORDER_BL
    #addr = NAME_TAB_XY(X + a_popupWidth, a_popupTop + 1)
    VPOKE #addr - 64, PATT_IDX_BORDER_TR
    VPOKE #addr, PATT_IDX_BORDER_VL
    VPOKE #addr + a_popupHeight * 32, PATT_IDX_BORDER_BR

    END

' -----------------------------------------------------------------------------
' set up the menu header (and footer)
' -----------------------------------------------------------------------------
setupHeader: PROCEDURE

    ' output top-left logo    
    CONST LOGO_WIDTH = 19
    #addr = #VDP_NAME_TAB
    FOR I = 1 TO LOGO_WIDTH
        VPOKE #addr, I
        VPOKE #addr + 32, I + 128
        #addr = #addr + 1
    NEXT I

    PRINT AT XY(26, 0),"v",FIRMWARE_MAJOR_VER,".",FIRMWARE_MINOR_VER,".",FIRMWARE_PATCH_VER
    PRINT AT XY(20, 1),"Configurator"
    PRINT AT XY(6, 23), "{}",#FIRMWARE_YEAR," Troy Schrapel"    

    BR = 2: GOSUB horzBarR
    BR = 20: GOSUB horzBarR
    BR = 22: GOSUB horzBarR

    END

' -----------------------------------------------------------------------------
' update the PICO9918 palette (shades of blue)
' -----------------------------------------------------------------------------
updatePalette: PROCEDURE
    WAIT
    VDP_REG(47) = $c0 + 16 ' palette data port from index #2
    DEFINE VRAM 0, 32, defPal
    VDP_REG(47) = $40
    END