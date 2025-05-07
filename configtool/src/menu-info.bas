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


deviceInfoMenu: PROCEDURE
    const PICO_MODEL_RP2040 = 1
    const PICO_MODEL_RP2350 = 2

    g_menuTopRow = MENU_TITLE_ROW + 3


    DRAW_TITLE("DEVICE INFO")

    oldMenuTopRow = g_menuTopRow
    oldIndex = g_currentMenuIndex

    g_menuTopRow = MENU_TITLE_ROW + 14
    MENU_INDEX_OFFSET = 13
    MENU_INDEX_COUNT = 1
    MENU_START_X = 6
    g_currentMenuIndex = MENU_INDEX_OFFSET

    GOSUB renderMenu

    g_menuTopRow = oldMenuTopRow

    #addr = XY(2, g_menuTopRow)
    PRINT AT #addr,       "Processor family : "
    PRINT AT #addr + 32,  "Hardware version : "
    PRINT AT #addr + 64,  "Software version : "
    PRINT AT #addr + 96,  "Display driver   : "
    PRINT AT #addr + 128, "Resolution       : "
    PRINT AT #addr + 160, "F18A version     : "
    PRINT AT #addr + 192, "Core temperature : Error"

    VDP_STATUS_REG = 12  ' config

    VDP_REG(58) = CONF_PICO_MODEL
    optValue = VDP_STATUS
    #addr = XY(21, g_menuTopRow + 0)
    PRINT AT #addr, "RP2"
    IF optValue = PICO_MODEL_RP2350 THEN
        PRINT "350"
    ELSE
        PRINT "040"
    END IF

    VDP_REG(58) = CONF_HW_VERSION
    optValue = VDP_STATUS
    tmpMajor = optValue / 16
    tmpMinor = optValue AND $0f
    PRINT AT  #addr + 32, tmpMajor, ".", tmpMinor
    IF verMajor = 1 THEN PRINT AT XY(24, g_menuTopRow + 1), "+"

    VDP_REG(58) = CONF_SW_VERSION
    optValue = VDP_STATUS
    tmpMajor = optValue / 16
    tmpMinor = optValue AND $0f
    VDP_REG(58) = CONF_SW_PATCH_VERSION
    tmpPatch = VDP_STATUS
    PRINT AT #addr + 64, tmpMajor, ".", tmpMinor, ".", tmpPatch

    VDP_REG(58) = CONF_DISP_DRIVER
    optValue = VDP_STATUS
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

    VDP_STATUS_REG = 14      ' SR14: Version
    optValue = VDP_STATUS
    tmpMajor = optValue / 16
    tmpMinor = optValue AND $0f
    PRINT AT XY(21, g_menuTopRow + 5), CHR$(hexChar(tmpMajor)), ".", CHR$(hexChar(tmpMinor))
    VDP_STATUS_REG0

    GOSUB delay

    WHILE 1
        WAIT

        GOSUB updateNavInput
        IF (g_nav AND NAV_CANCEL) OR (g_nav AND NAV_OK) OR (g_nav AND NAV_LEFT) THEN EXIT WHILE
        IF CONT.KEY > 0 AND CONT.KEY <= MENU_INDEX_COUNT THEN EXIT WHILE

        VDP_DISABLE_INT

        VDP_STATUS_REG = 13      ' SR13: Temperature
        optValue = VDP_STATUS
        VDP_STATUS_REG0

        IF optValue > 0 THEN
            tempC = optValue / 4
            tempDec = optValue AND $03
            tempDec = tempDec * 25

            PRINT AT XY(21, g_menuTopRow + 6), tempC, ".", <2>tempDec, "`C  "

            #optValueF = optValue
            #optValueF = #optValueF * 9
            #optValueF = #optValueF / 5

            #tempC = #optValueF / 4 + 32
            #tempDec = #optValueF AND $03
            #tempDec = #tempDec * 25

            PRINT AT XY(19, g_menuTopRow + 7), ": ",#tempC, ".", <2>#tempDec, "`F  "
        END IF

        VDP_ENABLE_INT
    WEND

    g_currentMenuIndex = oldIndex

    SET_MENU(MENU_ID_MAIN)

    END