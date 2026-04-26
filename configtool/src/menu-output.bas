'
' Project: pico9918
'
' PICO9918 Configurator
'
' This code is licensed under the MIT license
'
' https://github.com/visrealm/pico9918
'

' Output submenu: Driver / VGA mode / SCART mode. All require a reboot.

' Tracked fields for the Output dirty flag. To add a field, append here and
' bump OUTPUT_FIELD_COUNT.
CONST OUTPUT_FIELD_COUNT = 3
outputFields:
    DATA BYTE CONF_DISP_DRIVER_PREF
    DATA BYTE CONF_VGA_MODE
    DATA BYTE CONF_SCART_MODE

recomputeOutputDirty: PROCEDURE
    g_outputDirty = FALSE
    FOR I = 0 TO OUTPUT_FIELD_COUNT - 1
        outputFieldIdx = outputFields(I)
        IF tempConfigValues(outputFieldIdx) <> savedConfigValues(outputFieldIdx) THEN
            g_outputDirty = TRUE
        END IF
    NEXT I
    END

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

    GOSUB recomputeOutputDirty

    END
