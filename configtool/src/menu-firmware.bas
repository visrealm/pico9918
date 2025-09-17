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

#if BANK_SIZE
' convert .UF2 block number to name table location for visualization
DEF FN BLOCKPOS(#I) = XY((#I) % 30 + 1, (#I) / 30 + a_popupTop + 2)

#endif

CONST FWROWS = (#FIRMWARE_BLOCKS - 1) / 30 + 2

' -----------------------------------------------------------------------------
' open the firmware menu
' -----------------------------------------------------------------------------
firmwareMenu: PROCEDURE


    g_menuTopRow = MENU_TITLE_ROW + 3   ' WTF? For some reason I need this line twice???? At least on TI-99

    DRAW_TITLE("FIRMWARE UPDATE")

    PRINT AT XY(4, g_menuTopRow + 0), "Current version : v",verMajor, ".", verMinor, ".", verPatch
    PRINT AT XY(4, g_menuTopRow + 1), "New version     : v",FIRMWARE_MAJOR_VER,".",FIRMWARE_MINOR_VER,".",FIRMWARE_PATCH_VER


#if BANK_SIZE

    GOSUB verifyCartridgeFirmware

    IF STATUS THEN

        DRAW_POPUP_W("Update firmware?", 5, 20)

        GOSUB confirmationMenuLoop

        R = 19: GOSUB emptyRowR

        IF confirm THEN

            DRAW_POPUP_W("Upgrading firmware :        ", FWROWS, 30)

            WAIT

            FOR #FWBLOCK = 0 TO #FIRMWARE_BLOCKS - 1
                PRINT AT BLOCKPOS(#FWBLOCK), CHR$(1)
            NEXT #FWBLOCK

            GOSUB firmwareWriteAndVerify
        END IF
    END IF
#endif

    SET_MENU(MENU_ID_MAIN)
    END    

#if BANK_SIZE

' -----------------------------------------------------------------------------
' verify the firmware on this cart can be read in full
' -----------------------------------------------------------------------------
verifyCartridgeFirmware: PROCEDURE

    #FWBLOCK = 0
    STATUS = 1

    VDP_DISABLE_INT

    PRINT AT XY(2, g_menuTopRow + 11), "Verifying new firmware data..."

    I = 0
    FOR B = 1 TO FIRMWARE_BANKS
        BANKSEL(B)
        #FWOFFSET = 0
        PRINT AT XY(8, g_menuTopRow + 5), "Checking Bank: ", B

        IF bank1Start(0) <> B THEN
            STATUS = 0            
            PRINT AT XY(2, g_menuTopRow + 5), "Bank marker mismatch: ", bank1Start(0), " <> ", B
        ELSE
            FOR BL = 1 TO FIRMWARE_BLOCKS_PER_BANK

                blockFailed = FALSE

                FOR #UF2OFFSET = 0 TO 8
                    IF bank1Data(#FWOFFSET + #UF2OFFSET) <> uf2Header(#UF2OFFSET) THEN
                        blockFailed = TRUE
                        PRINT AT XY(2, g_menuTopRow + 7), "Block start marker not found"
                        EXIT FOR
                    END IF
                NEXT #UF2OFFSET

                #UF2BLOCK = bank1Data(#FWOFFSET + 20) + (bank1Data(#FWOFFSET + 21) * 256)

                IF #UF2BLOCK <> #FWBLOCK THEN
                    PRINT AT XY(1, g_menuTopRow + 7), "Block seq. mismatch: ", #UF2BLOCK, " <> ", #FWBLOCK
                    blockFailed = TRUE
                END IF

                FOR #UF2OFFSET = #FIRMWARE_BLOCK_BYTES - 4 TO #FIRMWARE_BLOCK_BYTES - 1
                    IF bank1Data(#FWOFFSET + #UF2OFFSET) <> uf2Header(#UF2OFFSET - 256) THEN
                        blockFailed = TRUE
                        PRINT AT XY(2, g_menuTopRow + 7), "Block end marker not found"
                        EXIT FOR
                    END IF
                NEXT #UF2OFFSET

                IF blockFailed THEN
                    PRINT AT XY(2, g_menuTopRow + 5), "Bank: ", B, ", Block: ", #FWBLOCK, " FAILED!"
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
        PRINT AT XY(1, g_menuTopRow + 11), "  New firmware data is valid   "
    ELSE
        PRINT AT XY(1, g_menuTopRow + 11), " New firmware data is invalid  "
    END IF

    VDP_ENABLE_INT

    END

' -----------------------------------------------------------------------------
' write the firmware
' -----------------------------------------------------------------------------
firmwareWriteAndVerify: PROCEDURE

    #FWBLOCK = 0

    STATUS = 1

    FOR B = 1 TO FIRMWARE_BANKS
        BANKSEL(B)
        #FWOFFSET = 0
        FOR BL = 1 TO FIRMWARE_BLOCKS_PER_BANK
            VDP_DISABLE_INT

            DEFINE VRAM #VDP_FIRMWARE_DATA, #FIRMWARE_BLOCK_BYTES, VARPTR bank1Data(#FWOFFSET)
            PRINT AT XY(23, a_popupTop), <3>(#FWBLOCK + 1),"/",#FIRMWARE_BLOCKS

            FWST = $c0 OR (#VDP_FIRMWARE_DATA / 256)

            VDP_REG($3F) = FWST
            R = 0
            WHILE (FWST AND $80)
                VDP_STATUS_REG = 2
                FWST = VDP_STATUS
                VDP_STATUS_REG0
                R = R + 1
            WEND

            IF FWST AND $1c THEN
                PRINT AT BLOCKPOS(#FWBLOCK), CHR$(2)
                STATUS = 0
            ELSE
                PRINT AT BLOCKPOS(#FWBLOCK), CHR$(0)
            END IF


            VDP_ENABLE_INT

            I = I + 1

            WAIT

            #FWOFFSET = #FWOFFSET + #FIRMWARE_BLOCK_BYTES
            #FWBLOCK = #FWBLOCK + 1
            IF #FWBLOCK = #FIRMWARE_BLOCKS THEN EXIT FOR
        NEXT BL
    NEXT B

    BANKSEL(0)

    'PRINT AT XY(5, a_popupTop), "Firmware write "
    IF STATUS THEN
     '   PRINT "success"
        clockChanged = TRUE
        GOSUB successMessage
    ELSE
      '  PRINT "failed"
        GOSUB failedMessage
    END IF

    GOSUB waitForInput

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

#endif