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
' reset options to defaults
' -----------------------------------------------------------------------------
resetOptions: PROCEDURE
    VDP_DISABLE_INT
    g_resetPending = TRUE   ' make next save bypass the display-change confirmation flow
    FOR I = 0 TO CONF_COUNT - 1
        tempConfigValues(I) = 0
    NEXT I
    
    g_paletteDirty = FALSE
    FOR I = 0 TO 31
        tempConfigValues(128 + I) = defPal(I)
        IF tempConfigValues(128 + I) <> savedConfigValues(128 + I) THEN g_paletteDirty = TRUE
    NEXT I

    g_diagDirty = FALSE
    FOR I = CONF_DIAG TO CONF_DIAG_ADDRESS
        IF tempConfigValues(I) <> savedConfigValues(I) THEN g_diagDirty = TRUE
    NEXT I

    FOR I = 0 TO CONF_COUNT - 1
        VDP_CONFIG(I) = tempConfigValues(I)
    NEXT I

    GOSUB applyConfigValues
    GOSUB renderMainMenu
    VDP_ENABLE_INT
    END

' -----------------------------------------------------------------------------
' save the current config to PICO9918 flash
' -----------------------------------------------------------------------------
saveOptions: PROCEDURE

    ' instruct the pico9918 to commit config to flash. After a factory reset,
    ' use the FORCED variant which bypasses the display-change confirmation
    ' flow so the user is not prompted for a change they explicitly initiated.
    IF g_resetPending THEN
        VDP_CONFIG(CONF_SAVE_FORCED) = 1
        g_resetPending = FALSE
    ELSE
        VDP_CONFIG(CONF_SAVE_TO_FLASH) = 1
    END IF

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
    VDP_STATUS_REG = 12    ' read config register
    FOR I = 0 TO CONF_COUNT - 1
        VDP_REG(58) = I
        optValue = VDP_STATUS            
        tempConfigValues(I) = optValue
        savedConfigValues(I) = optValue
    NEXT I
    VDP_STATUS_REG0
#endif
    END

' -----------------------------------------------------------------------------
' apply current options to the PICO9918
' -----------------------------------------------------------------------------
applyConfigValues: PROCEDURE
    VDP_REG(50) = tempConfigValues(CONF_CRT_SCANLINES) * 4         ' set crt scanlines
    VDP_REG(30) = pow2(tempConfigValues(CONF_SCANLINE_SPRITES) + 2)   ' set scanline sprites

    ' write the user's configured palette to page 0 (the target VDP palette)
    PAL_PORT(PAL_PAGE_TARGET, 0)
    DEFINE VRAM 0, 32, VARPTR tempConfigValues(128)
    PAL_PORT_END

    END
