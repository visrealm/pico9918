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

    DRAW_TITLE("DEVICE INFO", 11)

    PRINT AT XY(2, MENU_TOP_ROW + 0), "Processor family : "
    PRINT AT XY(2, MENU_TOP_ROW + 1), "Hardware version : "
    PRINT AT XY(2, MENU_TOP_ROW + 2), "Software version : "
    PRINT AT XY(2, MENU_TOP_ROW + 3), "Display driver   : "
    PRINT AT XY(2, MENU_TOP_ROW + 4), "Resolution       : "
    PRINT AT XY(2, MENU_TOP_ROW + 5), "F18A version     : "
    PRINT AT XY(2, MENU_TOP_ROW + 6), "Core temperature : Error"

    VDP_SET_CURRENT_STATUS_REG(12)  ' config

    VDP(58) = CONF_PICO_MODEL
    optValue = VDP_READ_STATUS
    IF optValue = PICO_MODEL_RP2350 THEN
        PRINT AT XY(21, MENU_TOP_ROW + 0), "RP2350"
    ELSE
        PRINT AT XY(21, MENU_TOP_ROW + 0), "RP2040"
    END IF

    VDP(58) = CONF_HW_VERSION
    optValue = VDP_READ_STATUS
    verMaj = optValue / 16
    verMin = optValue AND $0f
    PRINT AT XY(21, MENU_TOP_ROW + 1), verMaj, ".", verMin
    IF verMaj = 1 THEN PRINT AT XY(24, MENU_TOP_ROW + 1), "+"

    VDP(58) = CONF_SW_VERSION
    optValue = VDP_READ_STATUS
    verMaj = optValue / 16
    verMin = optValue AND $0f
    PRINT AT XY(21, MENU_TOP_ROW + 2), verMaj, ".", verMin

    VDP(58) = CONF_DISP_DRIVER
    optValue = VDP_READ_STATUS
    IF optValue = 1 THEN
        PRINT AT XY(21, MENU_TOP_ROW + 3), "RGBs NTSC"
        PRINT AT XY(21, MENU_TOP_ROW + 4), "480i 60Hz"
    ELSEIF optValue = 2 THEN
        PRINT AT XY(21, MENU_TOP_ROW + 3), "RGBs PAL"
        PRINT AT XY(21, MENU_TOP_ROW + 4), "576i 50Hz"
    ELSE
        PRINT AT XY(21, MENU_TOP_ROW + 3), "VGA"
        PRINT AT XY(21, MENU_TOP_ROW + 4), "480p 60Hz"
    END IF

    VDP_SET_CURRENT_STATUS_REG(14)      ' SR14: Version
    optValue = VDP_READ_STATUS
    verMaj = optValue / 16
    verMin = optValue AND $0f
    PUT_XY(21, MENU_TOP_ROW + 5, hexChar(verMaj))
    PUT_XY(22, MENU_TOP_ROW + 5, ".")
    PUT_XY(23, MENU_TOP_ROW + 5, hexChar(verMin))
    VDP_RESET_STATUS_REG

    VDP_ENABLE_INT
    GOSUB delay

    WHILE 1
        WAIT

        GOSUB updateNavInput
        IF (g_nav AND NAV_CANCEL) THEN EXIT WHILE

        VDP_DISABLE_INT

        VDP_SET_CURRENT_STATUS_REG(13)      ' SR13: Temperature
        optValue = VDP_READ_STATUS
        VDP_RESET_STATUS_REG

        IF optValue > 0 THEN
            tempC = optValue / 4
            tempDec = optValue AND $03
            tempDec = tempDec * 25

            PRINT AT XY(21, MENU_TOP_ROW + 6), tempC, ".", <2>tempDec, "`C  "

            #optValueF = optValue
            #optValueF = #optValueF * 9
            #optValueF = #optValueF / 5

            #tempC = #optValueF / 4 + 32
            #tempDec = #optValueF AND $03
            #tempDec = #tempDec * 25

            PRINT AT XY(19, MENU_TOP_ROW + 7), ": ",#tempC, ".", <2>#tempDec, "`F  "
        END IF

        VDP_ENABLE_INT
    WEND

    g_currentMenu = MENU_ID_MAIN

    END