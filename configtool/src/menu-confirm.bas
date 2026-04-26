'
' Project: pico9918
'
' PICO9918 Configurator
'
' Startup gates and prompts:
'  - checkFirmwareVersion: refuses to enter the main menu if the device's
'    running firmware is older than the firmware embedded in this configurator.
'    The new configurator's features (Output submenu, pending-change confirm)
'    rely on firmware bytes the old firmware doesn't understand, so silently
'    failing on those features would be worse than refusing to start.
'  - checkPendingDisplayChange: prompts to confirm or revert a display change
'    that the firmware booted into ARMED state.
'
' OK     -> firmware promotes pending values to the main config (permanent).
' Cancel -> firmware erases the pending block. The running firmware continues
'           with the pending values until reboot, but next boot reverts.
'

' -----------------------------------------------------------------------------
' Gate the configurator on the device's firmware version. If the running
' firmware is older than the firmware embedded in this configurator, force the
' user through the firmware update flow (or halt with a USB-update message on
' platforms without embedded firmware). On match-or-newer firmware, returns
' immediately and the configurator proceeds normally.
' -----------------------------------------------------------------------------
checkFirmwareVersion: PROCEDURE
    ' Pack as a 16-bit comparable value: (major << 12) | (minor << 8) | patch.
    ' Matches firmware's PICO9918_SW_VERSION_FULL packing.
    #deviceVer   = (verMajor * 4096) + (verMinor * 256) + verPatch
    #embeddedVer = (FIRMWARE_MAJOR_VER * 4096) + (FIRMWARE_MINOR_VER * 256) + FIRMWARE_PATCH_VER

    IF #deviceVer >= #embeddedVer THEN RETURN

    ' Device firmware is too old. Show explanation, then either drive an
    ' embedded-firmware update (BANK_SIZE > 0) or halt for manual USB update.
    GOSUB clearScreen
    DRAW_TITLE("FIRMWARE OUT OF DATE")

    PRINT AT XY(2, MENU_TITLE_ROW + 3), "Device firmware  : v", verMajor, ".", verMinor, ".", verPatch
    PRINT AT XY(2, MENU_TITLE_ROW + 4), "Required version : v", FIRMWARE_MAJOR_VER, ".", FIRMWARE_MINOR_VER, ".", FIRMWARE_PATCH_VER
    PRINT AT XY(2, MENU_TITLE_ROW + 6), "This configurator needs newer"
    PRINT AT XY(2, MENU_TITLE_ROW + 7), "firmware to work safely."

#if BANK_SIZE
    PRINT AT XY(2, MENU_TITLE_ROW + 9), "Updating firmware now..."

    ' Hand off to the firmware update menu. After it returns, halt - the user
    ' must power-cycle to boot the freshly-flashed firmware. Don't fall through
    ' to the main menu.
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

    ' Halt - the configurator does not proceed beyond this screen.
    WHILE 1
        WAIT
    WEND
    END



checkPendingDisplayChange: PROCEDURE
    ' Read pending state byte from the firmware's in-RAM mirror.
    VDP_STATUS_REG = 12
    VDP_REG(58) = CONF_PENDING_STATE
    pendingState = VDP_STATUS
    VDP_STATUS_REG0

    IF pendingState <> PENDING_STATE_ARMED THEN RETURN

    DRAW_POPUP_W("Display change OK?", 6, 22)

    PRINT AT XY(((32 - 18) / 2), a_popupTop + 1), "If you can read this"

    GOSUB confirmationMenuLoop

    IF confirm THEN
        VDP_CONFIG(CONF_PENDING_CONFIRM) = 1
    ELSE
        VDP_CONFIG(CONF_PENDING_CANCEL) = 1
    END IF

    END
