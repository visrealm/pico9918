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

    'I = 0
    'FOR B = 1 TO 5
    '    ON B FAST GOSUB ,selectBank1,selectBank2,selectBank3,selectBank4,selectBank5,selectBank6,selectBank7,selectBank8,selectBank9,selectBank10,selectBank11,selectBank12
    '    DEFINE VRAM NAME_TAB_XY(0, menuTopRow + B), 32, VARPTR bank1Start(289)
    'NEXT B

    'BANK SELECT 0

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

selectBank6:
    BANK SELECT 6
    RETURN

selectBank7:
    BANK SELECT 7
    RETURN

selectBank8:
    BANK SELECT 8
    RETURN

selectBank9:
    BANK SELECT 9
    RETURN

selectBank10:
    BANK SELECT 10
    RETURN

selectBank11:
    BANK SELECT 11
    RETURN

selectBank12:
    BANK SELECT 12
    RETURN
