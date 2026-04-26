'
' Project: pico9918
'
' PICO9918 Configurator
'
' This code is licensed under the MIT license
'
' https://github.com/visrealm/pico9918
'

' Output submenu:
'   Driver           AUTO / VGA-HDMI / SCART
'   VGA-HDMI mode    480p60 (extensible)
'   SCART mode       576i50 / 480i60
'
' All three settings require a firmware reboot to take effect (see help text
' in menu-data.bas). The firmware reads CONF_DISP_DRIVER_PREF early at boot to
' pick the system clock; CONF_VGA_MODE / CONF_SCART_MODE are read after
' readConfig().

outputMenu: PROCEDURE

    DRAW_TITLE("OUTPUT")

    GOSUB pushMenuCtx
    SET_MENU_CTX(MENU_OFFSET_OUTPUT, MENU_COUNT_OUTPUT, 1, MENU_TITLE_ROW + 3)
    g_currentMenuIndex = MENU_INDEX_OFFSET

    GOSUB renderMenu

    GOSUB delay

    WHILE 1

        WAIT

        GOSUB menuLoop

        IF NAV(NAV_CANCEL) THEN EXIT WHILE

        IF valueChanged THEN
            RENDER_MENU_ROW(g_currentMenuIndex)
            WAIT
            vdpOptId = MENU_DATA(g_currentMenuIndex, CONF_INDEX)
            IF vdpOptId < 200 THEN
                VDP_CONFIG(vdpOptId) = currentValueIndex
            END IF

            IF vdpOptId = CONF_MENU_CANCEL THEN EXIT WHILE
            GOSUB delay
        END IF

    WEND

    GOSUB popMenuCtx
    SET_MENU(MENU_ID_MAIN)

    g_outputDirty = FALSE
    IF savedConfigValues(CONF_SCART_MODE) <> tempConfigValues(CONF_SCART_MODE) THEN g_outputDirty = TRUE
    IF savedConfigValues(CONF_DISP_DRIVER_PREF) <> tempConfigValues(CONF_DISP_DRIVER_PREF) THEN g_outputDirty = TRUE
    IF savedConfigValues(CONF_VGA_MODE) <> tempConfigValues(CONF_VGA_MODE) THEN g_outputDirty = TRUE

    END
