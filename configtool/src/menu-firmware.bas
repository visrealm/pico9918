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

' convert .UF2 block number to name table location for visualization
DEF FN BLOCK_XY(#I) = XY(#I % 30 + 1, #I / 30 + a_popupTop + 2)

' -----------------------------------------------------------------------------
' open the firmware menu
' -----------------------------------------------------------------------------
firmwareMenu: PROCEDURE

    menuTopRow = MENU_TITLE_ROW + 3
    
    DRAW_TITLE("FIRMWARE UPDATE", 15)

    VDP_ENABLE_INT
    GOSUB delay

    PRINT AT XY(4, menuTopRow + 0), "Current version : v"
    PRINT AT XY(4, menuTopRow + 1), "New version     : v",FIRMWARE_MAJOR_VER,".",FIRMWARE_MINOR_VER

    VDP_SET_CURRENT_STATUS_REG(12)  ' config
    VDP(58) = CONF_SW_VERSION
    optValue = VDP_READ_STATUS
    'verMaj = optValue / 16
    'verMin = optValue AND $0f
    VDP_RESET_STATUS_REG

    PRINT AT XY(23, menuTopRow + 0), verMaj, ".", verMin

    isUpgrade = 0
    IF verMaj < FIRMWARE_MAJOR_VER OR verMaj = FIRMWARE_MAJOR_VER AND verMin < FIRMWARE_MINOR_VER THEN
        isUpgrade = 1
    ELSEIF verMaj > FIRMWARE_MAJOR_VER OR verMaj = FIRMWARE_MAJOR_VER AND verMin > FIRMWARE_MINOR_VER THEN
        isUpgrade = -1
    END IF

    GOSUB verifyCartridgeFirmware

    CONST FWROWS = (#FIRMWARE_BLOCKS - 1) / 30 + 2
    STATUS = TRUE

    IF STATUS THEN

        DRAW_POPUP2(FWROWS, "      Upgrading firmware      ", 30)

        FOR #I = 0 TO #FIRMWARE_BLOCKS - 1
            PRINT AT BLOCK_XY(#I), "\001"
        NEXT #I

        IF 0 THEN

            IF isUpgrade = 0 THEN
                PRINT AT XY(2, menuTopRow + 5), "Re-install firmware"
            ELSEIF isUpgrade = 1 THEN
                PRINT AT XY(2, menuTopRow + 5), "Upgrade firmware to"
            ELSE
                PRINT AT XY(2, menuTopRow + 5), "Downgrade firmware to"
            END IF

            PRINT " v", FIRMWARE_MAJOR_VER, ".", FIRMWARE_MINOR_VER, "?"

            GOSUB firmwareWriteAndVerify

            PRINT AT XY(2, menuTopRow + 7), "          DONE!            "
        END IF
    END IF

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


' -----------------------------------------------------------------------------
' verify the firmware on this cart can be read in full
' -----------------------------------------------------------------------------
verifyCartridgeFirmware: PROCEDURE

    #FWBLOCK = 0
    STATUS = 1

    PRINT AT XY(2, menuTopRow + 3), "Verifying new firmware data..."

    I = 0
    FOR B = 1 TO FIRMWARE_BANKS
        BANKSEL(B)
        #FWOFFSET = 0
        PRINT AT XY(8, menuTopRow + 5), "Checking Bank: ", B

        IF bank1Start(0) <> B THEN
            STATUS = 0            
            PRINT AT XY(2, menuTopRow + 5), "Bank marker mismatch: ", bank1Start(0), " <> ", B
        ELSE
            FOR BL = 1 TO FIRMWARE_BLOCKS_PER_BANK

                blockFailed = FALSE

                FOR #UF2OFFSET = 0 TO 8
                    IF bank1Data(#FWOFFSET + #UF2OFFSET) <> uf2Header(#UF2OFFSET) THEN
                        blockFailed = TRUE
                        PRINT AT XY(2, menuTopRow + 7), "Block start marker not found"
                        EXIT FOR
                    END IF
                NEXT #UF2OFFSET

                #UF2BLOCK = bank1Data(#FWOFFSET + 20) + (bank1Data(#FWOFFSET + 21) * 256)

                IF #UF2BLOCK <> #FWBLOCK THEN
                    PRINT AT XY(1, menuTopRow + 7), "Block seq. mismatch: ", #UF2BLOCK, " <> ", #FWBLOCK
                    blockFailed = TRUE
                END IF

                FOR #UF2OFFSET = #FIRMWARE_BLOCK_BYTES - 4 TO #FIRMWARE_BLOCK_BYTES - 1
                    IF bank1Data(#FWOFFSET + #UF2OFFSET) <> uf2Header(#UF2OFFSET - 256) THEN
                        blockFailed = TRUE
                        PRINT AT XY(2, menuTopRow + 7), "Block end marker not found"
                        EXIT FOR
                    END IF
                NEXT #UF2OFFSET

                IF blockFailed THEN
                    PRINT AT XY(2, menuTopRow + 5), "Bank: ", B, ", Block: ", #FWBLOCK, " FAILED!"
                    STATUS = 0
                END IF

                IF STATUS = 0 THEN EXIT FOR

                #FWOFFSET = #FWOFFSET + #FIRMWARE_BLOCK_BYTES
                #FWBLOCK = #FWBLOCK + 1
                IF #FWBLOCK = #FIRMWARE_BLOCKS THEN EXIT FOR
            NEXT BL
            
        END IF
        IF STATUS = 0 THEN EXIT FOR
    NEXT B
    
    BANKSEL(0)

    IF STATUS = 1 THEN
        PRINT AT XY(1, menuTopRow + 3), "  New firmware data is valid   "
    ELSE
        PRINT AT XY(1, menuTopRow + 3), " New firmware data is invalid  "
    END IF

    END

' -----------------------------------------------------------------------------
' write the firmware
' -----------------------------------------------------------------------------
firmwareWriteAndVerify: PROCEDURE

    #FWBLOCK = 0

    I = 0
    FOR B = 1 TO FIRMWARE_BANKS
        BANKSEL(B)
        #FWOFFSET = 0
        FOR BL = 1 TO FIRMWARE_BLOCKS_PER_BANK
            VDP_DISABLE_INT

            DEFINE VRAM #VDP_FIRMWARE_DATA, #FIRMWARE_BLOCK_BYTES, VARPTR bank1Data(#FWOFFSET)
            DEFINE VRAM NAME_TAB_XY(0, menuTopRow + 6), 32, VARPTR bank1Data(#FWOFFSET)

            FWST = $c0 OR (#VDP_FIRMWARE_DATA / 256)

            PRINT AT XY(2, menuTopRow + 7), "Writing block ", #FWBLOCK + 1,"/",#FIRMWARE_BLOCKS

            VDP($3F) = FWST
            R = 0
            WHILE (FWST AND $80)
                VDP_SET_CURRENT_STATUS_REG(2)
                FWST = VDP_READ_STATUS
                VDP_RESET_STATUS_REG
                R = R + 1
            WEND

            VDP_ENABLE_INT

            WAIT

            #FWOFFSET = #FWOFFSET + #FIRMWARE_BLOCK_BYTES
            #FWBLOCK = #FWBLOCK + 1
            IF #FWBLOCK = #FIRMWARE_BLOCKS THEN EXIT FOR
        NEXT BL
    NEXT B

    BANKSEL(0)

    END

uf2Header:
  DATA BYTE $55, $46, $32, $0a ' magic start 0
  DATA BYTE $57, $51, $5d, $9e ' magic start 1

  DATA BYTE $00, $20, $00, $00 ' flags
  DATA BYTE $00, $00, $00, $10 ' target address
  DATA BYTE $00, $01, $00, $00 ' payload size (256)
  DATA BYTE $00, $00, $00, $00 ' block No
  DATA BYTE $ef, $00, $00, $00 ' block count
  DATA BYTE $56, $ff, $8b, $e4 ' family Id

  DATA BYTE $30, $6f, $b1, $0a ' magic end
