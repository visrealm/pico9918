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

DIM tempConfigValues(CONF_COUNT)
DIM savedConfigValues(CONF_COUNT)

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
        PRINT AT XY(0, MENU_HELP_ROW), "  Skipped! No changes to save   "
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
        PRINT AT XY(0, MENU_HELP_ROW), " Success! ** Reboot required ** "
    ELSE
        PRINT AT XY(0, MENU_HELP_ROW), "  Success! Configuration saved  "
    END IF
    END

' -----------------------------------------------------------------------------
' load config values from VDP to tempConfigValues() and savedConfigValues() arrays
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
' apply current options to the PICO9918
' -----------------------------------------------------------------------------
applyConfigValues: PROCEDURE
    VDP(50) = tempConfigValues(0) * $04       ' set crt scanlines
    VDP(30) = pow2(tempConfigValues(1) + 2)   ' set scanline sprites
    END
