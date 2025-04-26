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


deviceInfoMenu: PROCEDURE
    const PICO_MODEL_RP2040 = 1
    const PICO_MODEL_RP2350 = 2

    menuTopRow = MENU_TITLE_ROW + 3


    DRAW_TITLE("DEVICE INFO")

    oldMenuTopRow = menuTopRow
    oldIndex = g_currentMenuIndex

    menuTopRow = MENU_TITLE_ROW + 14
    MENU_INDEX_OFFSET = 13
    MENU_INDEX_COUNT = 1
    MENU_START_X = 6
    g_currentMenuIndex = MENU_INDEX_OFFSET

    GOSUB renderMenu

    menuTopRow = oldMenuTopRow

    #addr = XY(2, menuTopRow)
    PRINT AT #addr,       "Processor family : "
    PRINT AT #addr + 32,  "Hardware version : "
    PRINT AT #addr + 64,  "Software version : "
    PRINT AT #addr + 96,  "Display driver   : "
    PRINT AT #addr + 128, "Resolution       : "
    PRINT AT #addr + 160, "F18A version     : "
    PRINT AT #addr + 192, "Core temperature : Error"

    VDP_SET_CURRENT_STATUS_REG(12)  ' config

    VDP(58) = CONF_PICO_MODEL
    optValue = VDP_READ_STATUS
    #addr = XY(21, menuTopRow + 0)
    PRINT AT #addr, "RP2"
    IF optValue = PICO_MODEL_RP2350 THEN
        PRINT "350"
    ELSE
        PRINT "040"
    END IF

    VDP(58) = CONF_HW_VERSION
    optValue = VDP_READ_STATUS
    verMaj = optValue / 16
    verMin = optValue AND $0f
    PRINT AT  #addr + 32, verMaj, ".", verMin
    IF verMaj = 1 THEN PRINT AT XY(24, menuTopRow + 1), "+"

    VDP(58) = CONF_SW_VERSION
    optValue = VDP_READ_STATUS
    verMaj = optValue / 16
    verMin = optValue AND $0f
    PRINT AT #addr + 64, verMaj, ".", verMin

    VDP(58) = CONF_DISP_DRIVER
    optValue = VDP_READ_STATUS
    #addr = #addr + 96
    IF optValue = 1 THEN
        PRINT AT #addr, "RGBs NTSC"
        PRINT AT #addr + 32, "480i 60Hz"
    ELSEIF optValue = 2 THEN
        PRINT AT #addr, "RGBs PAL"
        PRINT AT #addr + 32, "576i 50Hz"
    ELSE
        PRINT AT #addr, "VGA"
        PRINT AT #addr + 32, "480p 60Hz"
    END IF

    VDP_SET_CURRENT_STATUS_REG(14)      ' SR14: Version
    optValue = VDP_READ_STATUS
    verMaj = optValue / 16
    verMin = optValue AND $0f
    PUT_XY(21, menuTopRow + 5, hexChar(verMaj))
    PUT_XY(22, menuTopRow + 5, ".")
    PUT_XY(23, menuTopRow + 5, hexChar(verMin))
    VDP_RESET_STATUS_REG

    VDP_ENABLE_INT
    GOSUB delay

    WHILE 1
        WAIT

        GOSUB updateNavInput
        IF (g_nav AND NAV_CANCEL) OR (g_nav AND NAV_OK) OR (g_nav AND NAV_LEFT) THEN EXIT WHILE
        IF CONT.KEY > 0 AND CONT.KEY <= MENU_INDEX_COUNT THEN EXIT WHILE

        VDP_DISABLE_INT

        VDP_SET_CURRENT_STATUS_REG(13)      ' SR13: Temperature
        optValue = VDP_READ_STATUS
        VDP_RESET_STATUS_REG

        IF optValue > 0 THEN
            tempC = optValue / 4
            tempDec = optValue AND $03
            tempDec = tempDec * 25

            PRINT AT XY(21, menuTopRow + 6), tempC, ".", <2>tempDec, "`C  "

            #optValueF = optValue
            #optValueF = #optValueF * 9
            #optValueF = #optValueF / 5

            #tempC = #optValueF / 4 + 32
            #tempDec = #optValueF AND $03
            #tempDec = #tempDec * 25

            PRINT AT XY(19, menuTopRow + 7), ": ",#tempC, ".", <2>#tempDec, "`F  "
        END IF

        VDP_ENABLE_INT
    WEND

    g_currentMenuIndex = oldIndex

    SET_MENU(MENU_ID_MAIN)

    END