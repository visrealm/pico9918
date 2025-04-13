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

    menuTopRow = MENU_TITLE_ROW + 3
    
    DRAW_TITLE("FIRMWARE UPDATE", 15)

    VDP_ENABLE_INT
    GOSUB delay

    PRINT AT XY(2, menuTopRow + 0), "Current version :  v"
    PRINT AT XY(2, menuTopRow + 1), "New version     :  v",FIRMWARE_MAJOR_VER,".",FIRMWARE_MINOR_VER

    VDP_SET_CURRENT_STATUS_REG(12)  ' config
    VDP(58) = CONF_SW_VERSION
    optValue = VDP_READ_STATUS
    'verMaj = optValue / 16
    'verMin = optValue AND $0f
    VDP_RESET_STATUS_REG

    PRINT AT XY(22, menuTopRow + 0), verMaj, ".", verMin

    isUpgrade = 0
    IF verMaj < FIRMWARE_MAJOR_VER OR verMaj = FIRMWARE_MAJOR_VER AND verMin < FIRMWARE_MINOR_VER THEN
        isUpgrade = 1
    ELSEIF verMaj > FIRMWARE_MAJOR_VER OR verMaj = FIRMWARE_MAJOR_VER AND verMin > FIRMWARE_MINOR_VER THEN
        isUpgrade = -1
    END IF

    IF isUpgrade = 0 THEN
        PRINT AT XY(2, menuTopRow + 5), "Re-install firmware"
    ELSEIF isUpgrade = 1 THEN
        PRINT AT XY(2, menuTopRow + 5), "Upgrade firmware to"
    ELSE
        PRINT AT XY(2, menuTopRow + 5), "Downgrade firmware to"
    END IF

    PRINT " v", FIRMWARE_MAJOR_VER, ".", FIRMWARE_MINOR_VER, "?"

    #FWBLOCK = 0

    I = 0
    FOR B = 1 TO FIRMWARE_BANKS
        ON B FAST GOSUB ,selectBank1,selectBank2,selectBank3,selectBank4,selectBank5,selectBank6,selectBank7,selectBank8,selectBank9,selectBank10,selectBank11,selectBank12
        #FWOFFSET = 0
        FOR BL = 1 TO FIRMWARE_BLOCKS_PER_BANK

            VDP_DISABLE_INT

            DEFINE VRAM #VDP_FIRMWARE_DATA, #FIRMWARE_BLOCK_BYTES, VARPTR bank1Start(#FWOFFSET)
            DEFINE VRAM NAME_TAB_XY(0, menuTopRow + 6), 32, VARPTR bank1Start(#FWOFFSET)

            FWST = $c0 OR (#VDP_FIRMWARE_DATA / 256)

            PRINT AT XY(2, menuTopRow + 7), "Writing block ", #FWBLOCK + 1,"/",#FIRMWARE_BLOCKS

            PRINT AT XY(2, menuTopRow + 8), " SET REG                    "
            VDP($3F) = FWST
            R = 0
            WHILE (FWST AND $80)
                VDP_SET_CURRENT_STATUS_REG(2)
                FWST = VDP_READ_STATUS
                VDP_RESET_STATUS_REG
                PRINT AT XY(2, menuTopRow + 8), " REG OK ", R, " ",FWST,"   "
                R = R + 1
            WEND

            PRINT AT XY(2, menuTopRow + 9), FWST,"   "
            'EXIT FOR

            VDP_ENABLE_INT

            WAIT

            #FWOFFSET = #FWOFFSET + #FIRMWARE_BLOCK_BYTES
            #FWBLOCK = #FWBLOCK + 1
            IF #FWBLOCK = #FIRMWARE_BLOCKS THEN EXIT FOR
        NEXT BL
    NEXT B

    BANK SELECT 0

    PRINT AT XY(2, menuTopRow + 7), "          DONE!            "

    WHILE 1
        WAIT
        GOSUB updateNavInput

        IF (g_nav AND NAV_CANCEL) THEN EXIT WHILE
    WEND


    DRAW_POPUP ("Update progress", 30, 9)

    WHILE 1
        WAIT
        GOSUB updateNavInput

        IF (g_nav AND NAV_CANCEL) THEN EXIT WHILE
    WEND

    SET_MENU(MENU_ID_MAIN)
    END    
