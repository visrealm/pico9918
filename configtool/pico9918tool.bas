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
'
' BUILDING INSTRUCTIONS:
'
' 1. create ./asm and ./bin directories
' 2. cvbasic, gasm80 and xdt99 in path
'
' -- TI-99 --
'   > cvbasic --ti994a pico9918tool.bas asm/pico9918tool99.a99 lib
'   > xas99.py -b -R asm/pico9918tool99.a99
'   > linkticart.py asm/pico9918tool99.bin bin/pico9918tool99_8.bin "PICO9918 CONFIG TOOL"
'
' -- ColecoVision --
'   > cvbasic pico9918tool.bas asm/pico9918tool_cv.asm lib
'   > gasm80 asm/pico9918tool_cv.asm -o bin/pico9918tool_cv.rom
'
' -- MSX --
'   > cvbasic --msx pico9918tool.bas asm/pico9918tool_msx.asm lib
'   > gasm80 asm/pico9918tool_msx.asm -o bin/pico9918tool_msx.rom
'
' Cartridge images will be in the ./bin directory
'
' -----------------------------------------------------------------------------

    ' helper constants
    CONST TRUE           = -1
    CONST FALSE          = 0

    CONST MENU_TITLE_ROW   = 5
    CONST MENU_TOP_ROW     = 8

    ' pattern indixes
    CONST PATT_IDX_SELECTED_L = 20
    CONST PATT_IDX_SELECTED_R = 21
    CONST PATT_IDX_BORDER_H   = 22
    CONST PATT_IDX_BORDER_V   = 23
    CONST PATT_IDX_BORDER_TL  = 24
    CONST PATT_IDX_BORDER_TR  = 25
    CONST PATT_IDX_BORDER_BL  = 26
    CONST PATT_IDX_BORDER_BR  = 27


    ' Pico9918Options index, name[16], values index, num values,help[32]
    CONST CONF_COUNT      = 9
    CONST CONF_INDEX      = 0
    CONST CONF_LABEL      = 1
    CONST CONF_LABEL_LEN  = 16
    CONST CONF_VALUES_IND = (CONF_LABEL + CONF_LABEL_LEN)
    CONST CONF_NUM_VALUES = (CONF_VALUES_IND + 1)
    CONST CONF_HELP       = (CONF_NUM_VALUES + 1)
    CONST CONF_HELP_LEN   = 32
    CONST CONF_STRUCT_LEN = (CONF_HELP + CONF_HELP_LEN)

    ' config option value label length
    CONST CONF_VALUE_LABEL_LEN = 6

    ' -------------------------------
    ' PICO9918 Config Ids.
    ' See Pico9918Options enum in main.c
    CONST CONF_PICO_MODEL       = 0
    CONST CONF_HW_VERSION       = 1
    CONST CONF_SW_VERSION       = 2
    CONST CONF_CLOCK_TESTED     = 4
    CONST CONF_DISP_DRIVER      = 5
    ' ^^^ read only

    ' now the read/write ones
    CONST CONF_CRT_SCANLINES    = 8         ' 0 (off) or 1 (on)
    CONST CONF_SCANLINE_SPRITES = 9         ' 0 - 3 where value = (1 << (x + 2))
    CONST CONF_CLOCK_PRESET_ID  = 10        ' 0 - 2 see ClockSettings in main.c
    ' ^^^ read/write config IDs

    ' now the "special" config IDs
    CONST CONF_SAVE_TO_FLASH    = 255   
    ' -------------------------------

    CONST CONF_MENU_INFO        = 253
    CONST CONF_MENU_RESET       = 254
    CONST CONF_MENU_EMPTY       = 255
    CONST CONF_MENU_SAVE        = 250

    ' name table helpers
    DEF FN XY(X, Y) = ((Y) * 32 + (X))                      ' PRINT AT XY(1, 2), ...

    CONST #VDP_NAME_TAB  = $1800
    DEF FN NAME_TAB_XY(X, Y) = (#VDP_NAME_TAB + XY(X, Y))   ' DEFINE VRAM NAME_TAB_XY(1, 2), ...
    DEF FN PUT_XY(X, Y, C) = VPOKE NAME_TAB_XY(X, Y), C     ' place a byte in the name table
    DEF FN GET_XY(X, Y) = VPEEK(NAME_TAB_XY(X, Y))          ' read a byte from the name table

    CONST #VDP_SPRITE_ATTR = $1B00

    ' menu helpers
    DEF FN MENU_DATA(I, C) = configMenuData((I) * CONF_STRUCT_LEN + (C))
    DEF FN RENDER_MENU_ROW(R) = a_menuIndexToRender = R : WAIT : GOSUB renderMenuRow

    ' VDP helpers
    DEF FN VDP_DISABLE_INT = VDP(1) = $C2
    DEF FN VDP_ENABLE_INT = VDP(1) = $E2
    DEF FN VDP_WRITE_CONFIG(I, V) = VDP(58) = I : VDP(59) = V
    DEF FN VDP_READ_STATUS = USR RDVST
    DEF FN VDP_SET_CURRENT_STATUS_REG(R) = VDP(15) = R
    DEF FN VDP_RESET_STATUS_REG = VDP_SET_CURRENT_STATUS_REG(0)

    ' =========================================================================
    ' PROGRAM ENTRY
    ' -------------------------------------------------------------------------

    ' configuration values
    DIM tempConfigValues(CONF_COUNT)        ' current (live) config values
    DIM savedConfigValues(CONF_COUNT)       ' values saved in the PICO9918
    
    ' GLOBALS    
    g_currentMenuIndex = 0                  ' current menu index

    ' setup the screen
    BORDER 0
    GOSUB setupTiles
    GOSUB setupHeader
    
    ' what are we working with?
    GOSUB vdpDetect
   
    IF isF18ACompatible THEN

        ' looks like we're F18A compatible. do some more digging...
        
        VDP_DISABLE_INT

        VDP_SET_CURRENT_STATUS_REG(1)       ' SR1: ID
        statReg = VDP_READ_STATUS
        VDP_SET_CURRENT_STATUS_REG(14)      ' SR14: Version
        verReg = VDP_READ_STATUS
        VDP_RESET_STATUS_REG
        VDP_ENABLE_INT

        verMaj = verReg / 16
        verMin = verReg AND $0f
        IF (statReg AND $E8) = $E8 THEN
            PRINT AT XY(3, 3), "Detected: PICO9918 ver ", verMaj, ".", verMin
            isPico9918 = TRUE
        ELSEIF (statReg AND $E0) = $E0 THEN
            PRINT AT XY(5, 3), "Detected: F18A ver  ."
            PUT_XY(5 + 19, 3, hexChar(verMaj))
            PUT_XY(5 + 21, 3, hexChar(verMin))
        ELSE
            PRINT AT XY(8, 3), "Detected UNKNOWN SR1 = ", <>statReg
        END IF
        
    ELSE
        PRINT AT XY(6, 3), "Detected: LEGACY VDP"
    END IF

    isPico9918 = isF18ACompatible   ' FOR TESTING

    IF NOT isPico9918 THEN
        PRINT AT XY(7, 9 + (isF18ACompatible AND 3)), "PICO9918 not found"
        IF NOT isF18ACompatible THEN
            PRINT AT XY(15, 12), "OR"
            PRINT AT XY(3, 15), "PICO9918 firmware too old"
            PRINT AT XY(4, 17), "Firmware v1.0+ required"
            PRINT AT XY(4, 19), "Update manually via USB"
        END IF
    ELSE
        ' We are a PICO9918, set up the menu

        VDP(50) = $80  ' reset VDP registers to boot values
        VDP(0) = defaultReg(0)  ' VDP() doesn't accept variables, so...
        VDP(1) = defaultReg(1)
        VDP(2) = defaultReg(2)
        VDP(3) = defaultReg(3)
        VDP(4) = defaultReg(4)
        VDP(5) = defaultReg(5)
        VDP(6) = defaultReg(6)
        VDP(7) = defaultReg(7)

        VDP_ENABLE_INT  ' enable interrupts (so we can wait)
        WAIT    
        WAIT            ' ensure default config is now in place
        VDP_DISABLE_INT ' enable display, but interrupts still off

        GOSUB vdpUnlock ' reset locked the vdp. unlock it again

        GOSUB vdpLoadConfigValues  ' load config values from VDP

        GOSUB applyConfigValues

        ' render the menu
        GOSUB updatePalette

        g_currentMenu = 0

        WHILE 1
            ON g_currentMenu GOSUB mainMenu, deviceInfo
            GOSUB clearScreen
            VDP_DISABLE_INT
        WEND

    END IF

' -----------------------------------------------------------------------------
' end it all 
' -----------------------------------------------------------------------------
exit:
    WAIT
    GOTO exit

clearScreen: PROCEDURE
    DEFINE VRAM NAME_TAB_XY(0, 4), 32, horzBar
    FOR R = 5 TO 20
        PRINT AT XY(0, R), "                                "
    NEXT R
    END


' -----------------------------------------------------------------------------
' the top-level menu
' -----------------------------------------------------------------------------
mainMenu: PROCEDURE 

    DEFINE VRAM NAME_TAB_XY(11, MENU_TITLE_ROW - 1), 9, horzBar
    DEFINE VRAM NAME_TAB_XY(11, MENU_TITLE_ROW + 1), 9, horzBar
    PRINT AT XY(10, MENU_TITLE_ROW), "\23MAIN MENU\23"
    VPOKE NAME_TAB_XY(10, MENU_TITLE_ROW - 1), PATT_IDX_BORDER_TL
    VPOKE NAME_TAB_XY(10 + 10, MENU_TITLE_ROW - 1), PATT_IDX_BORDER_TR
    VPOKE NAME_TAB_XY(10, MENU_TITLE_ROW + 1), PATT_IDX_BORDER_BL
    VPOKE NAME_TAB_XY(10 + 10, MENU_TITLE_ROW + 1), PATT_IDX_BORDER_BR
    GOSUB renderMenu
    GOSUB initSprites

    VDP_ENABLE_INT
    GOSUB delay

    ' main menu loop
    WHILE 1
        WAIT

        IF g_currentMenuIndex = 1 THEN GOSUB animateSprites  ' do this first to ensure it's done within a frame

        key = CONT.KEY
        IF key >= $30 THEN key = key - $30

        lastMenuIndex = g_currentMenuIndex
        valueChanged = FALSE

        ' <down> button pressed?
        IF CONT.DOWN THEN  
            WHILE 1
                g_currentMenuIndex = g_currentMenuIndex + 1
                IF g_currentMenuIndex >= CONF_COUNT THEN g_currentMenuIndex = 0
                IF MENU_DATA(g_currentMenuIndex, CONF_INDEX) <> 255 THEN
                    EXIT WHILE
                END IF
            WEND
        
        ' <up> button pressed?
        ELSEIF CONT.UP THEN  
            WHILE 1
                g_currentMenuIndex = g_currentMenuIndex - 1
                IF g_currentMenuIndex >= CONF_COUNT THEN g_currentMenuIndex = CONF_COUNT - 1
                IF MENU_DATA(g_currentMenuIndex, CONF_INDEX) <> 255 THEN
                    EXIT WHILE
                END IF
            WEND

        ' number button pressed?
        ELSEIF key > 0 AND key <= CONF_COUNT THEN
            IF MENU_DATA(key - 1, CONF_INDEX) <> 255 THEN
                g_currentMenuIndex = key - 1
            END IF

        ' <fire>, <space> or <right> pressed? - next option value
        ELSEIF CONT.BUTTON OR (CONT1.KEY = 32) OR CONT.RIGHT THEN 
            IF MENU_DATA(g_currentMenuIndex, CONF_INDEX) - 1 < 200 THEN
                tempConfigValuesCount = MENU_DATA(g_currentMenuIndex, CONF_NUM_VALUES)
                currentValueIndex = tempConfigValues(g_currentMenuIndex)
                currentValueIndex = currentValueIndex + 1
                IF currentValueIndex >= tempConfigValuesCount THEN currentValueIndex = 0
                tempConfigValues(g_currentMenuIndex) = currentValueIndex
            END IF
            valueChanged = TRUE

        ' <left> pressed - previous option value
        ELSEIF CONT.LEFT THEN 
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
            GOSUB delay

        ' has the value changed for this config option?
        ELSEIF valueChanged THEN
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
            ELSEIF vdpOptId = CONF_MENU_INFO THEN
                g_currentMenu = 1
                EXIT WHILE
            ELSEIF vdpOptId = CONF_MENU_RESET THEN
                GOSUB resetOptions
            ELSEIF vdpOptId = CONF_MENU_SAVE THEN
                GOSUB saveOptions
            END IF
            GOSUB delay
        END IF
        
    WEND
    END

deviceInfo: PROCEDURE
    const PICO_MODEL_RP2040 = 1
    const PICO_MODEL_RP2350 = 2
    
    DEFINE VRAM NAME_TAB_XY(10, MENU_TITLE_ROW - 1), 11, horzBar
    DEFINE VRAM NAME_TAB_XY(10, MENU_TITLE_ROW + 1), 11, horzBar
    PRINT AT XY(9, MENU_TITLE_ROW), "\23DEVICE INFO\23"
    VPOKE NAME_TAB_XY(9, MENU_TITLE_ROW - 1), PATT_IDX_BORDER_TL
    VPOKE NAME_TAB_XY(9 + 12, MENU_TITLE_ROW - 1), PATT_IDX_BORDER_TR
    VPOKE NAME_TAB_XY(9, MENU_TITLE_ROW + 1), PATT_IDX_BORDER_BL
    VPOKE NAME_TAB_XY(9 + 12, MENU_TITLE_ROW + 1), PATT_IDX_BORDER_BR

    PRINT AT XY(2, 9), "Processor family : "
    PRINT AT XY(2,10), "Hardware version : "
    PRINT AT XY(2,11), "Software version : "
    PRINT AT XY(2,12), "Display driver   : "
    PRINT AT XY(2,13), "Resolution       : "
    PRINT AT XY(2,14), "F18A version     : "
    PRINT AT XY(2,15), "Core temperature : 0.0`C"

    VDP_SET_CURRENT_STATUS_REG(12)  ' config

    VDP(58) = CONF_PICO_MODEL
    optValue = VDP_READ_STATUS
    IF optValue = PICO_MODEL_RP2350 THEN
        PRINT AT XY(21, 9), "RP2350"
    ELSE
        PRINT AT XY(21, 9), "RP2040"
    END IF

    VDP(58) = CONF_HW_VERSION
    optValue = VDP_READ_STATUS
    verMaj = optValue / 16
    verMin = optValue AND $0f
    PRINT AT XY(21, 10), verMaj, ".", verMin
    IF verMaj = 1 THEN PRINT AT XY(24, 10), "+"

    VDP(58) = CONF_SW_VERSION
    optValue = VDP_READ_STATUS
    verMaj = optValue / 16
    verMin = optValue AND $0f
    PRINT AT XY(21, 11), verMaj, ".", verMin

    VDP(58) = CONF_DISP_DRIVER
    optValue = VDP_READ_STATUS
    IF optValue = 1 THEN
        PRINT AT XY(21, 12), "RGBs NTSC"
        PRINT AT XY(21, 13), "480i 60Hz"
    ELSEIF optValue = 2 THEN
        PRINT AT XY(21, 12), "RGBs PAL"
        PRINT AT XY(21, 13), "576i 50Hz"
    ELSE
        PRINT AT XY(21, 12), "VGA"
        PRINT AT XY(21, 13), "480p 60Hz"
    END IF

    VDP_SET_CURRENT_STATUS_REG(14)      ' SR14: Version
    optValue = VDP_READ_STATUS
    verMaj = optValue / 16
    verMin = optValue AND $0f
    PUT_XY(21, 14, hexChar(verMaj))
    PUT_XY(22, 14, ".")
    PUT_XY(23, 14, hexChar(verMin))
    VDP_RESET_STATUS_REG

    VDP_ENABLE_INT
    GOSUB delay

    WHILE 1
        WAIT
        IF CONT.BUTTON OR (CONT1.KEY = 32) OR CONT.LEFT THEN EXIT WHILE

        VDP_DISABLE_INT

        VDP_SET_CURRENT_STATUS_REG(13)      ' SR13: Temperature
        optValue = VDP_READ_STATUS
        tempC = optValue / 4
        tempDec = optValue AND $03
        tempDec = tempDec * 25

        PRINT AT XY(21,15), tempC, ".", tempDec, "`C  "

        VDP_RESET_STATUS_REG
        VDP_ENABLE_INT
    WEND

    g_currentMenu = 0

    END

' -----------------------------------------------------------------------------
' delay between user input (1/6 second)
' -----------------------------------------------------------------------------
delay: PROCEDURE
    FOR del = 1 TO 10
        WAIT
    NEXT del
    END

' -----------------------------------------------------------------------------
' reset options to defaults
' -----------------------------------------------------------------------------
resetOptions: PROCEDURE
    FOR I = 0 TO CONF_COUNT - 1
        tempConfigValues(I) = 0
    NEXT I
    GOSUB applyConfigValues
    GOSUB renderMenu
    END

' -----------------------------------------------------------------------------
' save the current config to PICO9918 flash
' -----------------------------------------------------------------------------
saveOptions: PROCEDURE
    
    configChanged = FALSE
    FOR I = 0 TO CONF_COUNT - 1
        IF savedConfigValues(I) <> tempConfigValues(I) THEN configChanged = TRUE
    NEXT I

    IF NOT configChanged THEN
        PRINT AT XY(0, 19), "  Skipped! No changes to save   "
        RETURN
    END IF

    ' instruct the pico9918 to commit config to flash
    VDP_WRITE_CONFIG(CONF_SAVE_TO_FLASH, 1)

    clockChanged = savedConfigValues(2) <> tempConfigValues(2)

    ' update device values again
    FOR I = 0 TO CONF_COUNT - 1
        savedConfigValues(I) = tempConfigValues(I)
    NEXT I
    GOSUB renderMenu

    ' if the clock frequency has changed... inform reboot
    IF clockChanged THEN
        PRINT AT XY(0, 19), " Success! ** Reboot required ** "
    ELSE
        PRINT AT XY(0, 19), "  Success! Configuration saved  "
    END IF
    END

' -----------------------------------------------------------------------------
' set up the various tile patters and colors
' -----------------------------------------------------------------------------
setupTiles: PROCEDURE
    VDP(1) = $82  ' disable interrupts and display

    DEFINE CHAR 32, 96, font        ' font standard
    DEFINE CHAR 32 + 128, 96, font  ' font highlighted

    DEFINE CHAR 1, 19, logo         ' first row of top left logo
    DEFINE CHAR 1 + 128, 19, logo2  ' second row of top left logo

    DEFINE CHAR PATT_IDX_BORDER_H, 6, lineSegments  '   border segments
    DEFINE CHAR PATT_IDX_BORDER_H + 128, 6, lineSegments

    DEFINE CHAR PATT_IDX_SELECTED_L, 1, highlightLeft   ' ends of selection bar
    DEFINE CHAR PATT_IDX_SELECTED_R, 1, highlightRight
    
    FOR I = 0 TO 31                 
        DEFINE COLOR I, 1, white    ' title color
    NEXT I
    FOR I = 32 TO 127
        DEFINE COLOR I, 1, grey     ' normal text color
    NEXT I
    FOR I = 128 TO 147
        DEFINE COLOR I, 1, white    ' title row 2 color
    NEXT I
    FOR I = 148 TO 250
        DEFINE COLOR I, 1, inv_white ' selected (highlighted) colors
    NEXT I

    DEFINE COLOR PATT_IDX_BORDER_H, 1, colorLineSegH     ' horizontal divideborder color
    FOR I = PATT_IDX_BORDER_V TO PATT_IDX_BORDER_BR
        DEFINE COLOR I, 1, colorLineSeg                  ' other border colors
    NEXT I    

    DEFINE COLOR PATT_IDX_BORDER_TL, 1, colorLineSegH                  
    DEFINE COLOR PATT_IDX_BORDER_TR, 1, colorLineSegH                  

    DEFINE COLOR PATT_IDX_SELECTED_L, 1, highlight    ' selection bar ends
    DEFINE COLOR PATT_IDX_SELECTED_R, 1, highlight

    DEFINE SPRITE 0, 7, logoSprites  ' set up logo sprites used for 'scanline sprites' demo

    SPRITE FLICKER OFF
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
    PRINT AT XY(4, 22), "(C) 2024 Troy Schrapel"    

    DEFINE VRAM NAME_TAB_XY(0, 2), 32, horzBar
    DEFINE VRAM NAME_TAB_XY(0, 4), 32, horzBar
    DEFINE VRAM NAME_TAB_XY(0, 21), 32, horzBar
    DEFINE VRAM NAME_TAB_XY(0, 23), 32, horzBar

    END

' -----------------------------------------------------------------------------
' apply current options to the PICO9918
' -----------------------------------------------------------------------------
applyConfigValues: PROCEDURE
    VDP(50) = tempConfigValues(0) * $04       ' set crt scanlines
    VDP(30) = pow2(tempConfigValues(1) + 2)   ' set scanline sprites
    END

' -----------------------------------------------------------------------------
' render all menu rows
' -----------------------------------------------------------------------------
renderMenu: PROCEDURE
    FOR a_menuIndexToRender = 0 TO CONF_COUNT - 1
        GOSUB renderMenuRow
    NEXT a_menuIndexToRender
    END

' -----------------------------------------------------------------------------
' render a menu row. Arguments: a_menuIndexToRender
' -----------------------------------------------------------------------------
renderMenuRow: PROCEDURE
    ' don't render special index 255
    IF MENU_DATA(a_menuIndexToRender, CONF_INDEX) = 255 THEN RETURN

    ' pre-compute row offset. we'll need this a few times
    #ROWOFFSET = XY(0, MENU_TOP_ROW + a_menuIndexToRender)

    ' output menu number (index + 1)
    PRINT AT #ROWOFFSET + 1, " ", a_menuIndexToRender + 1, ". "

    ' output menu label
    DEFINE VRAM #VDP_NAME_TAB + #ROWOFFSET + 5, CONF_LABEL_LEN, VARPTR configMenuData(a_menuIndexToRender * CONF_STRUCT_LEN + CONF_LABEL)
    PRINT AT #ROWOFFSET + 21, "          "

    ' determine and output config option value label
    valuesCount = MENU_DATA(a_menuIndexToRender, CONF_NUM_VALUES)
    IF valuesCount > 0 THEN
        valuesBaseIndex = MENU_DATA(a_menuIndexToRender, CONF_VALUES_IND)
        currentValueOffset = tempConfigValues(a_menuIndexToRender)

        ' output option value
        DEFINE VRAM #VDP_NAME_TAB + #ROWOFFSET + 22, 6, VARPTR configMenuOptionValueData((valuesBaseIndex + currentValueOffset) * CONF_VALUE_LABEL_LEN)
    END IF

    ' if thie config option is "dirty" output an asteric next to it
    IF savedConfigValues(a_menuIndexToRender) <> tempConfigValues(a_menuIndexToRender) THEN
        PRINT AT #ROWOFFSET + 29, "*"
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
    FOR R = 2 TO 29
        C = VPEEK(#VDP_NAME_TAB + #ROWOFFSET+ R)
        C = C OR 128
        VPOKE (#VDP_NAME_TAB + #ROWOFFSET + R), C
    NEXT R

    ' ends of highlight bar
    VPOKE (#VDP_NAME_TAB + #ROWOFFSET + 1),  PATT_IDX_SELECTED_L
    VPOKE (#VDP_NAME_TAB + #ROWOFFSET + 30), PATT_IDX_SELECTED_R

    ' output help line for the active menu item
    DEFINE VRAM #VDP_NAME_TAB + XY(0, 19), 32, VARPTR configMenuData(a_menuIndexToRender * CONF_STRUCT_LEN + CONF_HELP)
    END

' -----------------------------------------------------------------------------
' initialise the sprite attributes
' -----------------------------------------------------------------------------
initSprites: PROCEDURE
    CONST NUM_SPRITES = 16
    DIM spriteAttr(NUM_SPRITES * 4)

    xPos = 16

    FOR I = 0 TO NUM_SPRITES - 1
        spritePattIndex = logoSpriteIndices(I AND $07)
        spriteAttr(I * 4 + 0) = $d0
        spriteAttr(I * 4 + 1) = xPos
        spriteAttr(I * 4 + 2) = spritePattIndex * 4
        spriteAttr(I * 4 + 3) = 15
        xPos = xPos + logoSpriteWidths(spritePattIndex) + 1
        IF (I AND $07) = 7 THEN xPos = xPos + 8  ' small gap
    NEXT I

    spriteAttr(NUM_SPRITES * 4) = $d0
    END

' -----------------------------------------------------------------------------
' animate the sprites for 'scanline sprites' option
' -----------------------------------------------------------------------------
animateSprites: PROCEDURE

    CONST spritePosY = 135

    ' "static" values
    s_startAnimIndex = s_startAnimIndex + 3

    ' update all y positions
    FOR I = 0 TO NUM_SPRITES - 1
        spriteAttrIdx = I * 4
        spriteAttr(spriteAttrIdx) = spritePosY + sine((s_startAnimIndex + spriteAttr(spriteAttrIdx + 1)) AND $7f)
    NEXT I

    s_startSpriteIndex = s_startSpriteIndex + 1
    if s_startSpriteIndex >= NUM_SPRITES THEN s_startSpriteIndex = 0

    ' dump it to vram (sprite attribute table)
    DEFINE VRAM #VDP_SPRITE_ATTR, (NUM_SPRITES - s_startSpriteIndex) * 4, VARPTR spriteAttr(s_startSpriteIndex * 4)
    IF s_startSpriteIndex > 0 THEN
        DEFINE VRAM #VDP_SPRITE_ATTR + (NUM_SPRITES - s_startSpriteIndex) * 4, s_startSpriteIndex * 4, VARPTR spriteAttr(0)
    END IF

    END    

' -----------------------------------------------------------------------------
' hide the sprites when 'scanline sprites' option no longer selected
' -----------------------------------------------------------------------------
hideSprites: PROCEDURE
    SPRITE 0,$d0,0,0,0
    END

' -----------------------------------------------------------------------------
' update the PICO9918 palette (shades of blue)
' -----------------------------------------------------------------------------
updatePalette: PROCEDURE    
    WAIT
    VDP(47) = $c0 + 2 ' palette data port from index #2
    PRINT "\0\7"
    PRINT "\0\10"
    PRINT "\0\12"
    PRINT "\0\15"
    PRINT "\0\15"
    PRINT "\2\47"
    PRINT "\4\79"
    PRINT "\7\127"
    PRINT "\15\255"
    PRINT "\15\255"
    PRINT "\15\255"
    PRINT "\15\255"
    PRINT "\9\153"
    VDP(47) = $40
    END

' -----------------------------------------------------------------------------
' detect the vdp type. sets isF18ACompatible
' -----------------------------------------------------------------------------
vdpDetect: PROCEDURE
    GOSUB vdpUnlock
    DEFINE VRAM $3F00, 6, vdpGpuDetect
    VDP($36) = $3F                       ' set gpu start address msb
    VDP($37) = $00                       ' set gpu start address lsb (triggers)
    isF18ACompatible = VPEEK($3F00) = 0  ' check result
    END
    
' -----------------------------------------------------------------------------
' unlock F18A mode
' -----------------------------------------------------------------------------
vdpUnlock: PROCEDURE
    VDP_DISABLE_INT
    VDP(57) = $1C                       ' unlock
    VDP(57) = $1C                       ' unlock... again
    VDP_ENABLE_INT
    END

' -----------------------------------------------------------------------------
' load config valus from VDP to tempConfigValues() and savedConfigValues() arrays
' -----------------------------------------------------------------------------
vdpLoadConfigValues: PROCEDURE
    VDP_SET_CURRENT_STATUS_REG(12)    ' read config register
    FOR I = 0 TO CONF_COUNT - 1
        a_menuIndexToRender = MENU_DATA(I, CONF_INDEX)
        IF a_menuIndexToRender > 0 THEN
            VDP(58) = a_menuIndexToRender
            optValue = VDP_READ_STATUS            
            tempConfigValues(I) = optValue
            savedConfigValues(I) = optValue
        END IF
    NEXT I
    VDP_RESET_STATUS_REG
    END

' -----------------------------------------------------------------------------
' TMS9900 machine code (for PICO9918 GPU) to write $00 to VDP $3F00
' -----------------------------------------------------------------------------
vdpGpuDetect:
    DATA BYTE $04, $E0    ' CLR  @>3F00
    DATA BYTE $3F, $00
    DATA BYTE $03, $40    ' IDLE    


' -----------------------------------------------------------------------------
' Pico9918Options index, name[16], values index, num values, help[32]
' -----------------------------------------------------------------------------
configMenuData:
    DATA BYTE CONF_CRT_SCANLINES,   "CRT scanlines   ", 0, 2, "    Faux CRT scanline effect    "
    DATA BYTE CONF_SCANLINE_SPRITES,"Scanline sprites", 2, 4, "                                "
    DATA BYTE CONF_CLOCK_PRESET_ID, "Clock frequency ", 6, 3, " RP2040 clock (requires reboot) "
    DATA BYTE 0,                    "> Diagnostics   ", 0, 0, "   Manage diagnostics options   "
    DATA BYTE 0,                    "> Palette       ", 0, 0, "     Change default palette     "
    DATA BYTE CONF_MENU_INFO,       "> Device info.  ", 0, 0, "    View device information     "
    DATA BYTE CONF_MENU_RESET,      "Reset defaults  ", 0, 0, " Reset to default configuration "
    DATA BYTE CONF_MENU_EMPTY,      "                ", 0, 0, "                                "
    DATA BYTE CONF_MENU_SAVE,       "Save Settings   ", 0, 0, " Save configuration to PICO9918 "

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

hexChar:
    DATA BYTE "0123456789ABCDEF"

defaultReg: ' default VDP register values
    DATA BYTE $02, $82, $06, $FF, $03, $36, $07, $00

' PICO9918 logo pattern
logo:
    DATA BYTE $1f, $3f, $7f, $ff, $00, $00, $00, $00
    DATA BYTE $ff, $ff, $ff, $ff, $03, $01, $01, $03
    DATA BYTE $03, $c3, $e3, $f3, $f3, $f3, $f3, $f3
    DATA BYTE $e0, $e0, $e1, $e3, $e3, $e7, $e7, $e7
    DATA BYTE $1f, $7f, $ff, $ff, $f8, $e0, $c0, $c0
    DATA BYTE $ff, $fe, $fc, $f8, $00, $00, $00, $00
    DATA BYTE $00, $03, $0f, $1f, $1f, $3f, $3e, $3e
    DATA BYTE $7f, $ff, $ff, $ff, $c0, $00, $00, $00
    DATA BYTE $80, $f0, $fc, $fe, $fe, $3f, $1f, $1f
    DATA BYTE $07, $18, $20, $20, $41, $42, $41, $20
    DATA BYTE $ff, $00, $00, $00, $ff, $00, $ff, $00
    DATA BYTE $80, $60, $10, $08, $05, $85, $85, $04
    DATA BYTE $1f, $60, $80, $80, $07, $08, $07, $80
    DATA BYTE $fe, $01, $00, $00, $fc, $02, $fe, $00
    DATA BYTE $00, $81, $42, $24, $17, $10, $10, $10
    DATA BYTE $fc, $04, $04, $04, $84, $84, $84, $84
    DATA BYTE $1f, $60, $80, $80, $83, $84, $43, $20
    DATA BYTE $ff, $00, $00, $00, $fc, $02, $fc, $00
    DATA BYTE $80, $60, $10, $10, $10, $10, $20, $40
logo2:
    DATA BYTE $ff, $ff, $ff, $ff, $f8, $f8, $f8, $f8
    DATA BYTE $ff, $ff, $ff, $fe, $00, $00, $00, $00
    DATA BYTE $f3, $e3, $c3, $03, $03, $03, $03, $03
    DATA BYTE $e7, $e7, $e7, $e3, $e3, $e1, $e0, $e0
    DATA BYTE $c0, $c0, $e0, $f8, $ff, $ff, $7f, $1f
    DATA BYTE $00, $00, $00, $00, $ff, $fe, $fc, $f8
    DATA BYTE $3e, $3e, $3f, $1f, $1f, $0f, $03, $00
    DATA BYTE $00, $00, $00, $c0, $ff, $ff, $ff, $ff
    DATA BYTE $1f, $1f, $3f, $fe, $fe, $fc, $f0, $80
    DATA BYTE $18, $07, $00, $07, $08, $10, $20, $3f
    DATA BYTE $00, $ff, $00, $ff, $00, $00, $00, $ff
    DATA BYTE $04, $84, $84, $08, $08, $10, $60, $80
    DATA BYTE $60, $1f, $00, $1f, $20, $40, $80, $ff
    DATA BYTE $00, $fe, $02, $fc, $00, $00, $01, $fe
    DATA BYTE $10, $10, $10, $20, $20, $40, $80, $00
    DATA BYTE $84, $84, $84, $84, $84, $84, $84, $fc
    DATA BYTE $40, $83, $84, $83, $80, $80, $60, $1f
    DATA BYTE $00, $fc, $02, $fc, $00, $00, $00, $ff
    DATA BYTE $20, $10, $10, $10, $10, $10, $60, $80

highlightLeft:
    DATA BYTE $3F, $7F, $FF, $FF, $FF, $FF, $7F, $3F
highlightRight:
    DATA BYTE $FC, $FE, $FF, $FF, $FF, $FF, $FE, $FC

lineSegments:
    DATA BYTE $00, $00, $00, $ff, $ff, $00, $00, $00
    DATA BYTE $18, $18, $18, $18, $18, $18, $18, $18 ' vert
    DATA BYTE $00, $00, $00, $ff, $fF, $3C, $18, $18 ' tl
    DATA BYTE $00, $00, $00, $ff, $Ff, $3c, $18, $18 ' tr
 '   DATA BYTE $00, $00, $00, $07, $0F, $1C, $18, $18 ' tl
'    DATA BYTE $00, $00, $00, $E0, $F0, $38, $18, $18 ' tr
    DATA BYTE $18, $18, $1C, $0F, $07, $00, $00, $00 ' bl
    DATA BYTE $18, $18, $38, $F0, $E0, $00, $00, $00 ' br

colorLineSegH:
    DATA BYTE $00, $00, $00, $77, $44, $50, $50, $50
colorLineSeg:
    DATA BYTE $50, $50, $50, $50, $50, $50, $50, $50

horzBar:
    DATA BYTE PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H
    DATA BYTE PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H
    DATA BYTE PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H
    DATA BYTE PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H,PATT_IDX_BORDER_H

' PICO9918 logo name table entries (rows 1 and 2)
logoNames:
    DATA BYTE 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
logoNames2:
    DATA BYTE 129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147

' color entries for an entire tile
white: 
    DATA BYTE $f0, $f0, $f0, $f0, $f0, $f0, $f0, $f0
grey: 
    DATA BYTE $e0, $e0, $e0, $e0, $e0, $e0, $e0, $e0
blue: 
    DATA BYTE $40, $40, $40, $40, $40, $40, $40, $40
inv_white: 
    DATA BYTE $f9, $f8, $f7, $f6, $f5, $f4, $f3, $f2
highlight: 
    DATA BYTE $90, $80, $70, $60, $50, $40, $30, $20

font:
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $00 ' <SPACE$
    DATA BYTE $18, $18, $18, $18, $18, $00, $18, $00 ' !
    DATA BYTE $6C, $6C, $6C, $00, $00, $00, $00, $00 ' "
    DATA BYTE $6C, $6C, $FE, $6C, $FE, $6C, $6C, $00 ' #
    DATA BYTE $18, $7E, $C0, $7C, $06, $FC, $18, $00 ' $
    DATA BYTE $00, $C6, $CC, $18, $30, $66, $C6, $00 ' %
    DATA BYTE $38, $6C, $38, $76, $DC, $CC, $76, $00 ' &
    DATA BYTE $30, $30, $60, $00, $00, $00, $00, $00 ' '
    DATA BYTE $0C, $18, $30, $30, $30, $18, $0C, $00 ' (
    DATA BYTE $30, $18, $0C, $0C, $0C, $18, $30, $00 ' )
    DATA BYTE $00, $66, $3C, $FF, $3C, $66, $00, $00 ' *
    DATA BYTE $00, $18, $18, $7E, $18, $18, $00, $00 ' +
    DATA BYTE $00, $00, $00, $00, $00, $18, $18, $30 ' ,
    DATA BYTE $00, $00, $00, $7E, $00, $00, $00, $00 ' -
    DATA BYTE $00, $00, $00, $00, $00, $18, $18, $00 ' .
    DATA BYTE $06, $0C, $18, $30, $60, $C0, $80, $00 ' /
    DATA BYTE $7C, $CE, $DE, $F6, $E6, $C6, $7C, $00 ' 0
    DATA BYTE $18, $38, $18, $18, $18, $18, $7E, $00 ' 1
    DATA BYTE $7C, $C6, $06, $7C, $C0, $C0, $FE, $00 ' 2
    DATA BYTE $FC, $06, $06, $3C, $06, $06, $FC, $00 ' 3
    DATA BYTE $0C, $CC, $CC, $CC, $FE, $0C, $0C, $00 ' 4
    DATA BYTE $FE, $C0, $FC, $06, $06, $C6, $7C, $00 ' 5
    DATA BYTE $7C, $C0, $C0, $FC, $C6, $C6, $7C, $00 ' 6
    DATA BYTE $FE, $06, $06, $0C, $18, $30, $30, $00 ' 7
    DATA BYTE $7C, $C6, $C6, $7C, $C6, $C6, $7C, $00 ' 8
    DATA BYTE $7C, $C6, $C6, $7E, $06, $06, $7C, $00 ' 9
    DATA BYTE $00, $18, $18, $00, $00, $18, $18, $00 ' :
    DATA BYTE $00, $18, $18, $00, $00, $18, $18, $30 ' ;
    DATA BYTE $0C, $18, $30, $60, $30, $18, $0C, $00 ' <
    DATA BYTE $00, $00, $7E, $00, $7E, $00, $00, $00 ' =
    DATA BYTE $30, $18, $0C, $06, $0C, $18, $30, $00 ' >
    DATA BYTE $3C, $66, $0C, $18, $18, $00, $18, $00 ' ?
    DATA BYTE $7C, $C6, $DE, $DE, $DE, $C0, $7E, $00 ' @
    DATA BYTE $38, $6C, $C6, $C6, $FE, $C6, $C6, $00 ' A
    DATA BYTE $FC, $C6, $C6, $FC, $C6, $C6, $FC, $00 ' B
    DATA BYTE $7C, $C6, $C0, $C0, $C0, $C6, $7C, $00 ' C
    DATA BYTE $F8, $CC, $C6, $C6, $C6, $CC, $F8, $00 ' D
    DATA BYTE $FE, $C0, $C0, $F8, $C0, $C0, $FE, $00 ' E
    DATA BYTE $FE, $C0, $C0, $F8, $C0, $C0, $C0, $00 ' F
    DATA BYTE $7C, $C6, $C0, $C0, $CE, $C6, $7C, $00 ' G
    DATA BYTE $C6, $C6, $C6, $FE, $C6, $C6, $C6, $00 ' H
    DATA BYTE $7E, $18, $18, $18, $18, $18, $7E, $00 ' I
    DATA BYTE $06, $06, $06, $06, $06, $C6, $7C, $00 ' J
    DATA BYTE $C6, $CC, $D8, $F0, $D8, $CC, $C6, $00 ' K
    DATA BYTE $C0, $C0, $C0, $C0, $C0, $C0, $FE, $00 ' L
    DATA BYTE $C6, $EE, $FE, $FE, $D6, $C6, $C6, $00 ' M
    DATA BYTE $C6, $E6, $F6, $DE, $CE, $C6, $C6, $00 ' N
    DATA BYTE $7C, $C6, $C6, $C6, $C6, $C6, $7C, $00 ' O
    DATA BYTE $FC, $C6, $C6, $FC, $C0, $C0, $C0, $00 ' P
    DATA BYTE $7C, $C6, $C6, $C6, $D6, $DE, $7C, $06 ' Q
    DATA BYTE $FC, $C6, $C6, $FC, $D8, $CC, $C6, $00 ' R
    DATA BYTE $7C, $C6, $C0, $7C, $06, $C6, $7C, $00 ' S
    DATA BYTE $FF, $18, $18, $18, $18, $18, $18, $00 ' T
    DATA BYTE $C6, $C6, $C6, $C6, $C6, $C6, $FE, $00 ' U
    DATA BYTE $C6, $C6, $C6, $C6, $C6, $7C, $38, $00 ' V
    DATA BYTE $C6, $C6, $C6, $C6, $D6, $FE, $6C, $00 ' W
    DATA BYTE $C6, $C6, $6C, $38, $6C, $C6, $C6, $00 ' X
    DATA BYTE $C6, $C6, $C6, $7C, $18, $30, $E0, $00 ' Y
    DATA BYTE $FE, $06, $0C, $18, $30, $60, $FE, $00 ' Z
    DATA BYTE $3C, $30, $30, $30, $30, $30, $3C, $00 ' [
    DATA BYTE $C0, $60, $30, $18, $0C, $06, $02, $00 ' \
    DATA BYTE $3C, $0C, $0C, $0C, $0C, $0C, $3C, $00 ' ]
    DATA BYTE $10, $38, $6C, $C6, $00, $00, $00, $00 ' ^
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $FF ' _
    DATA BYTE $18, $24, $24, $18, $00, $00, $00, $00 ' ` 
    DATA BYTE $00, $00, $7C, $06, $7E, $C6, $7E, $00 ' a
    DATA BYTE $C0, $C0, $C0, $FC, $C6, $C6, $FC, $00 ' b
    DATA BYTE $00, $00, $7C, $C6, $C0, $C6, $7C, $00 ' c
    DATA BYTE $06, $06, $06, $7E, $C6, $C6, $7E, $00 ' d
    DATA BYTE $00, $00, $7C, $C6, $FE, $C0, $7C, $00 ' e
    DATA BYTE $1C, $36, $30, $78, $30, $30, $78, $00 ' f
    DATA BYTE $00, $00, $7E, $C6, $C6, $7E, $06, $FC ' g
    DATA BYTE $C0, $C0, $FC, $C6, $C6, $C6, $C6, $00 ' h
    DATA BYTE $18, $00, $38, $18, $18, $18, $3C, $00 ' i
    DATA BYTE $06, $00, $06, $06, $06, $06, $C6, $7C ' j
    DATA BYTE $C0, $C0, $CC, $D8, $F8, $CC, $C6, $00 ' k
    DATA BYTE $38, $18, $18, $18, $18, $18, $3C, $00 ' l
    DATA BYTE $00, $00, $CC, $FE, $FE, $D6, $D6, $00 ' m
    DATA BYTE $00, $00, $FC, $C6, $C6, $C6, $C6, $00 ' n
    DATA BYTE $00, $00, $7C, $C6, $C6, $C6, $7C, $00 ' o
    DATA BYTE $00, $00, $FC, $C6, $C6, $FC, $C0, $C0 ' p
    DATA BYTE $00, $00, $7E, $C6, $C6, $7E, $06, $06 ' q
    DATA BYTE $00, $00, $FC, $C6, $C0, $C0, $C0, $00 ' r
    DATA BYTE $00, $00, $7E, $C0, $7C, $06, $FC, $00 ' s
    DATA BYTE $18, $18, $7E, $18, $18, $18, $0E, $00 ' t
    DATA BYTE $00, $00, $C6, $C6, $C6, $C6, $7E, $00 ' u
    DATA BYTE $00, $00, $C6, $C6, $C6, $7C, $38, $00 ' v
    DATA BYTE $00, $00, $C6, $C6, $D6, $FE, $6C, $00 ' w
    DATA BYTE $00, $00, $C6, $6C, $38, $6C, $C6, $00 ' x
    DATA BYTE $00, $00, $C6, $C6, $C6, $7E, $06, $FC ' y
    DATA BYTE $00, $00, $FE, $0C, $38, $60, $FE, $00 ' z
    DATA BYTE $0E, $18, $18, $70, $18, $18, $0E, $00 ' {
    DATA BYTE $18, $18, $18, $00, $18, $18, $18, $00 ' |
    DATA BYTE $70, $18, $18, $0E, $18, $18, $70, $00 ' }
    DATA BYTE $76, $DC, $00, $00, $00, $00, $00, $00 ' ~
    DATA BYTE $FF, $FF, $FF, $FF, $FF, $FF, $FF, $FF '  

logoSprites: ' logo sprites for 'scanline sprites' demo
    DATA BYTE $3F, $7F, $FF, $00, $00, $FF, $FF, $FF    ' P
    DATA BYTE $E0, $E0, $E0, $00, $00, $00, $00, $00    ' 
    DATA BYTE $E0, $F8, $FC, $3C, $3C, $FC, $F8, $F0    ' 
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $00    ' 
    DATA BYTE $F0, $F0, $F0, $F0, $F0, $F0, $F0, $F0    ' I
    DATA BYTE $F0, $F0, $F0, $00, $00, $00, $00, $00    ' 
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $00    ' 
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $00    ' 
    DATA BYTE $0F, $3F, $7F, $F0, $E0, $E0, $E0, $F0    ' C
    DATA BYTE $7F, $3F, $0F, $00, $00, $00, $00, $00    ' 
    DATA BYTE $F8, $F0, $E0, $00, $00, $00, $00, $00    ' 
    DATA BYTE $F8, $F0, $E0, $00, $00, $00, $00, $00    ' 
    DATA BYTE $0F, $3F, $7F, $F0, $E0, $E0, $E0, $F0    ' O
    DATA BYTE $7F, $3F, $0F, $00, $00, $00, $00, $00    ' 
    DATA BYTE $E0, $F8, $FC, $1E, $0E, $0E, $0E, $1E    ' 
    DATA BYTE $FC, $F8, $E0, $00, $00, $00, $00, $00    ' 
    DATA BYTE $3F, $40, $8F, $90, $8F, $40, $3F, $00    ' 9
    DATA BYTE $3F, $40, $FF, $00, $00, $00, $00, $00    ' 
    DATA BYTE $F0, $08, $C4, $24, $E4, $04, $E4, $24    ' 
    DATA BYTE $C4, $08, $F0, $00, $00, $00, $00, $00    ' 
    DATA BYTE $3C, $44, $84, $E4, $24, $24, $24, $24    ' 1
    DATA BYTE $24, $24, $3C, $00, $00, $00, $00, $00    ' 
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $00    ' 
    DATA BYTE $00, $00, $00, $00, $00, $00, $00, $00    ' 
    DATA BYTE $3F, $40, $8F, $90, $4F, $40, $8F, $90    ' 8
    DATA BYTE $8F, $40, $3F, $00, $00, $00, $00, $00    ' 
    DATA BYTE $F0, $08, $C4, $24, $C8, $08, $C4, $24    ' 
    DATA BYTE $C4, $08, $F0, $00, $00, $00, $00, $00    ' 

logoSpriteWidths:
    DATA BYTE 14, 4, 13, 15, 14, 6, 14

logoSpriteIndices:  ' P, I, C, O, 9, 9, 1, 8
    DATA BYTE 0, 1, 2, 3, 4, 4,5, 6

palette: ' not currently used, but I'd prefer to use it. It stays!
    DATA BYTE $00, $00
    DATA BYTE $00, $00
    DATA BYTE $02, $C3
    DATA BYTE $05, $00
    DATA BYTE $05, $4F
    DATA BYTE $07, $6F
    DATA BYTE $0D, $54
    DATA BYTE $04, $EF
    DATA BYTE $0F, $54
    DATA BYTE $0F, $76
    DATA BYTE $0D, $C3
    DATA BYTE $0E, $D6
    DATA BYTE $02, $B2
    DATA BYTE $0C, $5C
    DATA BYTE $08, $88
    DATA BYTE $0F, $FF
  
sine: ' sine wave values for scanline sprite animation
    DATA BYTE $10, $10, $11, $11, $12, $12, $12, $13
    DATA BYTE $13, $13, $14, $14, $14, $15, $15, $15
    DATA BYTE $16, $16, $16, $16, $17, $17, $17, $17
    DATA BYTE $17, $18, $18, $18, $18, $18, $18, $18
    DATA BYTE $18, $18, $18, $18, $18, $18, $18, $18
    DATA BYTE $17, $17, $17, $17, $17, $16, $16, $16
    DATA BYTE $16, $15, $15, $15, $14, $14, $14, $13
    DATA BYTE $13, $13, $12, $12, $12, $11, $11, $10
    DATA BYTE $10, $10, $0F, $0F, $0E, $0E, $0E, $0D
    DATA BYTE $0D, $0D, $0C, $0C, $0C, $0B, $0B, $0B
    DATA BYTE $0A, $0A, $0A, $0A, $09, $09, $09, $09
    DATA BYTE $09, $08, $08, $08, $08, $08, $08, $08
    DATA BYTE $08, $08, $08, $08, $08, $08, $08, $08
    DATA BYTE $09, $09, $09, $09, $09, $0A, $0A, $0A
    DATA BYTE $0A, $0B, $0B, $0B, $0C, $0C, $0C, $0D
    DATA BYTE $0D, $0D, $0E, $0E, $0E, $0F, $0F, $10

pow2: ' 1 << INDEX
    DATA BYTE $01, $02, $04, $08, $10, $20, $40, $80
