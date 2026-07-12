'
' Project: pico9918
'
' PICO9918 Configurator
'
' Startup gates: checkFirmwareVersion (forced upgrade if device firmware is
' too old) and checkPendingDisplayChange (prompt user to confirm or revert
' an ARMED display change).
'

' Halt or force-update if device firmware is older than the embedded firmware.
checkFirmwareVersion: PROCEDURE
    ' (major << 12) | (minor << 8) | patch -- matches PICO9918_SW_VERSION_FULL
    #deviceVer   = (verMajor * 4096) + (verMinor * 256) + verPatch
    #embeddedVer = (FIRMWARE_MAJOR_VER * 4096) + (FIRMWARE_MINOR_VER * 256) + FIRMWARE_PATCH_VER

    IF #deviceVer >= #embeddedVer THEN RETURN

    GOSUB clearScreen
    DRAW_TITLE("FIRMWARE OUT OF DATE")

    PRINT AT XY(2, MENU_TITLE_ROW + 3), "Device firmware  : v", verMajor, ".", verMinor, ".", verPatch
    PRINT AT XY(2, MENU_TITLE_ROW + 4), "Required version : v", FIRMWARE_MAJOR_VER, ".", FIRMWARE_MINOR_VER, ".", FIRMWARE_PATCH_VER
    PRINT AT XY(2, MENU_TITLE_ROW + 6), "This configurator needs newer"
    PRINT AT XY(2, MENU_TITLE_ROW + 7), "firmware to work safely."

#if BANK_SIZE
    PRINT AT XY(2, MENU_TITLE_ROW + 9), "Updating firmware now..."

    GOSUB clearScreen
    g_menuTopRow = MENU_TITLE_ROW + 3   ' firmwareMenu reads this for status text
    SET_MENU(MENU_ID_FIRMWARE)
    GOSUB firmwareMenu

    GOSUB clearScreen
    DRAW_TITLE("REBOOT REQUIRED")
    PRINT AT XY(2, MENU_TITLE_ROW + 4), "Power cycle the host to load"
    PRINT AT XY(2, MENU_TITLE_ROW + 5), "the updated firmware."
#else
    PRINT AT XY(2, MENU_TITLE_ROW + 9), "Update via USB:"
    PRINT AT XY(2, MENU_TITLE_ROW + 10), "github.com/visrealm/pico9918"
#endif

    WHILE 1
        WAIT
    WEND
    END



checkPendingDisplayChange: PROCEDURE
    VDP_STATUS_REG = 12
    VDP_REG(58) = CONF_PENDING_STATE
    pendingState = VDP_STATUS
    VDP_STATUS_REG0

    IF pendingState <> PENDING_STATE_ARMED THEN RETURN

    DRAW_POPUP_W("Display change OK?", 6, 22)

    GOSUB confirmationMenuLoop

    IF confirm THEN
        VDP_CONFIG(CONF_PENDING_CONFIRM) = 1
    ELSE
        VDP_CONFIG(CONF_PENDING_CANCEL) = 1
    END IF

    END
