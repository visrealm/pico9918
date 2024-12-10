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

paletteMenu: PROCEDURE

    DRAW_TITLE("PALETTE", 7)

    DIM bmpBuf(64)

    FOR I = 0 TO 14
        bmpBuf(I * 2) = PATT_IDX_BOX_TL
        bmpBuf(I * 2 + 1) = PATT_IDX_BOX_TR
        bmpBuf(32 + I * 2) = PATT_IDX_BOX_BL
        bmpBuf(32 + I * 2 + 1) = PATT_IDX_BOX_BR
    NEXT I

    DEFINE VRAM NAME_TAB_XY(1, 8), 30, VARPTR bmpBuf(0)
    DEFINE VRAM NAME_TAB_XY(1, 9), 30, VARPTR bmpBuf(32)

    FOR I = 0 TO 64
        bmpBuf(I) = 0
    NEXT I

    CONST BITMAP_WIDTH  = 16 * 15
    CONST BITMAP_HEIGHT = 13

    ' Bitmap layer
    ' Total VRAM required is:
    '   BITMAP_WIDTH / 4 * BITMAP_HEIGHT: 60 * 13 = 780 B
    VDP(31) = $f0           ' bml en, pri, trans, fat, pal = 0
    VDP(32) = $70           ' $1C00 >> 6 
    VDP(33) = 10            ' x
    VDP(34) = 65            ' y
    VDP(35) = BITMAP_WIDTH  ' w
    VDP(36) = BITMAP_HEIGHT ' h

    CONST PAL_SWATCH_STRIDE = (BITMAP_WIDTH / 4)


    FOR C = 1 TO 15
        col = C * 16 + C
        bmpBuf((C * 4) - 4) = col
        bmpBuf((C * 4) - 3) = col
        bmpBuf((C * 4) - 2) = col
    NEXT C

    FOR R = 0 TO 12
        DEFINE VRAM $1C00 + (R * PAL_SWATCH_STRIDE), PAL_SWATCH_STRIDE, VARPTR bmpBuf(0)
    NEXT R

    FOR I = 1 TO 15
        PUT_XY( I * 2 - 1, 7, hexChar(I))
    NEXT I

    bmpBuf(0) = PATT_IDX_BOX_TL
    bmpBuf(1) = PATT_IDX_BOX_TR
    bmpBuf(2) = PATT_IDX_BOX_BL
    bmpBuf(3) = PATT_IDX_BOX_BR
    bmpBuf(4) = PATT_IDX_BOX_TL + 128
    bmpBuf(5) = PATT_IDX_BOX_TR + 128
    bmpBuf(6) = PATT_IDX_BOX_BL + 128
    bmpBuf(7) = PATT_IDX_BOX_BR + 128

    PRINT AT XY(1,11), "Red:"
    PRINT AT XY(1,13), "Green:"
    PRINT AT XY(1,15), "Blue:"

    FOR I = 11 to 15
        PRINT AT XY(26, I) , "\148\148\148\148\148"
    NEXT I

    FOR I = 11 to 15 STEP 2
        DEFINE VRAM NAME_TAB_XY(8, I), 16, horzBar
    NEXT I

    PRINT AT XY(10,17) , "Reset to defaults"
    PRINT AT XY(10,18),  "Save settings"

    currentIndex = 1
    lastIndex = 15

    currentMenu = 0 ' 0 = pal, 1 = r, 2 = g, 3 = b
    lastMenu    = 0
    DIM rgb(3)

    VDP_ENABLE_INT

    GOSUB delay

    DIM currentColor(2)

    WHILE 1
        WAIT
        
        'DEFINE VRAM NAME_TAB_XY(15,15), 1, VARPTR I


        IF currentMenu = 0 THEN
            SPRITE 0, $d0, 0,0,0
            DEFINE VRAM NAME_TAB_XY(currentIndex * 2 - 1, 8), 2, VARPTR bmpBuf(0 + (FRAME AND 8) / 2)
            DEFINE VRAM NAME_TAB_XY(currentIndex * 2 - 1, 9), 2, VARPTR bmpBuf(2 + (FRAME AND 8) / 2)
            IF lastIndex <> currentIndex THEN
                DEFINE VRAM NAME_TAB_XY(lastIndex * 2 - 1, 8), 2, VARPTR bmpBuf(0)
                DEFINE VRAM NAME_TAB_XY(lastIndex * 2 - 1, 9), 2, VARPTR bmpBuf(2)
                DEFINE VRAM NAME_TAB_XY(currentIndex * 2 - 1, 8), 2, VARPTR bmpBuf(0 + 4)
                DEFINE VRAM NAME_TAB_XY(currentIndex * 2 - 1, 9), 2, VARPTR bmpBuf(2 + 4)

                VDP_DISABLE_INT

                VDP_SET_CURRENT_STATUS_REG(12)    ' read config register
                VDP(58) = 128 + currentIndex * 2
                currentColor(0) = VDP_READ_STATUS
                VDP(58) = 128 + currentIndex * 2 + 1
                currentColor(1) = VDP_READ_STATUS            
                VDP_RESET_STATUS_REG

                currentColor(0) = defPal(currentIndex * 2)
                currentColor(1) = defPal(currentIndex * 2 + 1)

                rgb(0) = currentColor(0) AND $0f
                rgb(1) = currentColor(1) / 16
                rgb(2) = currentColor(1) AND $0f

                VDP(47) = $c0 + 16 + 10 ' palette data port from pal 2 index #10
                DEFINE VRAM 0, 2, VARPTR currentColor(0)
                VDP(47) = $40
                VDP_ENABLE_INT

                PUT_XY(0, 5, hexChar(rgb(0)))
                PUT_XY(1, 5, hexChar(rgb(1)))
                PUT_XY(2, 5, hexChar(rgb(2)))

                FOR I = 11 to 15 STEP 2
                    DEFINE VRAM NAME_TAB_XY(8, I), 16, horzBar
                NEXT I

                PUT_XY(8 + rgb(0), 11, PATT_IDX_SLIDER)            
                PUT_XY(8 + rgb(1), 13, PATT_IDX_SLIDER)            
                PUT_XY(8 + rgb(2), 15, PATT_IDX_SLIDER)            
            
                lastIndex = currentIndex
                GOSUB delay
            END IF
        ELSEIF currentMenu < 4 THEN
                SPRITE 0, 8 * (9 + (currentMenu * 2)) - 1, 8 * (8 + rgb(currentMenu - 1)),32,(FRAME AND 8)+7

                cc1 = rgb(1) * 16 + rgb(2)
                IF currentColor(0) <> rgb(0) OR currentColor(1) <> cc1 THEN 
                    DEFINE VRAM NAME_TAB_XY(8, 9 + (currentMenu * 2)), 16, horzBar
                    PUT_XY(8 + rgb(currentMenu - 1), 9 + (currentMenu * 2), PATT_IDX_SLIDER)            

                    currentColor(0) = rgb(0)
                    currentColor(1) = cc1

                    VDP(47) = $c0 + 16 + 10 ' palette data port from pal 2 index #10
                    DEFINE VRAM 0, 2, VARPTR currentColor(0)
                    VDP(47) = $40
                    VDP_ENABLE_INT
                    GOSUB delay
                END IF
        END IF

        IF lastMenu <> currentMenu THEN
            GOSUB delay
            lastMenu = currentMenu
        END IF

        GOSUB updateNavInput

'        IF (CONT1.KEY > 0 AND CONT1.KEY < 10) THEN
'            currentIndex = CONT1.KEY
        IF currentMenu = 0 THEN
            IF g_nav AND NAV_LEFT THEN
                currentIndex = currentIndex - 1
                if currentIndex = 0 THEN currentIndex = 15
            ELSEIF g_nav AND NAV_RIGHT THEN
                currentIndex = currentIndex + 1
                if currentIndex > 15 THEN currentIndex = 1
            ELSEIF g_nav AND NAV_DOWN THEN
                currentMenu = currentMenu + 1
                DEFINE VRAM NAME_TAB_XY(currentIndex * 2 - 1, 8), 2, VARPTR bmpBuf(0)
                DEFINE VRAM NAME_TAB_XY(currentIndex * 2 - 1, 9), 2, VARPTR bmpBuf(2)
            END IF

        ELSEIF currentMenu < 4 THEN
            rgbIndex = currentMenu - 1
            IF g_nav AND NAV_DOWN THEN
                currentMenu = currentMenu + 1
            ELSEIF g_nav AND NAV_UP THEN
                currentMenu = currentMenu - 1
            ELSEIF g_nav AND NAV_LEFT AND rgb(rgbIndex) > 0 THEN
                rgb(rgbIndex) = rgb(rgbIndex) - 1
            ELSEIF g_nav AND NAV_RIGHT  AND rgb(rgbIndex) < 15 THEN
                rgb(rgbIndex) = rgb(rgbIndex) + 1
            END IF
        END IF

        IF g_nav AND NAV_CANCEL THEN EXIT WHILE
    WEND

    VDP(31) = $00   ' bml en, pri, trans, fat, pal = 0
    
    SET_MENU(MENU_ID_MAIN)
    END

defPal:
  DATA BYTE $00, $00, $F0, $00, $F2, $C3, $F5, $D6, $F5, $4F, $F7, $6F, $FD, $54, $F4, $EF, $FF, $54, $FF, $76, $FD, $C3, $FE, $D6, $F2, $B2, $FC, $5C, $FC, $CC, $FF, $FF

