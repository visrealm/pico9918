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

' -----------------------------------------------------------------------------
' reset options to defaults
' -----------------------------------------------------------------------------
resetOptions: PROCEDURE
    FOR I = 0 TO CONF_COUNT - 1
        tempConfigValues(I) = 0
    NEXT I
    g_paletteDirty = FALSE
    FOR I = 0 TO 31
        tempConfigValues(128 + I) = defPal(I)
        IF tempConfigValues(128 + I) <> savedConfigValues(128 + I) THEN g_paletteDirty = TRUE
    NEXT I

    FOR I = CONF_DIAG TO CONF_DIAG_ADDRESS
        IF tempConfigValues(I) <> savedConfigValues(I) THEN g_diagDirty = TRUE
    NEXT I

    GOSUB applyConfigValues
    GOSUB renderMainMenu
    END

' -----------------------------------------------------------------------------
' save the current config to PICO9918 flash
' -----------------------------------------------------------------------------
saveOptions: PROCEDURE

    ' instruct the pico9918 to commit config to flash
    VDP_WRITE_CONFIG(CONF_SAVE_TO_FLASH, 1)

    clockChanged = savedConfigValues(CONF_CLOCK_PRESET_ID) <> tempConfigValues(CONF_CLOCK_PRESET_ID)

    ' update device values again
    FOR I = 0 TO CONF_COUNT - 1
        savedConfigValues(I) = tempConfigValues(I)
    NEXT I

    g_paletteDirty = FALSE
    g_diagDirty = FALSE

    GOSUB renderMainMenu

    END

' -----------------------------------------------------------------------------
' load config values from VDP to tempConfigValues() and savedConfigValues() arrays
' -----------------------------------------------------------------------------
vdpLoadConfigValues: PROCEDURE
#if F18A_TESTING
    FOR I = 0 TO CONF_COUNT - 1
        tempConfigValues(I) = 0
        savedConfigValues(I) = 0
    NEXT I
    FOR I = 0 TO 31
        tempConfigValues(128 + I) = defPal(I)
        savedConfigValues(128 + I) = defPal(I)
    NEXT I
#else
    VDP_SET_CURRENT_STATUS_REG(12)    ' read config register
    FOR I = 0 TO CONF_COUNT - 1
        VDP(58) = I
        optValue = VDP_READ_STATUS            
        tempConfigValues(I) = optValue
        savedConfigValues(I) = optValue
    NEXT I
    VDP_RESET_STATUS_REG
#endif
    END

' -----------------------------------------------------------------------------
' apply current options to the PICO9918
' -----------------------------------------------------------------------------
applyConfigValues: PROCEDURE
    VDP(50) = tempConfigValues(CONF_CRT_SCANLINES) * 4         ' set crt scanlines
    VDP(30) = pow2(tempConfigValues(CONF_SCANLINE_SPRITES) + 2)   ' set scanline sprites

    VDP(47) = $c0
    DEFINE VRAM 0, 32, VARPTR tempConfigValues(128)
    VDP(47) = $40

    END
